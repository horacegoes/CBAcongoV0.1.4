#pragma once
// =============================================================================
// SustainEngine.h  -  SUSTAIN mode: sample each phrase, then play ALL of it stretched
//
// This is how the real samplers worked: a phrase is recorded into its own memory and the
// timestretch runs over the whole of it, so it keeps playing after the input has stopped,
// until every bit of the phrase has been used. Each phrase is a voice that freezes the
// sampler settings it started with (model, type, time, pitch, cycle, fade, Auto D,
// quality, XFD, rate, bits); later phrases overlay it with whatever the knobs say then.
// 8 voices like the S950 (oldest is faded out when a 9th starts).
//
// Phrase structure follows the ROM routines exactly: stretching starts with the first
// cycle copied straight (S950: "start pushed to >= D"), periods of P with a jump of the
// cycle length, the end test "a + P (+D) >= end", and the remaining tail copied straight.
// At pitch 0 with a transparent converter an S950 voice is bit-exact with the ROM model.
// Playback starts while the phrase is still being recorded (squeezes < 100 % wait for the
// phrase to end, because they need audio that hasn't arrived yet).
// Header-only C++17, no JUCE, no allocation after prepare().
// =============================================================================

#include "AkaiStretch.h"
#include "SamplerFrontEnd.h"
#include "CongoSampler.h"

namespace cba
{

struct VoiceSettings
{
    CongoSampler::Model model = CongoSampler::Model::S950;
    CongoSampler::Type  type  = CongoSampler::Type::Cyclic;
    double timePct = 100.0, pitchSemis = 0.0, hwRate = 25000.0;
    int    cycle = 1000, quality = 10, xfd = 0, bits = 12;
    bool   poly = true, autoD = false;
    bool   extended = false;             // lift the hardware limits (see CongoSampler::Params)
    int    minDelay = 0;                 // TIGHT: start playback this long after the phrase starts
    bool   waitForEnd = false;           // SAMPLE: only play once the whole step is recorded...
    int    delayAfterEnd = 0;            //         ...and this long after it ends (TIGHT)
    bool   choke = false;                // SAMPLE: a new step cuts the previous one off
    double cycleHost = 0.0;              // > 0: tempo-synced cycle in host samples
    bool   bypassConverter = false;      // tests only: no rate / bit / filter colour
};

class PhraseVoice
{
public:
    static constexpr int chans = 2;

    void prepare (double sampleRate, int64_t maxSamples)
    {
        s950::sineTable();
        sr = sampleRate;
        maxLen = std::max<int64_t> (1024, maxSamples);
        data.assign ((size_t) maxLen * chans, 0);
        fe.prepare (sr);
        clear();
    }

    void clear() noexcept
    {
        gen = Gen::Idle;  recording = false;  len = 0;  fade = 1.0f;  fadeStep = 0.0f;  drained = 0;
    }

    bool isIdle() const noexcept      { return gen == Gen::Idle; }
    bool isRecording() const noexcept { return recording; }
    bool isFading() const noexcept    { return fadeStep > 0.0f; }
    uint64_t order() const noexcept   { return startOrder; }
    int currentCycle() const noexcept { return st.C; }

    void start (const VoiceSettings& settings, uint64_t orderNumber) noexcept
    {
        vs = settings;
        startOrder = orderNumber;
        len = 0;  recording = true;  frames = 0;  endedAt = 0;  justStarted = false;
        fade = 1.0f;  fadeStep = 0.0f;  stalls = 0;  drained = 0;
        ratio = std::exp2 (std::clamp (vs.pitchSemis, -24.5, 24.5) / 12.0);
        fe.reset();
        fe.set (vs.hwRate, vs.bits, ratio, vs.model == CongoSampler::Model::S950 ? 0.4 : 0.45);
        setupState();
        gen = Gen::Wait;
    }

    void stopRecording() noexcept { if (recording) endedAt = frames;  recording = false; }

    // SAMPLE: take the settings in force when playback starts (the step was recorded earlier)
    void retune (const VoiceSettings& s) noexcept
    {
        if (gen != Gen::Wait) return;
        const bool bypass = vs.bypassConverter;
        vs = s;  vs.bypassConverter = bypass;
        ratio = std::exp2 (std::clamp (vs.pitchSemis, -24.5, 24.5) / 12.0);
        fe.set (vs.hwRate, vs.bits, ratio, vs.model == CongoSampler::Model::S950 ? 0.4 : 0.45);
        setupState();
    }
    bool takeJustStarted() noexcept { const bool j = justStarted;  justStarted = false;  return j; }
    bool chokes() const noexcept { return vs.choke; }
    int64_t getShortfall() const noexcept { return shortfall; }

