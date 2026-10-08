#pragma once
// =============================================================================
// SamplerFrontEnd.h  -  sampler "converter" colour: sample rate + bit depth
//
//  Input side : 6-pole Butterworth anti-alias low-pass at the "audio bandwidth": the S950
//               ROM sets bandwidth 3000..19200 Hz = 0.4 x sample rate (its MF6 6th-order
//               Butterworth tracks the clock); the S2000 model uses 0.45 x rate. Then
//               sample-and-hold at the chosen rate and truncation to N bits (SAR ADCs truncate).
//  Output side: 6-pole Butterworth reconstruction low-pass at bandwidth x pitch ratio
//               (a sampler's DAC runs faster when a sample is played higher).
//  The filters switch out when the rate is at or above the host rate, so 44.1k on a
//  44.1k host is transparent apart from the bit depth.
// Header-only C++17, no allocation, real-time safe.
// =============================================================================

#include <algorithm>
#include <cmath>

namespace cba
{
namespace fe
{
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] {}, z2[2] {};
        void setLowpass (double fc, double fs, double Q) noexcept
        {
            const double w0 = 2.0 * 3.14159265358979323846 * std::clamp (fc, 10.0, 0.49 * fs) / fs;
            const double cw = std::cos (w0), al = std::sin (w0) / (2.0 * Q), a0 = 1.0 + al;
            b0 = (float) ((1.0 - cw) * 0.5 / a0);  b1 = (float) ((1.0 - cw) / a0);  b2 = b0;
            a1 = (float) (-2.0 * cw / a0);         a2 = (float) ((1.0 - al) / a0);
        }
        float tick (float x, int c) noexcept       // transposed direct form II
        {
            const float y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
    };

    struct Butter6
    {
        Biquad s[3];
        double lastFc = -1.0;
        void set (double fc, double fs) noexcept
        {
            if (std::abs (fc - lastFc) < 0.5) return;
            lastFc = fc;
            static constexpr double Q[3] = { 0.51763809, 0.70710678, 1.93185165 };
            for (int k = 0; k < 3; ++k) s[k].setLowpass (fc, fs, Q[k]);
        }
        float tick (float x, int c) noexcept { return s[2].tick (s[1].tick (s[0].tick (x, c), c), c); }
        void reset() noexcept { for (auto& b : s) b.reset();  lastFc = -1.0; }
    };
} // namespace fe

class SamplerFrontEnd
{
public:
    void prepare (double sampleRate) noexcept { fs = sampleRate;  reset(); }

    void reset() noexcept
    {
        aa.reset();  rc.reset();
        acc = 1.0;  held[0] = held[1] = 0.0f;
    }

    // rateHz: sampler rate (7500..48000), bits: 1..16, pitchRatio: playback speed
    // rateHz: sampler rate (7500..48000), bits: 1..16, pitchRatio: playback speed,
    // bandwidthRatio: anti-alias corner / rate (S950 0.4, S2000 0.45)
    void set (double rateHz, int bitDepth, double pitchRatio, double bandwidthRatio = 0.45) noexcept
    {
        rate = std::clamp (rateHz, 1000.0, 192000.0);
        bits = std::clamp (bitDepth, 1, 16);
        const double bw = std::clamp (bandwidthRatio, 0.2, 0.49);
        reduce = rate < fs * 0.98;
        if (reduce) aa.set (bw * rate, fs);
        const double rcFc = bw * rate * std::max (0.05, pitchRatio);
        reconstruct = rcFc < fs * 0.44;
        if (reconstruct) rc.set (rcFc, fs);
        q = (float) (1 << (bits - 1));
    }

    // input side, one frame in place
    void encode (float* frame, int numChannels) noexcept
    {
        const int nc = std::min (numChannels, 2);
        if (reduce)
        {
            for (int c = 0; c < nc; ++c) frame[c] = aa.tick (frame[c], c);
            acc += rate / fs;
            if (acc >= 1.0)
            {
                acc -= std::floor (acc);
                for (int c = 0; c < nc; ++c) held[c] = frame[c];
            }
            for (int c = 0; c < nc; ++c) frame[c] = held[c];
        }
        for (int c = 0; c < nc; ++c)                    // truncate toward zero: converter grit, no DC offset
            frame[c] = std::trunc (std::clamp (frame[c], -1.0f, 0.99999f) * q) / q;
    }

    // output side, one frame in place
    void decode (float* frame, int numChannels) noexcept
    {
        if (! reconstruct) return;
        const int nc = std::min (numChannels, 2);
        for (int c = 0; c < nc; ++c) frame[c] = rc.tick (frame[c], c);
    }

private:
    double fs = 44100.0, rate = 44100.0, acc = 1.0;
    int    bits = 16;
    float  q = 32768.0f, held[2] {};
    bool   reduce = false, reconstruct = false;
    fe::Butter6 aa, rc;
};

} // namespace cba
