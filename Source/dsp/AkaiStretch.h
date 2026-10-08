#pragma once
// =============================================================================
// AkaiStretch.h  -  the Akai timestretch arithmetic, decoded from the S950 / S2000 ROMs
//
//   Model::S950  (OS 1.2 ROM)  12-bit.  CYCLIC only.  MONO (linear) / POLY (sine) fades,
//                              fade spans the whole period.  Auto D.
//   Model::S2000 (OS 2.00 ROM) 16-bit.  CYCLIC and INTELL.  Linear Q15 fade lasting
//                              min((XFD+1) x cycle, period); new voice then plays alone.
//
// S950 references : STRETCH page 0x7B0B, Do 0x7BF6, passes 0x7726/0x7751/0x7ECA/0x7F1A,
//                   Auto D 0x6FEC.
// S2000 references: params 0xEE4 (cycle 20..2000, % 25..2000), main loop 0x14811,
//                   fade setup 0x13D79, mix pass 0x255BB, INTELL search 0x2573C.
//                   Sample-rate flag 0x87A1 bit0 (44.1k) doubles INTELL quality,
//                   min lag (20->40) and range (1000->2000).
// Used by CongoSampler (LIVE) and SustainEngine (SUSTAIN). Header-only C++17, no JUCE.
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace cba
{

namespace s950
{
    // ---- ROM 0x7C17: poly fade table ----------------------------------------
    inline std::array<int16_t, 4097> makeSineTable()
    {
        std::array<int16_t, 4097> t {};
        uint32_t x = 0x10000000u, y = 0;
        for (int k = 0; k < 4097; ++k)
        {
            const uint32_t si = y >> 16, bp = y & 0xFFFFu;
            t[(size_t) k] = (int16_t) (uint16_t) (si + (bp >= 0x8000u ? 1u : 0u));
            y += 0x19u * (x >> 16);
            x -= 0x19u * (y >> 16);
        }
        return t;
    }

    inline const std::array<int16_t, 4097>& sineTable()
    {
        static const auto t = makeSineTable();
        return t;
    }

    // ---- ROM 0x7C63: period length -------------------------------------------
    struct Period { int P; int D; };   // D may come back halved, as on the hardware

    inline Period period (int D, int pct) noexcept
    {
        if (pct == 100) return { D, D };
        while (D > 0)
        {
            const long q = (long) D * pct / std::abs (pct - 100);
            if (q <= 32767) return { (int) q, D };
            D >>= 1;
        }
        return { 0, 0 };
    }

    inline int32_t sat16 (int32_t v) noexcept { return std::clamp (v, -32768, 32767); }

    inline int gain (uint32_t phase, bool poly) noexcept
    {
        const int idx = (int) ((phase >> 8) & 0xFFFFu);        // 0..4096
        return poly ? sineTable()[(size_t) idx] : idx;
    }

    // One output word: pass 1 (fade-out write) + pass 2 (fade-in saturating add)
    inline int32_t mixWord (int a12, int b12, int i, uint32_t inc, bool poly) noexcept
    {
        const int32_t va = sat16 ((a12 * gain ((1u << 20) - (uint32_t) i * inc, poly)) >> 8);
        const int32_t vb = sat16 ((b12 * gain ((uint32_t) i * inc, poly)) >> 8);
        return sat16 (va + vb);
    }

    // ---- ROM 0x6FEC..0x7158: Auto D ------------------------------------------
    // top8(n) must return the top 8 bits of 12-bit sample n (i.e. s12 >> 4) as int8.
    // 'end' is one past the last sample. Returns the new D (lag of best 32-sample match,
    // searching lags D-1 downwards over up to 2000-32 candidates).
    template <typename Top8>
    int autoD (Top8 top8, int64_t end, int D) noexcept
    {
        constexpr int win = 32, range = 2000;
        int64_t start0 = end - D - win;
        if (start0 < 0) start0 = 0;
        const int64_t upper = std::min<int64_t> (start0 + range, end - 5);
        const int64_t cnt   = upper - start0 - win;
        uint32_t best = 0xFFFFFFFFu;
        int64_t  bestD = win;                                   // default if nothing accepted

        for (int64_t d = win; d < win + cnt; ++d)
        {
            uint16_t bp = 0, dx = 0;
            bool reject = false;
            for (int m = 0; m < win && ! reject; ++m)
            {
                const int r = top8 (end - 2 - m);               // reference, newest first
                const int c = top8 (start0 + d - 1 - m);        // candidate
                const int diff = r - c;
                if (diff < -128 || diff > 127) { reject = true; break; }   // 'jo'
                const uint16_t sq = (uint16_t) (diff * diff);
                if (m == 0)
                {
                    if ((sq & 0xFF) >= 10) { reject = true; break; }        // cmp al,0Ah
                    bp = (uint16_t) (sq << 2);
                    dx = (bp & 0x8000) ? 0xFFFF : 0;                         // cwd
                }
                else if (m == 1) bp = (uint16_t) (bp + (uint16_t) (sq << 1));
                else
                {
                    const uint32_t s = (uint32_t) bp + sq;
                    bp = (uint16_t) s;
                    if (s > 0xFFFF) ++dx;
                }
            }
            if (reject) continue;
            const uint32_t score = ((uint32_t) dx << 16) | bp;
            if (score <= best) { best = score; bestD = d; }     // ties: later (shorter lag) wins
        }
        const int64_t newD = (int64_t) D + win - 1 - bestD;
        return (int) std::clamp<int64_t> (newD, 5, 9999);
    }

    // ---- Offline render: bit-exact ROM model (for testing / capture-and-render) --
    // x12: signed 12-bit samples. Returns 16-bit words (DAC hears word >> 4).
    inline std::vector<int16_t> renderOffline (const int16_t* x12, int64_t n, int D, int pct,
                                               bool poly, int64_t start = 0, int64_t end = -1)
    {
        if (end < 0) end = n;
        std::vector<int16_t> out;
        const auto pr = period (D, pct);
        if (pct == 100 || pr.P <= 0)
        {
            for (int64_t k = 0; k < n; ++k) out.push_back ((int16_t) (x12[k] * 16));
            return out;
        }
        const int P = pr.P;  D = pr.D;
        const int64_t jump = pct > 100 ? -D : D;
        if (pct > 100 && start < D) start = D;                  // ROM 0x7D52
        for (int64_t k = 0; k < start; ++k) out.push_back ((int16_t) (x12[k] * 16));
        const uint32_t inc = (1u << 20) / (uint32_t) P;
        int64_t a = start;
        for (;;)
        {
            const int64_t b = a + jump;
            for (int i = 0; i < P; ++i)
                out.push_back ((int16_t) mixWord (x12[a + i], x12[b + i], i, inc, poly));
            a = b + P;
            if (a + P + (pct < 100 ? D : 0) >= end) break;     // ROM 0x7E63
        }
        for (int64_t k = a; k < n; ++k) out.push_back ((int16_t) (x12[k] * 16));
        return out;
    }
} // namespace s950
namespace s2000
{
    // ratio = pct/|pct-100| in 8.8 fixed point (0x148F0)
    inline uint32_t ratio88 (int pct) noexcept
    {
        return ((uint32_t) pct << 8) / (uint32_t) std::abs (pct - 100);
    }

    inline uint32_t periodLen (uint32_t C, uint32_t ratio) noexcept { return (C * ratio) >> 8; }

    // 0x13D79: fade length = min((XFD+1)*C, P); increment = floor(0x7FFF0000 / L)
    inline uint32_t fadeLen (uint32_t C, int xfd, uint32_t P) noexcept
    {
        return std::min ((uint32_t) (xfd + 1) * C, P);
    }
    inline uint32_t fadeInc (uint32_t L) noexcept
    {
        return L ? (uint32_t) (0x7FFF0000ull / L) : 0x7FFF0000u;
    }

    // 0x25696: 32-bit gain accumulator, high word is the Q15 gain, clamped at 0x7FFF
    struct Ramp
    {
        uint16_t lo = 0, hi = 0;
        void step (uint32_t inc) noexcept
        {
            const uint32_t t = (uint32_t) lo + (inc & 0xFFFFu);
            lo = (uint16_t) t;
            const uint32_t h = ((uint32_t) hi + (inc >> 16) + (t >> 16)) & 0xFFFFu;
            hi = (h & 0x8000u) ? (uint16_t) 0x7FFF : (uint16_t) h;
        }
    };

    // 0x25672: out = cur*(0x7FFF-g)>>15 + new*g>>15
    inline int16_t mixWord (int16_t cur, int16_t nw, uint16_t g) noexcept
    {
        const int32_t a = ((int32_t) cur * (int32_t) (0x7FFF - g)) >> 15;
        const int32_t b = ((int32_t) nw  * (int32_t) g) >> 15;
        return (int16_t) (a + b);
    }

    struct IntelCfg { int qp1, minLag, range; };
    inline IntelCfg intelCfg (int quality, bool rate44k) noexcept
    {
        IntelCfg c { quality + 1, 20, 1000 };
        if (rate44k) { c.qp1 *= 2; c.minLag *= 2; c.range *= 2; }
        return c;
    }

    // 0x14997: longest compare window
    inline uint32_t maxCompareLen (uint32_t ratio, int xfd, int range) noexcept
    {
        const uint32_t aba  = std::min (ratio, (uint32_t) (xfd + 1) << 8);
        const uint32_t prod = aba * (uint32_t) range;
        if (prod >> 24) return 0x4000;
        return std::min<uint32_t> (prod >> 8, 0x4000);
    }

    // 0x2573C..0x25824: INTELL search. x(k) returns the 16-bit sample at index k.
    // pos = start of the current voice for this period. Returns the chosen cycle length.
    template <typename X>
    int intelSearch (X x, int64_t pos, bool compress, uint32_t ratio, int xfd,
                     int quality, bool rate44k) noexcept
    {
        const IntelCfg cfg = intelCfg (quality, rate44k);
        const uint32_t aba = std::min (ratio, (uint32_t) (xfd + 1) << 8);
        const uint32_t mx  = maxCompareLen (ratio, xfd, cfg.range);
        uint32_t best = 0xFFFFFFFFu;
        int bestLag = cfg.minLag + 4;

        auto scan = [&] (int lag, int ncmp, bool fine) -> uint32_t
        {
            const uint32_t prod = (uint32_t) lag * aba;
            uint32_t len = (prod >> 8) & 0xFFFFu;
            if ((prod >> 24) != 0 || len > mx) len = mx;
            uint32_t stride = len / (uint32_t) ncmp;
            if (stride == 0) stride = 1;
            const int64_t off = compress ? lag : -lag;
            uint32_t sum = 0;
            int64_t p = pos;
            for (int k = 0; k < ncmp; ++k, p += stride)
            {
                const int d = (int) x (p) - (int) x (p + off);
                sum += (uint32_t) std::abs (d);
            }
            return fine ? (sum >> 1) : sum;
        };

        const int count = (cfg.range + 1 - cfg.minLag) >> 3;
        std::array<uint32_t, 256> tbl {};
        int lag = cfg.minLag + 4;                                  // coarse: step 8
        for (int j = 0; j < count; ++j, lag += 8)
        {
            const uint32_t s = scan (lag, cfg.qp1, false);
            tbl[(size_t) j] = s;
            if (s < best) { best = s; bestLag = lag; }
        }
        lag = cfg.minLag;                                          // fine: step 2, x2 quality
        for (int j = 0; j < count; ++j)
        {
            const uint32_t h = tbl[(size_t) j] >> 1;
            if (h + (h >> 1) < best)
            {
                for (int f = 0; f < 4; ++f, lag += 2)
                {
                    const uint32_t s = scan (lag, cfg.qp1 * 2, true);
                    if (s < best) { best = s; bestLag = lag; }
                }
            }
            else lag += 8;
        }
        return bestLag;
    }
} // namespace s2000

} // namespace cba
