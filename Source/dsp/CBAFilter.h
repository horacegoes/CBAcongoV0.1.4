#pragma once
// =============================================================================
// CBAFilter.h  -  aggressive analog ladder + Akai-style digital filter, switchable
//   (self-oscillation from 97% starts on its own, from a tiny circuit-noise floor)
//
//  Engine::Moog  4-pole transistor-ladder model.
//                - zero-delay feedback: resonance sits exactly at the cutoff
//                - tanh saturation at the input pair and inside every pole
//                - resonance runs past self-oscillation (k up to 4.6)
//                - Bass Keep (LP modes): feeds the input in harder as resonance rises so
//                  the passband stays at full level (authentic Moog thins out; at 1 this
//                  doesn't - and resonance drives the ladder harder, like pushing a real one)
//                - soft-saturating output stage: hot, but never runs away
//                - pole mixing gives LP24/18/12, BP, HP, Notch from the same ladder
//                - "Analog": per-pole component mismatch + thermal drift, per side
//                  (at 100%: +-5% per stage fixed, +-2.5% wandering - clearly audible)
//  Engine::Akai  S2000-style clean digital 2-pole resonant filter (LP/BP/HP/Notch),
//                drive into hard digital clipping. LP18/LP24 fall back to LP12.
//                (The S2000's filter runs in its voice chip, so this is a voicing,
//                 not a ROM-derived model.)
//
//  2x or 4x oversampling with linear-phase halfband FIRs (reports latency).
//  Final ceiling: untouched below -2 dBFS, soft knee to a hard 0 dBFS (before Output gain).
//  Header-only C++17, no JUCE dependency, no allocation after prepare().
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cba
{
namespace fdsp
{
    constexpr double pi = 3.14159265358979323846;

    // smooth tanh approximation, exact +-1 beyond |x| = 3 with zero slope there
    inline float sat (float x) noexcept
    {
        if (x >  3.0f) return  1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // transparent below 0.8, then a soft knee into a hard ceiling of 1.0
    inline float ceiling (float x) noexcept
    {
        const float a = std::fabs (x);
        if (a <= 0.8f) return x;
        const float y = 0.8f + 0.2f * sat ((a - 0.8f) * 5.0f);
        return x < 0.0f ? -y : y;
    }

    inline double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    }

    // linear-phase halfband lowpass (cutoff = quarter of the oversampled rate)
    template <int L>
    struct Halfband
    {
        static_assert (L % 4 == 3, "L must be 4k+3 so the centre tap is odd");
        std::array<float, L> h {};
        void design (double beta)
        {
            const int c = L / 2;
            double sum = 0.0;
            for (int n = 0; n < L; ++n)
            {
                const int m = n - c;
                const double ideal = m == 0 ? 0.5 : std::sin (pi * m / 2.0) / (pi * m);
                const double r = 2.0 * n / (L - 1) - 1.0;
                const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
                h[(size_t) n] = (float) (ideal * w);
                sum += ideal * w;
            }
            for (auto& v : h) v = (float) (v / sum);
        }
    };

    template <int L>
    struct Up2
    {
        static constexpr int N = 64;                     // history >= L/2 + 1, power of 2
        std::array<float, N> x {};
        int pos = 0;
        void reset() { x.fill (0.0f); pos = 0; }
        void process (float in, float& o0, float& o1, const Halfband<L>& f) noexcept
        {
            pos = (pos + N - 1) & (N - 1);
            x[(size_t) pos] = in;
            float a = 0.0f, b = 0.0f;
            for (int j = 0; 2 * j < L; ++j)
            {
                const float v = x[(size_t) ((pos + j) & (N - 1))];
                a += f.h[(size_t) (2 * j)] * v;
                if (2 * j + 1 < L) b += f.h[(size_t) (2 * j + 1)] * v;
            }
            o0 = 2.0f * a;  o1 = 2.0f * b;
        }
    };

    template <int L>
    struct Down2
    {
        static constexpr int N = 128;
        std::array<float, N> v {};
        int pos = 0;
        void reset() { v.fill (0.0f); pos = 0; }
        float process (float v0, float v1, const Halfband<L>& f) noexcept
        {
            pos = (pos + N - 1) & (N - 1);  v[(size_t) pos] = v0;
            pos = (pos + N - 1) & (N - 1);  v[(size_t) pos] = v1;
            float acc = 0.0f;
            for (int t = 0; t < L; ++t) acc += f.h[(size_t) t] * v[(size_t) ((pos + t) & (N - 1))];
            return acc;
        }
    };

    struct Rng
    {
        uint32_t s = 0x12345678u;
        float next() noexcept { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; }
    };
} // namespace fdsp

class CBAFilter
{
public:
    static constexpr int maxChannels = 8;

    enum class Engine { Moog, Akai };
    enum class Mode   { LP24, LP18, LP12, BP, HP, Notch };

    struct Params
    {
        Engine engine    = Engine::Moog;
        Mode   mode      = Mode::LP24;
        float  cutoffHz  = 1000.0f;   // 20 .. 20000
        float  resonance = 0.0f;      // 0..1  (Moog starts self-oscillating at 0.97)
        float  driveDb   = 0.0f;      // 0..36
        float  bassKeep  = 1.0f;      // Moog: 0 = authentic bass loss, 1 = full bass kept
        float  analog    = 0.5f;      // Moog: component mismatch + drift amount 0..1
        float  outputDb  = 0.0f;
    };

    // oversample: 1, 2 or 4 (fixed until the next prepare)
    void prepare (double sampleRate, int numChannels, int oversample = 2)
    {
        fs = sampleRate;
        chans = std::clamp (numChannels, 1, maxChannels);
        os = oversample >= 4 ? 4 : (oversample >= 2 ? 2 : 1);
        fsOS = fs * os;
        hb1.design (8.0);
        hb2.design (7.0);
        smoothCoef = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));   // ~4 ms glide
        driftStep  = (int) (0.25 * fs);
        fdsp::Rng r;  r.s = 0xC0FFEEu;
        for (int c = 0; c < maxChannels; ++c)
            for (int i = 0; i < 4; ++i) mismatch[c][i] = r.next();
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < maxChannels; ++c)
        {
            moog[c] = {};  akai[c] = {};
            up1[c].reset(); up2[c].reset();
            dn1[c].reset(); dn2[c].reset();
        }
        xfade = 0;
        first = true;
        driftCount = 0;
        for (int c = 0; c < maxChannels; ++c)
            for (int i = 0; i < 4; ++i) drift[c][i] = driftTarget[c][i] = driftInc[c][i] = 0.0f;
    }

    void setParams (const Params& p) noexcept
    {
        if (! first && p.engine != target.engine)
        {
            fadeFrom = cur.engine;                       // crossfade engines, no click
            xfade = xfadeLen = std::max (1, (int) (0.015 * fs));
            for (int c = 0; c < maxChannels; ++c)
            {
                if (p.engine == Engine::Moog) moog[c] = {};
                else                          akai[c] = {};
            }
        }
        target = p;
        if (first) { cur = p;  logCut = std::log2 (std::clamp (p.cutoffHz, 20.0f, 20000.0f));
                     res = p.resonance;  drive = dbToGain (p.driveDb);  first = false; }
        cur.engine = p.engine;  cur.mode = p.mode;
    }

    // latency in base-rate samples (report to the host for delay compensation)
    int getLatencySamples() const noexcept
    {
        if (os == 1) return 0;
        const double l1 = (double) (L1 / 2);              // stage 1: (c + c) at 2x  = c base samples
        const double l2 = os == 4 ? (double) (L2 / 2) / 2.0 : 0.0;
        return (int) std::lround (l1 + l2);
    }

    // io: numChannels pointers, processed in place. cutoffModOct: optional per-sample
    // cutoff offset in octaves (for LFO / envelope / sequencer), may be nullptr.
    void process (float* const* io, int numChannels, int numSamples,
                  const float* cutoffModOct = nullptr) noexcept
    {
        const int nc = std::min (numChannels, chans);
        const float tLog   = std::log2 (std::clamp (target.cutoffHz, 20.0f, 20000.0f));
        const float tRes   = std::clamp (target.resonance, 0.0f, 1.0f);
        const float tDrive = dbToGain (std::clamp (target.driveDb, 0.0f, 36.0f));
        const float outG   = dbToGain (std::clamp (target.outputDb, -36.0f, 24.0f));

        for (int n = 0; n < numSamples; ++n)
        {
            logCut += (tLog - logCut) * smoothCoef;
            res    += (tRes - res) * smoothCoef;
            drive  += (tDrive - drive) * smoothCoef;
            stepDrift();

            float lc = logCut + (cutoffModOct ? cutoffModOct[n] : 0.0f);
            const double maxHz = std::min (20000.0, 0.42 * fsOS);
            const double fc = std::clamp (std::exp2 ((double) lc), 16.0, maxHz);

            const bool fading = xfade > 0;
            const bool runMoog = cur.engine == Engine::Moog || (fading && fadeFrom == Engine::Moog);
            const bool runAkai = cur.engine == Engine::Akai || (fading && fadeFrom == Engine::Akai);
            const float tNew = fading ? 1.0f - (float) xfade / (float) xfadeLen : 1.0f;

            for (int c = 0; c < nc; ++c)
            {
                MoogCoefs mc {};  AkaiCoefs ac {};
                if (runMoog) mc = moogCoefs (fc, c);
                if (runAkai) ac = akaiCoefs (fc);

                float buf[4];
                float x = io[c][n];
                // circuit noise (about -100 dBFS), only once the ladder is past its oscillation
                // threshold: like a real Moog, it then starts singing even with no input at all
                if (runMoog && res > 0.965f)
                    x += 1.0e-5f * std::min (1.0f, (res - 0.965f) / 0.005f) * noiseRng.next();
                upsample (c, x, buf);

                for (int k = 0; k < os; ++k)
                {
                    float yM = 0.0f, yA = 0.0f;
                    if (runMoog) yM = moogTick (moog[c], mc, buf[k]);
                    if (runAkai) yA = akaiTick (akai[c], ac, buf[k]);
                    const float yNew = cur.engine == Engine::Moog ? yM : yA;
                    const float yOld = cur.engine == Engine::Moog ? yA : yM;
                    buf[k] = fading ? yOld + tNew * (yNew - yOld) : yNew;
                }
                io[c][n] = fdsp::ceiling (downsample (c, buf)) * outG;
            }
            if (xfade > 0) --xfade;
        }
    }

