#pragma once
// =============================================================================
// CongoSampler.h  -  real-time Akai sampler timestretch with independent pitch
//
// Time  : final length 25..2000 % (S2000 range).
// Pitch : -24..+24 semitones (+-50 cents fine), played like a sampler key: the stretch
//         is run at (time x pitch ratio) and the result is read back at the pitch ratio,
//         so pitch changes without changing the timing (the Akaizer convention).
// Models: S950 (12-bit integer path, MONO/POLY fades, Auto D) and S2000 (16-bit,
//         CYCLIC / INTELL with Quality + XFD). Arithmetic is the ROM-derived code in
//         AkaiStretch.h, unchanged; it is bit-exact at pitch 0 (see tests/).
// The cycle length is in samples at the chosen sampler rate, as on the hardware.
// Header-only C++17, no JUCE dependency, no allocation after prepare(), zero latency.
// =============================================================================

#include "AkaiStretch.h"

namespace cba
{

class CongoSampler
{
public:
    static constexpr int maxChannels = 2;

    enum class Model { S950, S2000 };
    enum class Type  { Cyclic, Intelligent };

    struct Params
    {
        Model  model    = Model::S950;
        Type   type     = Type::Cyclic;
        double timePct  = 100.0;    // 25 .. 2000
        double pitchSemis = 0.0;    // -24.5 .. +24.5 (semitones + cents/100)
        int    cycle    = 1000;     // S950 5..9999, S2000 20..2000 (sampler samples)
        bool   poly     = true;     // S950 fade: false = MONO (linear), true = POLY (sine)
        bool   autoD    = false;    // S950 Auto D at each trigger
        int    quality  = 10;       // S2000 INTELL 0..20
        int    xfd      = 0;        // S2000 0..20
        double hwRate   = 44100.0;  // sampler sample rate the cycle length is counted at
        int    maxLag   = 88200;    // free-run: re-anchor when this far behind / ahead
        bool   extended = false;    // lift the hardware limits (cycle ranges, S950 16-bit period)
        int    minLag   = 0;        // TIGHT: always read at least this far behind (the reported look-ahead)
        double cycleHost = 0.0;     // > 0: cycle given directly in host samples (tempo sync)
    };

