#pragma once
// =============================================================================
// EB16.h  -  the Effects Board (Akai EB16): FX1, FX2 (multi-effects) + RV1, RV2 (reverbs)
// (FX1 / FX2 run DIST/EQ -> MOD -> ECHO; the reverbs are RV1 and RV2, placed with ORDER)
//
// One signal runs in series through the four units in the chosen ORDER; each unit has ON
// (click-free) and WET/DRY. A unit that is off costs nothing.
//
// Structure, controls, units and routing follow the S2000 ROM's EB16 pages (see
// EB16Spec.h). Hardware traits from the EB16 schematic: delay / reverb memory is 24-bit
// (three 128K x 8 SRAMs on a 24-bit bus) - every memory write here is quantised to a
// 24-bit word with 6 dB of headroom and saturates like fixed-point memory; delay lengths
// stay within the board's ranges (3-digit ms). The DFX chip's own microcode is not in the
// S2000 ROM's readable data, so the algorithms are EB16-era designs built to the ROM's
// parameter set: a parallel 4-band filter bank with F-MOD, allpass phaser / modulated
// flanger and chorus, a horn-and-drum rotary, FM vibrato + auto pan, a two-window pitch
// shifter, a damped multi-mode echo and diffused 8-line feedback-delay-network reverbs
// with dedicated gated and reverse algorithms.
// Header-only C++17, no JUCE, no allocation after prepare().
// =============================================================================