    // fade to silence over n samples, then free the voice (KILL / voice stealing)
    void fadeOut (int n) noexcept
    {
        recording = false;
        if (gen == Gen::Idle) return;
        fadeStep = 1.0f / (float) std::max (1, n);
    }

    // record one input frame (call once per sample while recording)
    void record (const float* in) noexcept
    {
        if (! recording) return;
        float fr[2] = { in[0], in[1] };
        if (! vs.bypassConverter) fe.encode (fr, 2);
        for (int c = 0; c < chans; ++c)
            data[(size_t) len * chans + (size_t) c] = quantise16 (fr[c]);
        if (++len >= maxLen) recording = false;                 // phrase memory full
    }

    // add this voice's next output frame into out[2]; returns false once finished
    bool render (float* out) noexcept
    {
        if (gen == Gen::Idle) return false;
        ++frames;

        if (gen == Gen::Wait)
        {
            bool ready;
            if (vs.waitForEnd) ready = ! recording && frames - endedAt >= vs.delayAfterEnd;
            else               ready = ! recording || (vs.timePct >= 100.0 && len >= startDelay);
            if (! ready || len == 0) return true;
            begin();
            justStarted = true;
        }

        float y[2] = { 0.0f, 0.0f };
        if (! produce (y)) return false;                            // finished

        if (! vs.bypassConverter) fe.decode (y, 2);
        if (fadeStep > 0.0f)
        {
            fade -= fadeStep;
            if (fade <= 0.0f) { clear();  return false; }
        }
        out[0] += y[0] * fade;  out[1] += y[1] * fade;
        return true;
    }

    int getStalls() const noexcept { return stalls; }

private:
    enum class Gen { Idle, Wait, LeadIn, Periods, Tail, Drain };

    struct State
    {
        int64_t  a = 0, b = 0;
        int      i = 0, P = 1, C = 1000;
        int64_t  pctH = 10000;
        bool     compress = false, first = true;
        uint32_t inc20 = 0, ratio88 = 512, L = 1, inc32 = 0;
        bool     rate44k = true;
        s2000::Ramp ramp;
    };

    static int16_t quantise16 (float x) noexcept
    {
        const float v = std::floor (std::clamp (x, -1.0f, 1.0f) * 32768.0f + 0.5f);
        return (int16_t) std::clamp ((int) v, -32768, 32767);
    }

    bool isS950() const noexcept   { return vs.model == CongoSampler::Model::S950; }
    bool isIntell() const noexcept { return ! isS950() && vs.type == CongoSampler::Type::Intelligent; }

    int16_t x (int64_t k, int c) const noexcept                    // phrase sample, 0 outside
    {
        if (k < 0 || k >= len) return 0;
        return data[(size_t) k * chans + (size_t) c];
    }
    int16_t mono (int64_t k) const noexcept { return (int16_t) (((int) x (k, 0) + (int) x (k, 1)) / 2); }

    int cycleSamples (int lo, int hi) const noexcept
    {
        if (vs.cycleHost > 0.0) return (int) std::clamp (std::llround (vs.cycleHost), 1LL, (long long) maxLen / 4);
        return toHost (vs.extended ? std::clamp (vs.cycle, 5, 9999) : std::clamp (vs.cycle, lo, hi));
    }

    int toHost (int hw) const noexcept
    {
        return std::max (1, (int) std::lround (hw * sr / (vs.hwRate > 0.0 ? vs.hwRate : sr)));
    }

    void setS950Period (int D) noexcept
    {
        int P = D;
        if (st.pctH != 10000)
        {
            const int64_t cap = vs.extended ? (1LL << 30) : 32767;
            for (;;)
            {
                const int64_t q = (int64_t) D * st.pctH / std::llabs (st.pctH - 10000);
                if (q <= cap || D <= 1) { P = (int) std::min<int64_t> (q, cap); break; }
                D >>= 1;                                        // ROM overflow rule
            }
        }
        st.C = std::max (1, D);  st.P = std::max (1, P);
        st.inc20 = (1u << 20) / (uint32_t) st.P;
    }