    void prepare (double sampleRate, int numChannels, double maxBufferSeconds = 12.0)
    {
        s950::sineTable();
        sr = sampleRate;
        chans = std::clamp (numChannels, 1, maxChannels);
        const auto needed = (size_t) std::ceil (maxBufferSeconds * sampleRate);
        size = 1;  while (size < needed) size <<= 1;
        mask = (int64_t) size - 1;
        buf.assign (size * (size_t) chans, 0);
        mono.assign (size, 0);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), (int16_t) 0);
        std::fill (mono.begin(), mono.end(), (int16_t) 0);
        w = -1;  active = false;  declick = 0;  reanchors = 0;
        cur = {};  old = {};
    }

    // retrigger crossfade (plugin-only, not hardware). The processor triggers this much early
    // so the fade is finished by the beat and the attack lands on it at full level.
    static constexpr int declickLen = 64;

    void setParams (const Params& p) noexcept { pending = p; }

    // Start a pass at the next sample. passLength = output samples until the next
    // trigger (0 = unknown / free-running).
    // onTheGrid: a RETRIG beat on the host's timeline (TIGHT's look-ahead applies); false for
    // things that happen 'now', like pressing TEST
    void trigger (int64_t passLength = 0, bool onTheGrid = true) noexcept
    {
        beginDeclick();
        Voice v;
        latch (v.st);
        v.ratio = pitchRatio();
        const int64_t now = w + 1;

        if (v.st.model == Model::S950 && pending.autoD)
        {
            const int D = s950::autoD ([this] (int64_t k) { return (int) (mono[(size_t) (k & mask)] >> 8); },
                                       now, v.st.C);
            setS950Period (v.st, D);
            lastAutoD = v.st.C;
        }

        // one cycle of headroom: within a pass the lag swings by up to one jump
        int64_t back = ahead (v.st, v.ratio) + (v.ratio != 1.0 ? maxJump (v.st) + 64 : 0);
        const int64_t minLag = onTheGrid ? pending.minLag : 0;
        const double timePct = std::clamp (pending.timePct, 25.0, 2000.0);
        if (timePct < 100.0)                                    // lag shrinks: start in the past
        {
            const double L = (double) (passLength > 0 ? passLength : pending.maxLag);
            back += (int64_t) std::ceil (L * std::max (0.0, 100.0 / timePct - 1.0) * 1.02)
                  + 2 * maxJump (v.st) + 64;
        }
        back = std::min<int64_t> (back, (int64_t) size / 2);
        lastShortfall = std::max<int64_t> (0, back - minLag);           // how late TIGHT still is
        back = std::max<int64_t> (back, minLag);
        // TIGHT + a RETRIG step: the voice may only use this step's audio, never the next beat's
        v.st.limit = (minLag > 0 && passLength > 0) ? now - back + declickLen + passLength : INT64_MAX;
        v.st.limitGain = 1.0f;

        // like the ROM: a stretch starts with its first cycle played straight, so the attack at
        // the start lands on time; then the periods begin one cycle in
        if (v.st.pctH > 10000) { v.st.lead = (int) maxJump (v.st);  v.st.leadPos = now - back;  v.st.i = 0; }
        else                   beginPeriod (v.st, now - back);
        lastLead = v.st.lead;
        lastTriggerStart = now - back;

        // prime the interpolator: first output is exactly the first generated sample
        float g0[maxChannels], g1[maxChannels], g2[maxChannels];
        generate (v.st, g0);  generate (v.st, g1);  generate (v.st, g2);
        for (int c = 0; c < chans; ++c)
        {
            v.h[0][c] = g0[c];  v.h[1][c] = g0[c];  v.h[2][c] = g1[c];  v.h[3][c] = g2[c];
        }
        v.phase = 0.0;
        cur = v;  active = true;
    }

    bool isActive() const noexcept        { return active; }
    int  getLastAutoD() const noexcept    { return lastAutoD; }
    int  getCurrentCycle() const noexcept { return cur.st.C; }
    int  getReanchorCount() const noexcept { return reanchors; }
    int64_t getLastTriggerStart() const noexcept { return lastTriggerStart; }   // for tests
    double  getShortfallSamples() const noexcept { return (double) lastShortfall; } // TIGHT: still this late
    int     getLastLead() const noexcept { return lastLead; }   // the straight first cycle (tests)

    // Process one frame in place. Before the first trigger the input passes through.
    void processFrame (float* frame) noexcept
    {
        ++w;
        int32_t sum = 0;
        for (int c = 0; c < chans; ++c)
        {
            const int16_t q = quantise16 (frame[c]);
            buf[(size_t) (w & mask) * (size_t) chans + (size_t) c] = q;
            sum += q;
        }
        mono[(size_t) (w & mask)] = (int16_t) (sum / chans);

        if (! active) return;

        float wet[maxChannels];
        cur.ratio = pitchRatio();
        render (cur, wet);

        if (declick > 0)
        {
            float prev[maxChannels];
            if (oldIsDry) for (int c = 0; c < chans; ++c) prev[c] = frame[c];
            else          { old.ratio = cur.ratio;  render (old, prev); }
            const float t = 1.0f - (float) declick / (float) declickLen;
            for (int c = 0; c < chans; ++c) wet[c] = prev[c] + t * (wet[c] - prev[c]);
            --declick;
        }
        for (int c = 0; c < chans; ++c) frame[c] = wet[c];
    }

