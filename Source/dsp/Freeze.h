#pragma once
// =============================================================================
// Freeze.h  -  FREEZE: loop the last SIZE of sound, seamlessly
//
// Always records the stream it sits on. When frozen it loops the most recent SIZE of it;
// changing SIZE while frozen changes the loop length, and so the repeat rate (long sizes
// stutter, tiny ones buzz into a pitch). The loop seam is crossfaded with the audio just
// before the loop (up to 5 ms, never more than a quarter of the loop), and going in and
// out of freeze is a 5 ms crossfade, so nothing clicks.
//   level: the frozen sound's own level (the FREEZE OUTPUT knob) - it touches nothing else.
//   HOLD:  the loop keeps going but the live sound comes back underneath it, so the held
//          loop can be faded in and out with the level.
// Header-only C++17, no JUCE, no allocation after prepare().
// =============================================================================

#include <algorithm>
#include <cmath>
#include <vector>

namespace cba
{
class Freeze
{
public:
    void prepare (double sampleRate, double maxSeconds = 8.0)
    {
        sr = sampleRate;
        int n = 1;
        while (n < (int) (maxSeconds * sr) + 16) n <<= 1;
        buf.assign ((size_t) n * 2, 0.0f);
        mask = n - 1;
        fadeStep = 1.0f / (float) std::max (1.0, 0.005 * sr);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;  frozen = false;  mix = 0.0f;  replace = 1.0f;  pos = 0.0;  filled = 0;
    }

    // size in samples (clamped to what has been recorded and the buffer)
    void setSize (double samples) noexcept { target = std::max (8.0, samples); }
    void setFrozen (bool f) noexcept
    {
        if (f && ! frozen) { end = w;  pos = 0.0; }            // capture: loop ends at "now"
        frozen = f;
    }
    bool isFrozen() const noexcept { return frozen; }
    // the loop length in use right now (what HOLD prints)
    int currentLoopLength() const noexcept
    {
        return (int) std::max (8.0, std::min ({ target, (double) filled - 8.0, (double) mask - 8.0 }));
    }

    // one stereo frame in place
    void process (float& L, float& R, float level = 1.0f, bool hold = false) noexcept
    {
        if (! frozen)
        {
            buf[(size_t) (w & mask) * 2] = L;  buf[(size_t) (w & mask) * 2 + 1] = R;
            ++w;  filled = std::min<int64_t> (filled + 1, mask);
        }
        mix = frozen ? std::min (1.0f, mix + fadeStep) : std::max (0.0f, mix - fadeStep);
        replace = hold ? std::max (0.0f, replace - fadeStep) : std::min (1.0f, replace + fadeStep);
        if (mix <= 0.0f) return;

        const double size = std::min ({ target, (double) filled - 8.0, (double) mask - 8.0 });
        if (size < 8.0) return;
        if (pos >= size) pos = std::fmod (pos, size);
        const double xf = std::min (0.005 * sr, size * 0.25);   // seam crossfade
        const double start = (double) end - size;

        float yL, yR;
        read (start + pos, yL, yR);
        if (pos > size - xf)                                    // blend into the audio before the loop
        {
            float pL, pR;
            read (start + pos - size, pL, pR);
            const float t = (float) ((pos - (size - xf)) / xf);
            yL = yL + t * (pL - yL);  yR = yR + t * (pR - yR);
        }
        pos += 1.0;
        if (pos >= size) pos -= size;
        const float keep = 1.0f - mix * replace;                // live: gone when frozen, back with HOLD
        L = L * keep + yL * mix * level;
        R = R * keep + yR * mix * level;
    }

private:
    void read (double at, float& l, float& r) const noexcept
    {
        const auto i = (int64_t) std::floor (at);
        const float f = (float) (at - (double) i);
        const size_t a = (size_t) (i & mask) * 2, b = (size_t) ((i + 1) & mask) * 2;
        l = buf[a] + f * (buf[b] - buf[a]);
        r = buf[a + 1] + f * (buf[b + 1] - buf[a + 1]);
    }

    std::vector<float> buf;
    int64_t mask = 0, w = 0, end = 0, filled = 0;
    double  sr = 44100.0, pos = 0.0, target = 4410.0;
    float   mix = 0.0f, replace = 1.0f, fadeStep = 0.01f;
    bool    frozen = false;
};
} // namespace cba