    void setS2000Period() noexcept
    {
        st.P = (int) std::clamp<int64_t> ((int64_t) (((uint64_t) st.C * st.ratio88) >> 8), 1, 1LL << 30);
        st.L = s2000::fadeLen ((uint32_t) st.C, std::clamp (vs.xfd, 0, 20), (uint32_t) st.P);
        st.inc32 = s2000::fadeInc (st.L);
    }

    int maxJump() const noexcept
    {
        return isIntell() ? s2000::intelCfg (std::clamp (vs.quality, 0, 20), st.rate44k).range + 8 : st.C;
    }
    int64_t lookahead() const noexcept
    {
        return isIntell() ? (int64_t) s2000::maxCompareLen (st.ratio88, std::clamp (vs.xfd, 0, 20),
                                                            s2000::intelCfg (std::clamp (vs.quality, 0, 20), st.rate44k).range) + 8
                          : 0;
    }

    void setupState() noexcept
    {
        st = {};
        const double internal = std::clamp (vs.timePct, 25.0, 2000.0) * ratio;
        st.pctH = std::max<int64_t> (1, (int64_t) std::llround (internal * 100.0));
        if (isS950())
            setS950Period (cycleSamples (5, 9999));
        else
        {
            // exactly 100 %: play straight through (the formula would divide by zero)
            st.ratio88 = st.pctH == 10000 ? 256u                // P = C with no jump: a clean pass-through
                       : std::max<uint32_t> (1, (uint32_t) (((uint64_t) st.pctH << 8) / (uint64_t) std::llabs (st.pctH - 10000)));
            st.rate44k = vs.hwRate > 33075.0;
            st.C = cycleSamples (20, 2000);
            setS2000Period();
        }
        st.compress = st.pctH < 10000;

        // playback may start before the phrase is complete: enough lead so that reading at
        // the pitch ratio never needs audio that hasn't been recorded yet (capped at 0.5 s)
        const double fast = ratio > 1.0 ? 1.0 - 1.0 / ratio : 0.0;
        const double need = (double) (maxJump() + st.P) * fast + (double) lookahead()
                          + (st.compress ? maxJump() : 0) + 2.0 * std::ceil (ratio) + 4.0;
        startDelay = std::min<int64_t> ((int64_t) std::ceil (need), (int64_t) (0.5 * sr));
        shortfall  = std::max<int64_t> (0, startDelay - vs.minDelay);           // TIGHT: still this late
        startDelay = std::max<int64_t> (startDelay, (int64_t) vs.minDelay);
    }

    void begin() noexcept
    {
        pos = 0;  phase = 0.0;
        if (st.compress) { gen = Gen::Periods;  st.first = true;  st.i = st.P;  st.a = 0;  pendingFirst = true; }
        else             { gen = Gen::LeadIn;   leadLen = maxJump(); }
        float g[3][2] {};
        int got = 0;
        for (int k = 0; k < 3 && got < 3; ++k)
            if (generate (g[got])) ++got;
        for (int c = 0; c < chans; ++c)
        {
            h[0][c] = g[0][c];  h[1][c] = g[0][c];  h[2][c] = g[1][c];  h[3][c] = g[2][c];
        }
    }

    // ---- the ROM period structure on the phrase ----------------------------------------
    bool startPeriod (int64_t a) noexcept
    {
        if (isS950() && vs.autoD && st.first)
        {
            const int64_t end = len;
            const int D = s950::autoD ([this] (int64_t k) { return (int) (mono (k) >> 8); }, end, st.C);
            setS950Period (D);
        }
        if (isIntell())
        {
            // the search reads up to a + compare length (and a + lag when squeezing)
            if (recording && a + lookahead() + (st.compress ? maxJump() : 0) >= len) return false;
            st.C = s2000::intelSearch ([this] (int64_t k) { return mono (k); }, a, st.compress, st.ratio88,
                                       std::clamp (vs.xfd, 0, 20), std::clamp (vs.quality, 0, 20), st.rate44k);
            setS2000Period();
        }
        st.a = a;  st.i = 0;  st.ramp = {};
        st.b = a + (st.pctH > 10000 ? -st.C : (st.pctH < 10000 ? st.C : 0));
        st.first = false;
        return true;
    }