private:
    struct State
    {
        Model    model = Model::S950;
        Type     type = Type::Cyclic;
        int64_t  a = 0, b = 0;
        int      i = 0, P = 1, C = 1000;
        int64_t  pctH = 10000;           // internal stretch in 1/100 %
        bool     compress = false;
        bool     poly = true;
        uint32_t inc20 = 0;
        uint32_t ratio88 = 512, L = 1, inc32 = 0;
        int      xfd = 0, quality = 10;
        bool     rate44k = true;
        s2000::Ramp ramp;
        int      lead = 0;               // ROM start: the first cycle plays straight before stretching
        int64_t  leadPos = 0;
        int64_t  limit = INT64_MAX;      // TIGHT: this step may not use audio from the next step...
        float    limitGain = 1.0f;       // ...and fades out (like a sample ending) if it gets there
    };

    struct Voice
    {
        State  st;
        float  h[4][maxChannels] {};
        double phase = 0.0, ratio = 1.0;
    };

    // ---- helpers -------------------------------------------------------------------
    static int16_t quantise16 (float x) noexcept
    {
        const float v = std::floor (std::clamp (x, -1.0f, 1.0f) * 32768.0f + 0.5f);
        return (int16_t) std::clamp ((int) v, -32768, 32767);
    }

    double pitchRatio() const noexcept
    {
        return std::exp2 (std::clamp (pending.pitchSemis, -24.5, 24.5) / 12.0);
    }

    int16_t at (int64_t k, int c) const noexcept
    {
        k = std::clamp (k, w - (int64_t) size + 1, w);          // safety net only
        return buf[(size_t) (k & mask) * (size_t) chans + (size_t) c];
    }
    int16_t monoAt (int64_t k) const noexcept
    {
        k = std::clamp (k, w - (int64_t) size + 1, w);
        return mono[(size_t) (k & mask)];
    }

    // cycle in host samples: the hardware range, the knob's full range (extended), or a tempo-synced value
    int cycleSamples (int lo, int hi) const noexcept
    {
        if (pending.cycleHost > 0.0)
            return (int) std::clamp (std::llround (pending.cycleHost), 1LL, (long long) size / 8);
        return toHost (pending.extended ? std::clamp (pending.cycle, 5, 9999) : std::clamp (pending.cycle, lo, hi));
    }

    int toHost (int hwSamples) const noexcept
    {
        const double hw = pending.hwRate > 0.0 ? pending.hwRate : sr;
        return std::max (1, (int) std::lround (hwSamples * sr / hw));
    }

    // S950 period with the internal stretch in hundredths of a percent. For whole
    // percentages this is identical to the ROM's P = D*pct/|pct-100| (incl. halving).
    void setS950Period (State& s, int D) noexcept
    {
        int P = D;
        if (s.pctH != 10000)
        {
            const int64_t cap = pending.extended ? (1LL << 30) : 32767;     // ROM: int16 period
            for (;;)
            {
                const int64_t q = (int64_t) D * s.pctH / std::llabs (s.pctH - 10000);
                if ((q <= cap && ! needsHalving (q)) || D <= 1) { P = (int) std::min<int64_t> (q, cap); break; }
                D >>= 1;
            }
        }
        s.C = std::max (1, D);  s.P = std::max (1, P);
        s.inc20 = (1u << 20) / (uint32_t) s.P;
    }

    // Real time with pitch up: a period reads P samples in P/r output samples, so it must
    // start P*(1-1/r) in the past. Near 100 % internal stretch P gets huge; keep that inside
    // the buffer by halving the cycle (the S950 ROM's own overflow rule). Never at pitch 0.
    int64_t readAheadBudget() const noexcept { return (int64_t) size / 4; }
    bool needsHalving (int64_t P) const noexcept
    {
        const double r = pitchRatio();
        return r > 1.0 && (double) P * (1.0 - 1.0 / r) > (double) readAheadBudget();
    }

    // 64-bit version of the ROM's (C * ratio) >> 8; huge periods are clamped (~6 h at 48k)
    static int64_t s2000Period (int C, uint32_t ratio88) noexcept
    {
        return std::clamp<int64_t> ((int64_t) (((uint64_t) C * ratio88) >> 8), 1, 1LL << 30);
    }

    void setS2000Period (State& s) noexcept
    {
        while (s.C > 1 && needsHalving (s2000Period (s.C, s.ratio88)))
            s.C >>= 1;
        s.P = (int) s2000Period (s.C, s.ratio88);
        s.L = s2000::fadeLen ((uint32_t) s.C, s.xfd, (uint32_t) s.P);
        s.inc32 = s2000::fadeInc (s.L);
    }

    void latch (State& s) noexcept
    {
        const Params& p = pending;
        s.model = p.model;
        s.type  = p.model == Model::S2000 ? p.type : Type::Cyclic;
        const double internal = std::clamp (p.timePct, 25.0, 2000.0) * pitchRatio();
        s.pctH = std::max<int64_t> (1, (int64_t) std::llround (internal * 100.0));
        if (s.model == Model::S950)
        {
            s.poly = p.poly;
            setS950Period (s, cycleSamples (5, 9999));
        }
        else
        {
            // exactly 100 %: the formula would divide by zero - play straight through (no jumps)
            s.ratio88 = s.pctH == 10000 ? 256u                // P = C with no jump: a clean pass-through
                      : (uint32_t) (((uint64_t) s.pctH << 8) / (uint64_t) std::llabs (s.pctH - 10000));
            s.ratio88 = std::max<uint32_t> (1, s.ratio88);
            s.xfd     = std::clamp (p.xfd, 0, 20);
            s.quality = std::clamp (p.quality, 0, 20);
            s.rate44k = (p.hwRate > 0.0 ? p.hwRate : sr) > 33075.0;
            s.C = cycleSamples (20, 2000);
            setS2000Period (s);
        }
        s.compress = s.pctH < 10000;
    }

    int64_t maxJump (const State& s) const noexcept
    {
        if (s.model == Model::S2000 && s.type == Type::Intelligent)
            return s2000::intelCfg (s.quality, s.rate44k).range + 8;
        return s.C;
    }

    int64_t lookahead (const State& s) const noexcept
    {
        if (s.model == Model::S2000 && s.type == Type::Intelligent)
            return (int64_t) s2000::maxCompareLen (s.ratio88, s.xfd,
                                                   s2000::intelCfg (s.quality, s.rate44k).range) + 8;
        return 0;
    }

    // how far behind "now" a period must start so that nothing it reads (at pitch
    // ratio r, with the interpolator running 2 samples ahead) is in the future
    int64_t ahead (const State& s, double r) const noexcept
    {
        const double perPeriod = r > 1.0 ? (double) s.P * (1.0 - 1.0 / r) : 0.0;
        return (int64_t) std::ceil (perPeriod) + (s.compress ? maxJump (s) : 0)
             + lookahead (s) + 2 * (int64_t) std::ceil (r) + 2;
    }

    void beginPeriod (State& s, int64_t a) noexcept
    {
        s.a = a;  s.i = 0;
        if (s.model == Model::S2000)
        {
            if (s.type == Type::Intelligent)
            {
                s.C = s2000::intelSearch ([this] (int64_t k) { return monoAt (k); },
                                          a, s.compress, s.ratio88, s.xfd, s.quality, s.rate44k);
                setS2000Period (s);
            }
            s.ramp = {};
        }
        const int64_t jump = s.pctH > 10000 ? -s.C : (s.pctH < 10000 ? s.C : 0);
        s.b = a + jump;
    }

    void nextPeriod (State& s, double r) noexcept
    {
        const int64_t now = w;
        int64_t a = s.b + s.P;                                  // new voice continues as current
        latch (s);
        const int64_t need = std::max<int64_t> (ahead (s, r), pending.minLag);
        bool moved = false;
        if (pending.timePct >= 100.0)                           // lag grows (or holds)
        {
            if (pending.maxLag > 0 && now - a > pending.maxLag) { a = now - need;  moved = true; }
            if (a > now - need)                                 { a = now - need;  moved = true; }
        }
        else if (a > now - need)
        {
            a = now - need - std::max<int64_t> (pending.maxLag, 4LL * (maxJump (s) + s.P));
            moved = true;
        }
        const int64_t oldest = w - (int64_t) size + 1 + 2LL * maxJump (s) + 8;
        if (a < oldest) { a = now - need;  moved = true; }
        if (moved) ++reanchors;
        beginPeriod (s, a);
    }

    // one sample of the stretch at the internal rate
    void generate (State& s, float* out) noexcept
    {
        generateRaw (s, out);
        if (s.limit != INT64_MAX)
        {
            const int64_t reach = s.lead > 0 ? s.leadPos : std::max (s.a + s.i, s.b + s.i);
            // fade out over the last stretch before the boundary, so nothing of the next beat gets through
            if (reach >= s.limit - declickLen) s.limitGain = std::max (0.0f, s.limitGain - 1.0f / (float) declickLen);
            for (int c = 0; c < chans; ++c) out[c] *= s.limitGain;
        }
    }

    void generateRaw (State& s, float* out) noexcept
    {
        if (s.lead > 0)                                         // the straight first cycle
        {
            for (int c = 0; c < chans; ++c)
                out[c] = s.model == Model::S950 ? (float) (at (s.leadPos, c) >> 4) / 2048.0f
                                                : (float) at (s.leadPos, c) / 32768.0f;
            ++s.leadPos;
            if (--s.lead == 0) beginPeriod (s, s.leadPos);
            return;
        }
        if (s.i >= s.P) nextPeriod (s, cur.ratio);
        if (s.model == Model::S950)
        {
            for (int c = 0; c < chans; ++c)
            {
                const int32_t word = s950::mixWord (at (s.a + s.i, c) >> 4, at (s.b + s.i, c) >> 4,
                                                    s.i, s.inc20, s.poly);
                out[c] = (float) (word >> 4) / 2048.0f;         // 12-bit DAC
            }
        }
        else
        {
            const uint16_t g = s.ramp.hi;
            for (int c = 0; c < chans; ++c)
                out[c] = (float) s2000::mixWord (at (s.a + s.i, c), at (s.b + s.i, c), g) / 32768.0f;
            s.ramp.step (s.inc32);
        }
        ++s.i;
    }

    // read the generated stream at the pitch ratio (Catmull-Rom cubic)
    void render (Voice& v, float* out) noexcept
    {
        const float t = (float) v.phase;
        for (int c = 0; c < chans; ++c)
        {
            const float y0 = v.h[0][c], y1 = v.h[1][c], y2 = v.h[2][c], y3 = v.h[3][c];
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            out[c] = ((c3 * t + c2) * t + c1) * t + y1;
        }
        v.phase += v.ratio;
        while (v.phase >= 1.0)
        {
            v.phase -= 1.0;
            float g[maxChannels];
            generate (v.st, g);
            for (int c = 0; c < chans; ++c)
            {
                v.h[0][c] = v.h[1][c];  v.h[1][c] = v.h[2][c];  v.h[2][c] = v.h[3][c];  v.h[3][c] = g[c];
            }
        }
    }

    void beginDeclick() noexcept
    {
        oldIsDry = ! active;
        old = cur;
        declick = declickLen;
    }

    std::vector<int16_t> buf, mono;
    size_t  size = 0;
    int64_t mask = 0, w = -1;
    int     chans = 2;
    double  sr = 44100.0;

    Params pending;
    Voice  cur, old;
    bool   active = false, oldIsDry = true;
    int    declick = 0, lastAutoD = 0, reanchors = 0;
    int64_t lastShortfall = 0;
    int     lastLead = 0;
    int64_t lastTriggerStart = 0;

};

} // namespace cba