private:
    static constexpr int L1 = 63, L2 = 31;

    struct MoogState { float s[4] {}; float h = 0.0f; };
    struct AkaiState { float s1 = 0.0f, s2 = 0.0f; };
    struct MoogCoefs { float G[4]; float Gh, k, bk, drive; };
    struct AkaiCoefs { float g, R, drive; };

    static float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    // ---- Moog ladder ------------------------------------------------------------
    MoogCoefs moogCoefs (double fc, int c) const noexcept
    {
        MoogCoefs m;
        // resonance knob -> feedback: just below the threshold at 96% (rings and dies away),
        // over it from 97% with a clear, solid sine, and on to a strong one at 100%
        m.k = res <= 0.96f ? 3.95f * res / 0.96f
            : (res <= 0.97f ? 3.95f + (res - 0.96f) * (0.17f / 0.01f)
                            : 4.12f + (res - 0.97f) * (0.48f / 0.03f));
        // past k = 4 the saturated loop runs flat (measured -50 cents at full); pre-tune it
        const double over = std::max (0.0, (double) m.k - 4.0);
        const double tune = std::exp2 (77.3 * std::pow (over, 0.85) / 1200.0);
        const float a = std::clamp (target.analog, 0.0f, 1.0f);
        for (int i = 0; i < 4; ++i)
        {
            // ANALOG at 100%: each stage off by up to +-5% (about a semitone across the range), and
            // wandering by up to +-2.5% (about +-40 cents), left and right independently
            const double f = fc * tune * (1.0 + a * (0.05 * mismatch[c][i] + 0.025 * drift[c][i]));
            const double g = std::tan (fdsp::pi * std::min (f, 0.45 * fsOS) / fsOS);
            m.G[i] = (float) (g / (1.0 + g));
        }
        m.Gh = 0.0f;
        m.bk = std::clamp (target.bassKeep, 0.0f, 1.0f);
        m.drive = drive;
        return m;
    }

    float moogTick (MoogState& st, const MoogCoefs& m, float in) const noexcept
    {
        // Bass Keep: DC gain of the ladder is 1/(1+k); feeding the input in at (1+k)
        // restores the passband exactly and leaves the resonance peak on top of it
        const bool  lpMode = cur.mode == Mode::LP24 || cur.mode == Mode::LP18 || cur.mode == Mode::LP12;
        const float x = in * m.drive * (lpMode ? 1.0f + m.k * m.bk : 1.0f);

        // zero-delay feedback: solve the linearised loop for the ladder input u
        float A = 1.0f, B = 0.0f;
        for (int i = 0; i < 4; ++i) { A = m.G[i] * A;  B = m.G[i] * B + (1.0f - m.G[i]) * st.s[i]; }
        const float u = (x - m.k * B) / (1.0f + m.k * A);

        // run the ladder with saturation at every transistor pair
        float y[5];
        y[0] = fdsp::sat (u);
        float xi = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = m.G[i] * (fdsp::sat (xi) - fdsp::sat (st.s[i]));
            const float yi = v + st.s[i];
            st.s[i] = yi + v;
            y[i + 1] = yi;  xi = yi;
        }

        float out = y[4];
        switch (cur.mode)
        {
            case Mode::LP24:  out = y[4]; break;
            case Mode::LP18:  out = y[3]; break;
            case Mode::LP12:  out = y[2]; break;
            case Mode::BP:    out = 4.0f * (y[2] - 2.0f * y[3] + y[4]); break;
            case Mode::HP:    out = y[0] - 4.0f * y[1] + 6.0f * y[2] - 4.0f * y[3] + y[4]; break;
            case Mode::Notch: out = y[0] - 2.0f * y[1] + 2.0f * y[2]; break;
        }
        return fdsp::sat (out);                              // output stage: soft ceiling at 0 dBFS
    }

    // ---- Akai-style digital SVF -------------------------------------------------
    AkaiCoefs akaiCoefs (double fc) const noexcept
    {
        AkaiCoefs a;
        a.g = (float) std::tan (fdsp::pi * std::min (fc, 0.45 * fsOS) / fsOS);
        const double Q = 0.7071 * std::pow (42.0, (double) res);    // 0.707 .. 30
        a.R = (float) (0.5 / Q);
        a.drive = drive;
        return a;
    }

    float akaiTick (AkaiState& st, const AkaiCoefs& a, float in) const noexcept
    {
        const float x  = in * a.drive;
        const float hp = (x - (2.0f * a.R + a.g) * st.s1 - st.s2) / (1.0f + 2.0f * a.R * a.g + a.g * a.g);
        const float bp = a.g * hp + st.s1;   st.s1 = a.g * hp + bp;
        const float lp = a.g * bp + st.s2;   st.s2 = a.g * bp + lp;
        float y;
        switch (cur.mode)
        {
            case Mode::BP:    y = 2.0f * a.R * bp; break;          // unity at the peak
            case Mode::HP:    y = hp; break;
            case Mode::Notch: y = x - 2.0f * a.R * bp; break;
            default:          y = lp; break;                       // LP24/18/12 -> 2-pole
        }
        return std::clamp (y, -1.0f, 1.0f);                        // digital clipping
    }

    // ---- oversampling -----------------------------------------------------------
    void upsample (int c, float x, float* out) noexcept
    {
        if (os == 1) { out[0] = x; return; }
        float a, b;
        up1[c].process (x, a, b, hb1);
        if (os == 2) { out[0] = a;  out[1] = b;  return; }
        up2[c].process (a, out[0], out[1], hb2);          // one continuous 2x stream
        up2[c].process (b, out[2], out[3], hb2);
    }

    float downsample (int c, const float* in) noexcept
    {
        if (os == 1) return in[0];
        if (os == 2) return dn1[c].process (in[0], in[1], hb1);
        const float a = dn2[c].process (in[0], in[1], hb2);
        const float b = dn2[c].process (in[2], in[3], hb2);
        return dn1[c].process (a, b, hb1);
    }

    // ---- thermal drift: gliding random targets, per stage and per side ------------------
    void stepDrift() noexcept
    {
        if (--driftCount <= 0)
        {
            driftCount = driftStep;
            for (int c = 0; c < maxChannels; ++c)
                for (int i = 0; i < 4; ++i)
                {
                    driftTarget[c][i] = rng.next();
                    driftInc[c][i] = (driftTarget[c][i] - drift[c][i]) / (float) driftStep;
                }
        }
        for (int c = 0; c < maxChannels; ++c)
            for (int i = 0; i < 4; ++i) drift[c][i] += driftInc[c][i];
    }

    double fs = 44100.0, fsOS = 88200.0;
    int    chans = 2, os = 2;
    fdsp::Halfband<L1> hb1;
    fdsp::Halfband<L2> hb2;
    fdsp::Up2<L1>   up1[maxChannels];
    fdsp::Up2<L2>   up2[maxChannels];
    fdsp::Down2<L1> dn1[maxChannels];
    fdsp::Down2<L2> dn2[maxChannels];

    MoogState moog[maxChannels];
    AkaiState akai[maxChannels];
    float     mismatch[maxChannels][4] {};

    Params target, cur;
    float  logCut = 10.0f, res = 0.0f, drive = 1.0f, smoothCoef = 0.01f;
    bool   first = true;
    Engine fadeFrom = Engine::Moog;
    int    xfade = 0, xfadeLen = 1;

    fdsp::Rng rng, noiseRng;
    float drift[maxChannels][4] {}, driftTarget[maxChannels][4] {}, driftInc[maxChannels][4] {};
    int driftCount = 0, driftStep = 16000;
};

} // namespace cba

/* =============================================================================
   JUCE glue

   // prepareToPlay:
   filter.prepare (sampleRate, getTotalNumOutputChannels(), 2);   // 2x oversampling
   setLatencySamples (filter.getLatencySamples());

   // processBlock:
   cba::CBAFilter::Params p;
   p.engine    = engineParam->load() > 0.5f ? cba::CBAFilter::Engine::Akai
                                            : cba::CBAFilter::Engine::Moog;   // the switch
   p.mode      = (cba::CBAFilter::Mode) (int) modeParam->load();
   p.cutoffHz  = cutoffParam->load();
   p.resonance = resParam->load();
   p.driveDb   = driveParam->load();
   p.bassKeep  = bassKeepParam->load();
   p.analog    = analogParam->load();
   p.outputDb  = outParam->load();
   filter.setParams (p);
   filter.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                   buffer.getNumSamples());
   ========================================================================== */