    // one generated sample at the internal rate; false = stall (not recorded yet) or done
    bool generate (float* out) noexcept
    {
        switch (gen)
        {
            case Gen::LeadIn:
            {
                if (pos >= leadLen) { gen = Gen::Periods;  pendingFirst = true;  return generate (out); }
                if (recording && pos >= len) return false;
                if (! recording && pos >= len) { gen = Gen::Drain;  return generate (out); }
                plain (pos++, out);
                return true;
            }
            case Gen::Periods:
            {
                if (pendingFirst || st.i >= st.P)
                {
                    const int64_t next = pendingFirst ? (st.compress ? 0 : leadLen) : st.b + st.P;
                    // ROM end test (not before the first period): a + P (+D) >= end -> tail
                    if (! pendingFirst && ! recording && next + st.P + (st.compress ? st.C : 0) >= len)
                    {
                        gen = Gen::Tail;  pos = next;
                        return generate (out);
                    }
                    if (! startPeriod (next)) return false;
                    pendingFirst = false;
                }
                const int64_t ia = st.a + st.i, ib = st.b + st.i;
                if (recording && std::max (ia, ib) >= len) return false;
                if (isS950())
                    for (int c = 0; c < chans; ++c)
                    {
                        const int32_t w = s950::mixWord (x (ia, c) >> 4, x (ib, c) >> 4, st.i, st.inc20, vs.poly);
                        out[c] = (float) (w >> 4) / 2048.0f;
                    }
                else
                {
                    const uint16_t g = st.ramp.hi;
                    for (int c = 0; c < chans; ++c)
                        out[c] = (float) s2000::mixWord (x (ia, c), x (ib, c), g) / 32768.0f;
                    st.ramp.step (st.inc32);
                }
                ++st.i;
                return true;
            }
            case Gen::Tail:
            {
                if (pos >= len) { gen = Gen::Drain;  return generate (out); }
                plain (pos, out);
                const float g = std::min (1.0f, (float) (len - pos) / 64.0f);   // de-click the very end
                if (! vs.bypassConverter) { out[0] *= g;  out[1] *= g; }
                ++pos;
                return true;
            }
            case Gen::Drain:
                out[0] = out[1] = 0.0f;
                if (++drained > 4) { clear(); return false; }
                return true;
            default:
                return false;
        }
    }

    void plain (int64_t k, float* out) const noexcept
    {
        for (int c = 0; c < chans; ++c)
            out[c] = isS950() ? (float) (x (k, c) >> 4) / 2048.0f : (float) x (k, c) / 32768.0f;
    }

    // read the generated stream at the pitch ratio (Catmull-Rom cubic)
    bool produce (float* y) noexcept
    {
        const float t = (float) phase;
        for (int c = 0; c < chans; ++c)
        {
            const float y0 = h[0][c], y1 = h[1][c], y2 = h[2][c], y3 = h[3][c];
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            y[c] = ((c3 * t + c2) * t + c1) * t + y1;
        }
        double nextPhase = phase + ratio;
        while (nextPhase >= 1.0)
        {
            float g[2];
            if (! generate (g))
            {
                if (gen == Gen::Idle) return false;
                ++stalls;                                           // not recorded yet: hold
                return true;
            }
            nextPhase -= 1.0;
            for (int c = 0; c < chans; ++c)
            {
                h[0][c] = h[1][c];  h[1][c] = h[2][c];  h[2][c] = h[3][c];  h[3][c] = g[c];
            }
        }
        phase = nextPhase;
        return true;
    }

    std::vector<int16_t> data;
    int64_t  maxLen = 0, len = 0, pos = 0, leadLen = 0, startDelay = 0, frames = 0, endedAt = 0, shortfall = 0;
    bool     justStarted = false;
    double   sr = 44100.0, ratio = 1.0, phase = 0.0;
    float    h[4][2] {}, fade = 1.0f, fadeStep = 0.0f;
    bool     recording = false, pendingFirst = false;
    int      drained = 0, stalls = 0;
    uint64_t startOrder = 0;
    Gen      gen = Gen::Idle;
    State    st;
    VoiceSettings vs;
    SamplerFrontEnd fe;
};

// =============================================================================
class SustainEngine
{
public:
    static constexpr int maxVoices = 8;
    static constexpr int slots = maxVoices + 2;      // room for voices that are fading out