#include "EB16Spec.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cba
{
namespace eb16
{
    constexpr float kTwoPi = 6.283185307179586f;
    constexpr float kPi    = 3.141592653589793f;

    // 24-bit memory word, +-2.0 full scale (6 dB headroom), saturating
    inline float q24 (float x) noexcept
    {
        x = std::clamp (x, -2.0f, 1.9999998f);
        const float s = x * 4194304.0f;                         // 2^22 steps per unit
        return (float) (int32_t) (s + (s >= 0.0f ? 0.5f : -0.5f)) / 4194304.0f;
    }

    struct Delay
    {
        std::vector<float> b;
        int mask = 0, w = 0;
        void prepare (int maxSamples)
        {
            int n = 1;  while (n < maxSamples + 8) n <<= 1;
            b.assign ((size_t) n, 0.0f);  mask = n - 1;  w = 0;
        }
        void reset() noexcept { std::fill (b.begin(), b.end(), 0.0f); }
        void push (float x) noexcept { w = (w + 1) & mask;  b[(size_t) w] = q24 (x); }
        float tap (int d) const noexcept { return b[(size_t) ((w - d) & mask)]; }
        float tapf (float d) const noexcept
        {
            d = std::max (0.0f, d);
            const int i = (int) d;  const float f = d - (float) i;
            const float a = tap (i), c = tap (i + 1);
            return a + (c - a) * f;
        }
        int size() const noexcept { return mask + 1; }
    };

    struct Svf                                                  // TPT state variable filter
    {
        float g = 0.1f, k = 1.4f, a1 = 0, a2 = 0, a3 = 0, s1 = 0, s2 = 0;
        void set (float f, float q, float fs) noexcept
        {
            g = std::tan (kPi * std::clamp (f, 10.0f, 0.45f * fs) / fs);
            k = 1.0f / std::max (0.1f, q);
            a1 = 1.0f / (1.0f + g * (g + k));  a2 = g * a1;  a3 = g * a2;
        }
        void tick (float x, float& lp, float& bp, float& hp) noexcept
        {
            const float v3 = x - s2;
            const float v1 = a1 * s1 + a2 * v3;
            const float v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;  s2 = 2.0f * v2 - s2;
            lp = v2;  bp = v1;  hp = x - k * v1 - v2;
        }
        void reset() noexcept { s1 = s2 = 0.0f; }
    };

    struct OnePole
    {
        float a = 0.0f, y = 0.0f;
        void setLP (float f, float fs) noexcept { a = std::exp (-kTwoPi * std::clamp (f, 5.0f, 0.49f * fs) / fs); }
        float lp (float x) noexcept { y = x + a * (y - x);  return y; }
        float hp (float x) noexcept { return x - lp (x); }
        void reset() noexcept { y = 0.0f; }
    };

    inline float panL (float p) noexcept { return std::cos ((p + 50.0f) / 100.0f * kPi * 0.5f); }   // -50..50
    inline float panR (float p) noexcept { return std::sin ((p + 50.0f) / 100.0f * kPi * 0.5f); }

    // ============================================================================
    // RINGMOD/DISTORT + 4-BAND EQUALISE (parallel LP + BP1 + BP2 + HP, F-MOD on BP1/BP2)
    // ============================================================================
    struct DistEq
    {
        OnePole lp[2], hp[2];                                 // complementary 1st order: LP + HP = flat
        Svf bp1[2], bp2[2];
        float rmPhase = 0, lfo1 = 0, lfo2 = 0, fs = 44100;
        float cDepth = -1, cAtten = -1, drive = 1, att = 1, cDb[4] { -99, -99, -99, -99 }, gDb[4] {};
        int   ctr = 0;
        void prepare (float sampleRate) { fs = sampleRate;  reset(); }
        void reset() noexcept
        {
            for (int c = 0; c < 2; ++c) { lp[c].reset(); bp1[c].reset(); bp2[c].reset(); hp[c].reset(); }
            rmPhase = lfo1 = lfo2 = 0;  ctr = 0;
        }
        static float widthQ (float wid) noexcept { return 0.5f * std::exp2 ((99.0f - wid) / 99.0f * 4.0f); }

        void process (const float* v, float& L, float& R) noexcept
        {
            // ring modulator + distortion
            if (v[DistOn] > 0.5f)
            {
                const float rmD = v[RmDepth] / 99.0f;
                if (rmD > 0.0f)
                {
                    rmPhase += rmHz (v[RmFreq]) / fs;  rmPhase -= std::floor (rmPhase);
                    const float c = std::sin (kTwoPi * rmPhase);
                    L = L * (1.0f - rmD) + L * c * rmD;  R = R * (1.0f - rmD) + R * c * rmD;
                }
                if (v[DistDepth] != cDepth || v[DistAtten] != cAtten)
                {
                    cDepth = v[DistDepth];  cAtten = v[DistAtten];
                    drive = std::pow (10.0f, cDepth * 0.4f / 20.0f);                    // up to +40 dB
                    att   = std::pow (10.0f, -cAtten * 0.4f / 20.0f);                   // down to -40 dB
                }
                if (v[DistDepth] > 0.0f)
                {
                    L = std::clamp (L * drive, -1.0f, 1.0f);  R = std::clamp (R * drive, -1.0f, 1.0f);   // digital clip
                }
                L *= att;  R *= att;
            }
            if (v[EqOn] > 0.5f)
            {
                if (ctr-- <= 0)                                                          // F-MOD, every 16 samples
                {
                    ctr = 15;
                    lfo1 += 16.0f * v[Bp1Rate] / fs;  lfo1 -= std::floor (lfo1);
                    lfo2 += 16.0f * v[Bp2Rate] / fs;  lfo2 -= std::floor (lfo2);
                    const float m1 = std::exp2 (2.0f * v[Bp1Depth] / 99.0f * std::sin (kTwoPi * lfo1));
                    const float m2 = std::exp2 (2.0f * v[Bp2Depth] / 99.0f * std::sin (kTwoPi * lfo2));
                    for (int c = 0; c < 2; ++c)
                    {
                        lp[c].setLP (isoHz (v[LowHz]), fs);
                        hp[c].setLP (isoHz (v[HighHz]), fs);
                        bp1[c].set (isoHz (v[Bp1Hz]) * m1, widthQ (v[Bp1Wid]), fs);
                        bp2[c].set (isoHz (v[Bp2Hz]) * m2, widthQ (v[Bp2Wid]), fs);
                    }
                }
                if (v[LowDb] != cDb[0] || v[Bp1Db] != cDb[1] || v[Bp2Db] != cDb[2] || v[HighDb] != cDb[3])
                {
                    cDb[0] = v[LowDb];  cDb[1] = v[Bp1Db];  cDb[2] = v[Bp2Db];  cDb[3] = v[HighDb];
                    for (int k = 0; k < 4; ++k) gDb[k] = db37 (cDb[k]);
                }
                const float gL = gDb[0], g1 = gDb[1], g2 = gDb[2], gH = gDb[3];
                float* io[2] = { &L, &R };
                for (int c = 0; c < 2; ++c)
                {
                    const float x = *io[c];
                    float l, b, h, y = 0.0f;
                    y += gL * lp[c].lp (x);
                    bp1[c].tick (x, l, b, h);  y += g1 * b * bp1[c].k;                   // unity-peak band-pass
                    bp2[c].tick (x, l, b, h);  y += g2 * b * bp2[c].k;
                    y += gH * hp[c].hp (x);
                    *io[c] = y;
                }
            }
        }
    };

    // ============================================================================
    // MOD. FUNCTION
    // ============================================================================
    struct Mod
    {
        float fs = 44100;
        // phaser
        float apX[2][6] {}, apY[2][6] {}, phFb[2] {};
        // flanger / chorus / FM vibrato
        Delay dly[2];
        float fbMem[2] {};
        // rotary
        Delay horn[2];
        OnePole xover;
        float hornPh = 0, drumPh = 0, speedH = 0, speedD = 0;
        bool  rotInit = false;
        // pitch shift
        Delay ps[2];
        float psPh[2] {}, psOut[2] {};
        // shared LFOs
        float lfo = 0, panLfo = 0;

        void prepare (float sampleRate)
        {
            fs = sampleRate;
            for (int c = 0; c < 2; ++c)
            {
                dly[c].prepare ((int) (0.05f * fs));
                horn[c].prepare ((int) (0.01f * fs));
                ps[c].prepare ((int) (0.6f * fs));
            }
            xover.setLP (800.0f, fs);
            reset();
        }
        void reset() noexcept
        {
            for (int c = 0; c < 2; ++c)
            {
                dly[c].reset();  horn[c].reset();  ps[c].reset();
                fbMem[c] = phFb[c] = psOut[c] = 0;  psPh[c] = 0;
                for (int s = 0; s < 6; ++s) apX[c][s] = apY[c][s] = 0;
            }
            xover.reset();  hornPh = drumPh = lfo = panLfo = 0;  rotInit = false;
        }

        // returns the wet signal in (L, R)
        void process (const float* v, float& L, float& R) noexcept
        {
            const int type = (int) std::lround (v[ModType]);
            if (type != Rotary) rotInit = false;
            switch (type)
            {
                case Rotary:     rotary (v, L, R); break;
                case FmodApan:   fmApan (v, L, R); break;
                case PitchShift: pitch (v, L, R, false); break;
                case PitchFbk:   pitch (v, L, R, true); break;
                default:         pfc (v, L, R); break;
            }
        }

        void pfc (const float* v, float& L, float& R) noexcept
        {
            lfo += v[ModRate] / fs;  lfo -= std::floor (lfo);
            const float depth = v[ModDepth] / 99.0f, fb = v[ModFbk] / 99.0f;
            const int sub = (int) std::lround (v[PfcType]);
            float in[2] = { L, R }, out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float s = std::sin (kTwoPi * (lfo + 0.25f * (float) c));        // R 90 degrees on
                if (sub == Phase)
                {
                    const float f = 250.0f * std::exp2 (3.2f * depth * (0.5f + 0.5f * s));
                    const float t = std::tan (kPi * std::min (f, 0.45f * fs) / fs);
                    const float a = (t - 1.0f) / (t + 1.0f);
                    float x = in[c] + 0.85f * fb * phFb[c];
                    for (int st = 0; st < 6; ++st)
                    {
                        const float y = a * x + apX[c][st] - a * apY[c][st];
                        apX[c][st] = x;  apY[c][st] = y;  x = y;
                    }
                    phFb[c] = x;  out[c] = x;
                }
                else if (sub == Flange)
                {
                    const float d = (0.0005f + 0.006f * depth * (0.5f + 0.5f * s)) * fs;
                    dly[c].push (in[c] + 0.95f * fb * fbMem[c]);
                    fbMem[c] = dly[c].tapf (d);  out[c] = fbMem[c];
                }
                else
                {
                    const float s2 = std::sin (kTwoPi * (lfo + 0.25f * (float) c + 0.333f));
                    const float d1 = (0.012f + 0.008f * depth * s) * fs;
                    const float d2 = (0.016f + 0.008f * depth * s2) * fs;
                    dly[c].push (in[c] + 0.5f * fb * fbMem[c]);
                    fbMem[c] = 0.5f * (dly[c].tapf (d1) + dly[c].tapf (d2));
                    out[c] = fbMem[c];
                }
            }
            L = out[0];  R = out[1];
        }

        void rotary (const float* v, float& L, float& R) noexcept
        {
            const float target = v[RotFast] > 0.5f ? v[RotSpeed2] : v[RotSpeed1];
            if (! rotInit) { speedH = speedD = target;  rotInit = true; }
            const float accel = std::max (0.1f, v[RotAccel]);
            speedH += (target - speedH) * (1.0f - std::exp (-1.0f / (accel * 0.5f * fs)));   // horn faster
            speedD += (target - speedD) * (1.0f - std::exp (-1.0f / (accel * fs)));          // drum heavier
            hornPh += speedH / fs;          hornPh -= std::floor (hornPh);
            drumPh += speedD * 0.85f / fs;  drumPh -= std::floor (drumPh);
            const float depth = v[RotDepth] / 99.0f;

            const float m = 0.5f * (L + R);
            const float lo = xover.lp (m), hi = m - lo;
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float hs = std::sin (kTwoPi * (hornPh + 0.5f * (float) c));
                const float ds = std::sin (kTwoPi * (drumPh + 0.5f * (float) c));
                horn[c].push (hi);
                const float doppler = (0.0012f + 0.0008f * depth * hs) * fs;
                const float hornOut = horn[c].tapf (doppler) * (1.0f - 0.45f * depth * (0.5f - 0.5f * hs));
                const float drumOut = lo * (1.0f - 0.25f * depth * (0.5f - 0.5f * ds));
                out[c] = hornOut + drumOut;
            }
            L = out[0];  R = out[1];
        }

        void fmApan (const float* v, float& L, float& R) noexcept
        {
            lfo += v[FmRate] / fs;  lfo -= std::floor (lfo);
            const float depth = v[FmDepth] / 99.0f, fb = v[FmFbk] / 99.0f;
            float in[2] = { L, R }, out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float s = std::sin (kTwoPi * lfo);
                const float d = (0.005f + 0.004f * depth * s) * fs;
                dly[c].push (in[c] + 0.9f * fb * fbMem[c]);
                fbMem[c] = dly[c].tapf (d);  out[c] = fbMem[c];
            }
            panLfo += v[ApanRate] / fs;  panLfo -= std::floor (panLfo);
            const float pd = v[ApanDepth] / 99.0f;
            const int mode = (int) std::lround (v[ApanMode]);
            float gL = 1.0f, gR = 1.0f;
            if (mode == Trem)
            {
                const float t = 1.0f - pd * (0.5f + 0.5f * std::sin (kTwoPi * panLfo));
                gL = gR = t;
            }
            else
            {
                float p;                                            // -1 left .. +1 right
                if (mode == Pan)       p = std::sin (kTwoPi * panLfo);
                else if (mode == LtoR) p = 2.0f * panLfo - 1.0f;
                else                   p = 1.0f - 2.0f * panLfo;
                p *= pd;
                gL = std::cos ((p + 1.0f) * kPi * 0.25f) * 1.41421356f;
                gR = std::sin ((p + 1.0f) * kPi * 0.25f) * 1.41421356f;
            }
            L = out[0] * gL;  R = out[1] * gR;
        }

        void pitch (const float* v, float& L, float& R, bool feedback) noexcept
        {
            const float win = 0.05f * fs;                              // 50 ms window
            float in[2] = { L, R }, out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float semis = (c == 0 || v[TuneLink] > 0.5f) ? v[TuneL] : v[TuneR];   // LINK: R follows L
                const float base  = (c == 0 ? v[DelL] : v[DelR]) * 0.001f * fs;
                const float fb    = feedback ? (c == 0 ? v[FbL] : v[FbR]) / 99.0f * 0.95f : 0.0f;
                const float ratio = std::exp2 (semis / 12.0f);
                ps[c].push (in[c] + fb * psOut[c]);
                psPh[c] += (1.0f - ratio) / win;                       // delay sweeps through the window
                psPh[c] -= std::floor (psPh[c]);
                const float p1 = psPh[c], p2 = p1 + 0.5f - std::floor (p1 + 0.5f);
                const float g1 = std::sin (kPi * p1), g2 = std::sin (kPi * p2);   // sin^2 sums to 1
                const float y = ps[c].tapf (base + p1 * win + 2.0f) * g1 * g1
                              + ps[c].tapf (base + p2 * win + 2.0f) * g2 * g2;
                psOut[c] = y;  out[c] = y;
            }
            L = out[0];  R = out[1];
        }
    };

    // ============================================================================
    // ECHO
    // ============================================================================
    struct Echo
    {
        Delay d[2];
        OnePole damp[2];
        float fs = 44100, cur[2] { 1, 1 }, last[2] {}, cDamp[2] { -1, -1 };
        void prepare (float sampleRate)
        {
            fs = sampleRate;
            for (auto& x : d) x.prepare ((int) (2.05f * fs) + 8);         // synced echoes: up to 2 s
            reset();
        }
        void reset() noexcept
        {
            for (int c = 0; c < 2; ++c) { d[c].reset(); damp[c].reset(); last[c] = 0; cDamp[c] = -1; }
            cur[0] = cur[1] = 0.2f * fs;
        }
        // input (L, R) -> wet echo (L, R)
        void process (const float* v, float& L, float& R) noexcept
        {
            const int mode = (int) std::lround (v[EchoMode]);
            const bool mono = mode == MonoLeft || mode == MonoLR;
            const float tgt[2] = { (mono ? v[EchoDelay] : v[LDelay]) * 0.001f * fs,
                                   (mono ? v[EchoDelay] : v[RDelay]) * 0.001f * fs };
            for (int c = 0; c < 2; ++c) cur[c] += (tgt[c] - cur[c]) * 0.0005f;   // glide, no zipper
            const float fbL = (mono ? v[EchoFbk] : v[LFbk]) / 99.0f, fbR = (mono ? v[EchoFbk] : v[RFbk]) / 99.0f;
            const float dl0 = mono ? v[EchoDamp] : v[LDamp], dl1 = mono ? v[EchoDamp] : v[RDamp];
            if (dl0 != cDamp[0]) { cDamp[0] = dl0;  damp[0].setLP (isoHz (dl0), fs); }
            if (dl1 != cDamp[1]) { cDamp[1] = dl1;  damp[1].setLP (isoHz (dl1), fs); }

            if (mono)
            {
                const float x = mode == MonoLeft ? L : 0.5f * (L + R);
                const float e = d[0].tapf (cur[0]);
                d[0].push (x + fbL * damp[0].lp (e));
                L = e * panL (v[EchoOffs]) * 1.41421356f;  R = e * panR (v[EchoOffs]) * 1.41421356f;
                return;
            }
            const float eL = d[0].tapf (cur[0]), eR = d[1].tapf (cur[1]);
            const float dl = damp[0].lp (eL), dr = damp[1].lp (eR);
            if (mode == XoverLR) { d[0].push (L + fbR * dr);  d[1].push (R + fbL * dl); }   // ping-pong
            else                 { d[0].push (L + fbL * dl);  d[1].push (R + fbR * dr); }
            L = eL;  R = eR;
        }
    };

    // ============================================================================
    // REVERB (halls, rooms, gated, reverse)
    // ============================================================================
    struct Reverb
    {
        static constexpr int N = 8;
        Delay line[N], pre, ap[4], rev;
        OnePole lpf[N], hpf[N];
        float fs = 44100, gain[N] {}, len[N] {}, modPh = 0;
        float gateEnv = 0, gateHold = 0, gateGain = 0;
        float timeSec = -1.0f;                                 // > 0: synced gate / reverse length
        int   lastType = -1, cType = -1;
        float lastSize = -1, cDecay = -1, cLF = -1, cHF = -1;

        static constexpr float baseMs[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.7f, 73.9f };
        static constexpr float apMs[4]   = { 4.7f, 3.6f, 12.7f, 9.3f };

        void prepare (float sampleRate)
        {
            fs = sampleRate;
            for (int i = 0; i < N; ++i) line[i].prepare ((int) (baseMs[i] * 0.001f * 1.7f * fs) + 64);
            for (int i = 0; i < 4; ++i) ap[i].prepare ((int) (apMs[i] * 0.001f * 1.4f * fs) + 8);
            pre.prepare ((int) (0.25f * fs) + 8);
            rev.prepare ((int) (2.05f * fs) + 8);                         // synced reverse: up to 2 s
            reset();
        }
        void reset() noexcept
        {
            for (int i = 0; i < N; ++i) { line[i].reset(); lpf[i].reset(); hpf[i].reset(); }
            for (auto& a : ap) a.reset();
            pre.reset();  rev.reset();
            gateEnv = gateHold = gateGain = 0;  modPh = 0;  lastType = -1;  cType = -1;  cDecay = -1;
        }

        static float sizeOf (int type) noexcept
        {
            switch (type)
            {
                case LargeHall: return 1.6f;   case SmallHall: return 1.0f;
                case LargeRoom: return 0.7f;   case SmallRoom: return 0.42f;
                default:        return 0.8f;
            }
        }

        // pv points at [Predelay, Decay, LF, HF, Diffuse, Near, Time] for this reverb
        void process (int type, const float* pv, float inL, float inR, float& outL, float& outR) noexcept
        {
            const float predelay = pv[0], decay = pv[1], lfIdx = pv[2], hfIdx = pv[3],
                        diffuse = pv[4], nearLvl = pv[5], time = pv[6];
            const float size = sizeOf (type);
            if (type != lastType || size != lastSize)
            {
                for (int i = 0; i < N; ++i) len[i] = baseMs[i] * 0.001f * size * fs;
                lastType = type;  lastSize = size;
            }
            if (decay != cDecay || lfIdx != cLF || hfIdx != cHF || type != cType)
            {
                cDecay = decay;  cLF = lfIdx;  cHF = hfIdx;  cType = type;
                const bool hall = type == LargeHall || type == SmallHall;
                const float rtMax = hall ? 20.0f : (type <= SmallRoom ? 8.0f : 3.0f);
                const float rt = 0.2f * std::pow (rtMax / 0.2f, decay / 99.0f);
                for (int i = 0; i < N; ++i)
                {
                    gain[i] = std::pow (10.0f, -3.0f * len[i] / (rt * fs));
                    lpf[i].setLP (isoHz (hfIdx), fs);
                    hpf[i].setLP (isoHz (lfIdx), fs);
                }
            }

            const float x = 0.5f * (inL + inR);
            pre.push (x);
            const float pd = pre.tapf (predelay * 0.001f * fs);

            if (type == Reverse) { reverse (pd, time, diffuse, outL, outR);  return; }

            // input diffusion
            const float g = 0.72f * diffuse / 99.0f;
            float s = pd;
            for (int i = 0; i < 4; ++i)
            {
                const float dl = apMs[i] * 0.001f * std::sqrt (size) * fs;
                const float z = ap[i].tapf (dl);
                const float y = -g * s + z;
                ap[i].push (s + g * y);
                s = y;
            }

            // 8-line FDN with Hadamard feedback, LF / HF damping in every line
            modPh += 0.37f / fs;  modPh -= std::floor (modPh);
            const float wob = 0.0003f * fs * std::sin (kTwoPi * modPh);
            float o[N];
            for (int i = 0; i < N; ++i)
            {
                const float d = len[i] + (i == 1 ? wob : (i == 6 ? -wob : 0.0f));
                o[i] = line[i].tapf (d);
            }
            float h[N];
            for (int i = 0; i < N; ++i) h[i] = o[i];
            for (int span = 1; span < N; span <<= 1)                 // fast Hadamard
                for (int i = 0; i < N; i += 2 * span)
                    for (int j = i; j < i + span; ++j)
                    {
                        const float a = h[j], b = h[j + span];
                        h[j] = a + b;  h[j + span] = a - b;
                    }
            for (int i = 0; i < N; ++i)
            {
                float y = h[i] * 0.35355339f * gain[i];
                if (hfIdx < 29.5f) y = lpf[i].lp (y);                     // 16k = no HF damping
                if (lfIdx > 0.5f)  y = y - hpf[i].lp (y);                 // 16 Hz = no LF damping
                line[i].push (y + ((i & 1) ? -s : s) * 0.5f);
            }
            float L = 0.0f, R = 0.0f;
            for (int i = 0; i < N; i += 2) { L += o[i] * ((i & 2) ? -1.0f : 1.0f);  R += o[i + 1] * ((i & 2) ? 1.0f : -1.0f); }
            L *= 0.45f;  R *= 0.45f;

            // early reflections ("Near")
            if (nearLvl > 0.0f)
            {
                static constexpr float erMs[8] = { 7.1f, 11.3f, 16.9f, 23.2f, 29.7f, 36.1f, 43.9f, 52.3f };
                const float ng = nearLvl / 99.0f;
                for (int k = 0; k < 8; ++k)
                {
                    const float e = pre.tapf ((predelay + erMs[k] * size) * 0.001f * fs) * ng * (1.0f - 0.09f * (float) k);
                    if (k & 1) R += e; else L += e;
                }
            }

            // gated types: dense reverb cut by a gate that follows the input
            if (type == Gated1 || type == Gated2)
            {
                const float hold = (timeSec > 0.0f ? timeSec : 0.03f + 0.6f * time / 99.0f) * fs;   // gate length
                const float a = std::abs (pd);
                gateEnv = std::max (a, gateEnv * 0.9995f);
                if (gateEnv > 0.003f) gateHold = hold;
                else if (gateHold > 0.0f) gateHold -= 1.0f;
                float target;
                if (type == Gated1) target = gateHold > 0.0f ? 1.0f : 0.0f;           // hard gate
                else                target = gateHold > 0.0f ? gateHold / hold : 0.0f; // decaying gate
                gateGain += (target - gateGain) * (type == Gated1 ? 0.01f : 0.002f);
                L *= gateGain * 1.6f;  R *= gateGain * 1.6f;
            }
            tailImage (L, R, outL, outR);
        }

        // Stereo image of the tails: the EB16 demo's reverb tails measure a left/right
        // correlation of about +0.1..+0.6 (typically +0.3), not fully independent. Sharing
        // a little of each side with the other gives corr = 2a / (1 + a^2) = 0.3 for
        // a = 0.1535, normalised so the level is unchanged.
        static void tailImage (float L, float R, float& outL, float& outR) noexcept
        {
            constexpr float a = 0.1535f, norm = 0.98846f;            // 1 / sqrt (1 + a^2)
            outL = (L + a * R) * norm;
            outR = (R + a * L) * norm;
        }

        // reverse: dense taps whose level rises with delay, then stops
        void reverse (float x, float time, float diffuse, float& outL, float& outR) noexcept
        {
            rev.push (x);
            const float T = std::min ((timeSec > 0.0f ? timeSec : 0.08f + 0.5f * time / 99.0f) * fs,
                                      (float) rev.size() - 8.0f);
            const float spread = 0.4f + 0.6f * diffuse / 99.0f;
            float L = 0.0f, R = 0.0f;
            static constexpr int K = 48;
            for (int k = 1; k <= K; ++k)
            {
                const float u = (float) k / (float) K;                    // 0..1 across the window
                const float jitter = 0.5f + 0.5f * std::sin ((float) k * 12.9898f);
                const float d = T * (u - spread * 0.5f / (float) K * jitter);
                const float g = u * u * ((k & 1) ? 1.0f : -1.0f);
                const float t = rev.tapf (std::max (1.0f, d)) * g;
                if (k % 3 == 0) L += t; else if (k % 3 == 1) R += t; else { L += 0.5f * t; R += 0.5f * t; }
            }
            tailImage (L * 0.18f, R * 0.18f, outL, outR);
        }
    };
    constexpr float Reverb::baseMs[Reverb::N];
    constexpr float Reverb::apMs[4];

    // ============================================================================
    // one multi-effect (FX1 / FX2)
    // ============================================================================
    struct MultiFx
    {
        DistEq de;  Mod mod;  Echo echo;

        void prepare (float fs) { de.prepare (fs);  mod.prepare (fs);  echo.prepare (fs); }
        void reset() noexcept   { de.reset();  mod.reset();  echo.reset(); }

        // DIST/EQ -> MOD -> ECHO, each mixed with its own level / pan (and width)
        void process (const float* v, float inL, float inR, float& outL, float& outR) noexcept
        {
            float aL = inL, aR = inR;
            de.process (v, aL, aR);
            float mL = 0, mR = 0, eL = 0, eR = 0;
            if (v[ModOn] > 0.5f)  { mL = aL;  mR = aR;  mod.process (v, mL, mR); }
            if (v[EchoOn] > 0.5f) { eL = aL + mL;  eR = aR + mR;  echo.process (v, eL, eR); }

            auto stereo = [] (float& l, float& r, float pan, float width, float lvl)
            {
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * (width / 99.0f);
                l = (mid + side) * lvl * panL (pan) * 1.41421356f;
                r = (mid - side) * lvl * panR (pan) * 1.41421356f;
            };
            float dL = aL, dR = aR;
            stereo (dL, dR, v[DistPan], 99.0f, level (v[DistLevel]));
            stereo (mL, mR, v[ModPan], v[ModWidth], level (v[ModLevel]));
            stereo (eL, eR, v[EchoPan], v[EchoWidth], level (v[EchoLevel]));
            outL = dL + mL + eL;
            outR = dR + mR + eR;
        }
    };

    // ============================================================================
    // the board: FX1, FX2, RV1, RV2 in series, in the chosen ORDER
    // ============================================================================
    class Board
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            fx[0].prepare (fs);  fx[1].prepare (fs);
            rv[0].prepare (fs);  rv[1].prepare (fs);
            step = 1.0f / (0.01f * fs);                            // 10 ms on/off and wet/dry glide
            reset();
        }

        void reset() noexcept
        {
            fx[0].reset();  fx[1].reset();  rv[0].reset();  rv[1].reset();
            primed = false;
        }

        // tempo sync: the tempo, and the beat position at the start of the block
        void setTempo (double bpmNow, double beatAtBlockStart) noexcept
        {
            bpm = bpmNow > 1.0 ? bpmNow : 120.0;
            beat = beatAtBlockStart;
        }

        // v: TOTAL parameter values (EB16Spec order). Processes one stereo frame in place.
        void processFrame (const float* v, float& L, float& R) noexcept
        {
            std::copy (v, v + TOTAL, eff);                         // the values in force, synced ones worked out
            applySync();
            beat += bpm / (60.0 * (double) fs);
            run (eff, L, R);
        }

        // read-only views for tests: a value as the effects use it (tempo sync worked out),
        // and where an F-MOD sweep is (0..1) for unit u (0 = FX1, 1 = FX2), band 0 / 1
        float effectiveValue (int index) const noexcept { return eff[(size_t) index]; }
        float fmodPhase (int u, int band) const noexcept { return band == 0 ? fx[u].de.lfo1 : fx[u].de.lfo2; }

    private:
        // turn each synced note value into the time or speed the effects use, at this tempo,
        // and lock the sweeps to the bar
        void applySync() noexcept
        {
            const double secPerQ = 60.0 / bpm;
            auto hz = [&] (const std::vector<Div>& d, float idx) { return (float) (1.0 / (divQuarters (d, idx) * secPerQ)); };
            auto ms = [&] (const std::vector<Div>& d, float idx) { return (float) (divQuarters (d, idx) * secPerQ * 1000.0); };
            auto phase = [&] (const std::vector<Div>& d, float idx) { const double c = beat / divQuarters (d, idx); return (float) (c - std::floor (c)); };
            for (int u = 0; u < 2; ++u)
            {
                float* p = eff + (u == 0 ? FX1 : FX2);
                MultiFx& f = fx[u];
                if (p[EqSync] > 0.5f)
                {
                    p[Bp1Rate] = hz (rateDivs(), p[Bp1Div]);  p[Bp2Rate] = hz (rateDivs(), p[Bp2Div]);
                    f.de.lfo1 = phase (rateDivs(), p[Bp1Div]);  f.de.lfo2 = phase (rateDivs(), p[Bp2Div]);
                }
                if (p[ModSync] > 0.5f)
                {
                    p[ModRate]  = hz (rateDivs(), p[ModDiv]);
                    p[ApanRate] = hz (rateDivs(), p[ApanDiv]);
                    p[FmRate]   = hz (rateDivs(), p[FmDiv]);
                    p[DelL] = std::min (500.0f, ms (delayDivs(), p[DelLDiv]));
                    p[DelR] = std::min (500.0f, ms (delayDivs(), p[DelRDiv]));
                    const int t = (int) std::lround (p[ModType]);
                    f.mod.lfo = phase (rateDivs(), t == FmodApan ? p[FmDiv] : p[ModDiv]);
                    f.mod.panLfo = phase (rateDivs(), p[ApanDiv]);
                }
                if (p[RotSync] > 0.5f)
                {
                    p[RotSpeed1] = hz (rateDivs(), p[Rot1Div]);  p[RotSpeed2] = hz (rateDivs(), p[Rot2Div]);
                }
                if (p[EchoSync] > 0.5f)
                {
                    p[EchoDelay] = std::min (2000.0f, ms (delayDivs(), p[EchoDiv]));
                    p[LDelay]    = std::min (2000.0f, ms (delayDivs(), p[LDiv]));
                    p[RDelay]    = std::min (2000.0f, ms (delayDivs(), p[RDiv]));
                }
            }
            for (int n = 0; n < 2; ++n)
            {
                float* p = eff + (n == 0 ? RV1 : RV2);
                if (p[RvSync] > 0.5f)
                {
                    p[RvPredelay] = std::min (240.0f, ms (delayDivs(), p[RvPreDiv]));
                    rv[n].timeSec = std::min (2.0f, ms (delayDivs(), p[RvTimeDiv]) / 1000.0f);
                }
                else rv[n].timeSec = -1.0f;
            }
        }

        void run (const float* v, float& L, float& R) noexcept
        {
            const int onIdx[4]  = { FX1 + FxOn,  FX2 + FxOn,  RV1 + RvOn,  RV2 + RvOn };
            const int mixIdx[4] = { FX1 + FxMix, FX2 + FxMix, RV1 + RvMix, RV2 + RvMix };
            if (! primed)                                          // start from the settings, no glide
            {
                primed = true;
                for (int u = 0; u < 4; ++u)
                {
                    on[u]  = v[onIdx[u]] > 0.5f ? 1.0f : 0.0f;
                    wet[u] = v[mixIdx[u]] / 100.0f;
                    if (on[u] == 0.0f) reset (u);
                }
            }

            const auto& ord = orders()[(size_t) std::clamp ((int) std::lround (v[GLOB + Order]), 0,
                                                            (int) orders().size() - 1)];
            for (int k = 0; k < 4; ++k)
            {
                const int u = ord[(size_t) k];
                const float onT = v[onIdx[u]] > 0.5f ? 1.0f : 0.0f;
                on[u]  = onT > on[u] ? std::min (onT, on[u] + step) : std::max (onT, on[u] - step);
                const float wetT = v[mixIdx[u]] / 100.0f;
                wet[u] = wetT > wet[u] ? std::min (wetT, wet[u] + step) : std::max (wetT, wet[u] - step);
                if (on[u] <= 0.0f)
                {
                    if (! idle[u]) { reset (u);  idle[u] = true; }   // off: no CPU, clean restart
                    continue;
                }
                idle[u] = false;

                float oL = 0.0f, oR = 0.0f;
                if (u < 2)
                    fx[u].process (v + (u == 0 ? FX1 : FX2), L, R, oL, oR);
                else
                {
                    const float* p = v + (u == 2 ? RV1 : RV2);
                    float rL, rR;
                    rv[u - 2].process ((int) std::lround (p[RvType]), p + RvPredelay, L, R, rL, rR);
                    const float lv = level (p[RvLevel]);
                    oL = rL * lv * panL (p[RvPan]) * 1.41421356f;
                    oR = rR * lv * panR (p[RvPan]) * 1.41421356f;
                }
                const float m = on[u] * wet[u];                       // WET/DRY, faded by ON
                L += m * (oL - L);
                R += m * (oR - R);
            }
        }

        void reset (int u) noexcept
        {
            if (u < 2) fx[u].reset(); else rv[u - 2].reset();
        }

        float   fs = 44100.0f, step = 0.002f;
        double  bpm = 120.0, beat = 0.0;
        float   eff[TOTAL] {};
        MultiFx fx[2];
        Reverb  rv[2];
        float   on[4] {}, wet[4] {};
        bool    idle[4] { true, true, true, true }, primed = false;
    };
} // namespace eb16
} // namespace cba