    // phrase memory per voice: 10 s at up to 48 kHz (shorter in seconds at higher rates)
    void prepare (double sampleRate, double maxPhraseSeconds = 10.0)
    {
        sr = sampleRate;
        const auto n = (int64_t) std::min (maxPhraseSeconds * sampleRate, maxPhraseSeconds * 48000.0);
        for (auto& v : voices) v.prepare (sr, n);
        clear();
    }

    void clear() noexcept
    {
        for (auto& v : voices) v.clear();
        current = -1;
    }

    void startPhrase (const VoiceSettings& s) noexcept
    {
        endPhrase();
        int active = 0, oldest = -1, freeSlot = -1;
        for (int k = 0; k < slots; ++k)
        {
            auto& v = voices[(size_t) k];
            if (v.isIdle()) { if (freeSlot < 0) freeSlot = k;  continue; }
            if (v.isFading()) continue;
            ++active;
            if (oldest < 0 || v.order() < voices[(size_t) oldest].order()) oldest = k;
        }
        if (active >= maxVoices && oldest >= 0) voices[(size_t) oldest].fadeOut ((int) (0.005 * sr));
        if (freeSlot < 0)                                              // everything still fading
        {
            freeSlot = 0;
            for (int k = 1; k < slots; ++k)
                if (voices[(size_t) k].order() < voices[(size_t) freeSlot].order()) freeSlot = k;
            voices[(size_t) freeSlot].clear();
        }
        voices[(size_t) freeSlot].start (s, ++counter);
        current = freeSlot;
    }

    void endPhrase (const VoiceSettings* playWith = nullptr) noexcept
    {
        if (current >= 0)
        {
            voices[(size_t) current].stopRecording();
            if (playWith != nullptr) voices[(size_t) current].retune (*playWith);
        }
        current = -1;
    }

    bool isRecording() const noexcept
    {
        return current >= 0 && voices[(size_t) current].isRecording();
    }

    void killAll (int fadeSamples) noexcept
    {
        for (auto& v : voices) v.fadeOut (fadeSamples);
        current = -1;
    }

    // in: this frame's input (recorded into the current phrase), out: sum of all voices
    void processFrame (const float* in, float* out) noexcept
    {
        if (current >= 0)
        {
            auto& v = voices[(size_t) current];
            v.record (in);
            if (! v.isRecording()) current = -1;                       // phrase memory full
        }
        out[0] = out[1] = 0.0f;
        for (auto& v : voices) v.render (out);
        // SAMPLE: when a new step starts playing, the previous one fades out (5 ms)
        for (int k = 0; k < slots; ++k)
            if (voices[(size_t) k].takeJustStarted() && voices[(size_t) k].chokes())
                for (int j = 0; j < slots; ++j)
                    if (j != k && ! voices[(size_t) j].isIdle() && ! voices[(size_t) j].isRecording()
                        && voices[(size_t) j].chokes() && ! voices[(size_t) j].isFading())
                        voices[(size_t) j].fadeOut ((int) (0.005 * sr));
    }

    // TIGHT: how late the newest voice still is (needs more look-ahead than there is)
    int64_t newestShortfall() const noexcept
    {
        int best = -1;  uint64_t ord = 0;
        for (int k = 0; k < slots; ++k)
            if (! voices[(size_t) k].isIdle() && voices[(size_t) k].order() >= ord) { ord = voices[(size_t) k].order();  best = k; }
        return best >= 0 ? voices[(size_t) best].getShortfall() : 0;
    }

    int activeVoices() const noexcept
    {
        int n = 0;
        for (auto& v : voices) if (! v.isIdle() && ! v.isFading()) ++n;
        return n;
    }

    int newestCycle() const noexcept
    {
        int best = -1;  uint64_t ord = 0;
        for (int k = 0; k < slots; ++k)
            if (! voices[(size_t) k].isIdle() && voices[(size_t) k].order() >= ord)
            { ord = voices[(size_t) k].order();  best = k; }
        return best >= 0 ? voices[(size_t) best].currentCycle() : 0;
    }

    PhraseVoice& voice (int k) noexcept { return voices[(size_t) k]; }       // tests

private:
    std::array<PhraseVoice, slots> voices;
    double   sr = 44100.0;
    int      current = -1;
    uint64_t counter = 0;
};

} // namespace cba
