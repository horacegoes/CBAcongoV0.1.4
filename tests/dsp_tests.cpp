// Congo Filter DSP test suite (no JUCE). Build: g++ -std=c++17 -O2 -I../Source/dsp dsp_tests.cpp
#include "CongoSampler.h"
#include "CBAFilter.h"
#include "SamplerFrontEnd.h"
#include "S950TestTone.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
using namespace cba;
static constexpr double M_PI_ = 3.14159265358979323846;
static int failures = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); printf("  [%s] ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++failures; } while (0)
static const double FS = 44100.0;

static std::vector<int16_t> loadAmen16() {           // mono L, tiled x4
    std::ifstream f("amen16x3.raw", std::ios::binary); std::vector<int16_t> x(198450);
    f.read((char*) x.data(), (std::streamsize) x.size() * 2); return x; }

static double peakFreq (const std::vector<float>& y, size_t a, size_t b, double fs) {
    // single-bin DFT scan (coarse then fine) over [a,b)
    auto mag = [&](double f){ double re=0,im=0; for (size_t n=a;n<b;++n){ double w=0.5-0.5*std::cos(2*M_PI_*(n-a)/(b-a)); re+=y[n]*w*std::cos(2*M_PI_*f*n/fs); im+=y[n]*w*std::sin(2*M_PI_*f*n/fs);} return re*re+im*im; };
    double best=0,bf=0; for (double f=40; f<5000; f*=1.003){ double m=mag(f); if(m>best){best=m;bf=f;} }
    for (double f=bf*0.996; f<bf*1.004; f+=bf*0.0002){ double m=mag(f); if(m>best){best=m;bf=f;} }
    return bf; }

int main (int argc, char** argv)
{
    const bool dump = argc > 1 && std::string (argv[1]) == "dump";
    auto amen = loadAmen16();

    printf("== 1. Sampler at pitch 0 vs offline ROM model (S950 bit-exact)\n");
    for (int pct : {200, 700, 50, 25})
    {
        CongoSampler s; s.prepare (FS, 1, 12);
        CongoSampler::Params p; p.model = CongoSampler::Model::S950; p.timePct = pct; p.cycle = 1000; p.hwRate = FS;
        s.setParams (p);
        const long t0 = 120000, N = 20000;
        std::vector<int32_t> rt;
        for (long n = 0; n < t0 + N; ++n) {
            if (n == t0) s.trigger (N);
            float fr[1] = { (amen[(size_t) n % amen.size()] >> 4) / 2048.0f };
            s.processFrame (fr);
            if (n >= t0) rt.push_back ((int32_t) std::lround (fr[0] * 2048.0f)); }
        const long a0 = (long) s.getLastTriggerStart();
        std::vector<int16_t> xx (t0 + N + 200000);
        for (size_t n = 0; n < xx.size(); ++n) xx[n] = (int16_t) (amen[n % amen.size()] >> 4);
        // like the ROM, a stretch plays its first cycle straight and starts the periods one cycle in
        auto y = s950::renderOffline (xx.data(), (int64_t) xx.size(), 1000, pct, true, a0 + (pct > 100 ? 1000 : 0), (int64_t) xx.size());
        long mism = 0; for (long k = 64; k < N; ++k) if ((y[(size_t) (a0 + k)] >> 4) != rt[(size_t) k]) ++mism;
        CHECK (mism == 0 && s.getReanchorCount() == 0, "S950 %d%%: %ld mismatches over %ld samples, re-anchors %d", pct, mism, N - 64, s.getReanchorCount());
    }

    if (dump) {   // S2000 outputs for the Python reference check
        struct C { int pct, cyc; bool intel; int xfd, q; } cs[] = {{200,1000,false,0,0},{375,400,false,3,0},{50,1000,false,0,0},{200,0,true,0,10},{400,0,true,2,5},{60,0,true,0,0}};
        std::ofstream meta ("s2000_meta.txt");
        int idx = 0;
        for (auto c : cs) {
            CongoSampler s; s.prepare (FS, 1, 12);
            CongoSampler::Params p; p.model = CongoSampler::Model::S2000; p.type = c.intel ? CongoSampler::Type::Intelligent : CongoSampler::Type::Cyclic;
            p.timePct = c.pct; p.cycle = c.cyc ? c.cyc : 1000; p.xfd = c.xfd; p.quality = c.q; p.hwRate = FS; s.setParams (p);
            const long t0 = 120000, N = 20000; std::vector<int16_t> out;
            for (long n = 0; n < t0 + N; ++n) { if (n == t0) s.trigger (N); float fr[1] = { amen[(size_t) n % amen.size()] / 32768.0f }; s.processFrame (fr);
                if (n >= t0) out.push_back ((int16_t) std::lround (fr[0] * 32768.0f)); }
            std::ofstream o ("s2000_out_" + std::to_string (idx) + ".raw", std::ios::binary); o.write ((char*) out.data(), (std::streamsize) out.size() * 2);
            meta << idx << " " << c.pct << " " << (c.cyc ? c.cyc : 1000) << " " << (c.intel ? "intel" : "cyclic") << " " << c.xfd << " " << c.q << " " << s.getLastTriggerStart() << " " << s.getReanchorCount() << " " << s.getLastLead() << "\n";
            ++idx; }
    }

    printf("== 2. Pitch: sine 441 Hz through the sampler, retriggered every beat at 120 bpm\n");
    struct PT { CongoSampler::Model m; CongoSampler::Type t; double time, semis; } pts[] = {
        {CongoSampler::Model::S950, CongoSampler::Type::Cyclic, 100, 7}, {CongoSampler::Model::S950, CongoSampler::Type::Cyclic, 300, 12},
        {CongoSampler::Model::S950, CongoSampler::Type::Cyclic, 100, -12}, {CongoSampler::Model::S2000, CongoSampler::Type::Cyclic, 200, 7},
        {CongoSampler::Model::S2000, CongoSampler::Type::Intelligent, 200, 7}, {CongoSampler::Model::S2000, CongoSampler::Type::Intelligent, 50, -5},
        {CongoSampler::Model::S2000, CongoSampler::Type::Intelligent, 2000, 24}, {CongoSampler::Model::S950, CongoSampler::Type::Cyclic, 25, -24} };
    for (auto t : pts) {
        CongoSampler s; s.prepare (FS, 1, 12);
        // 441 Hz = exactly 100 samples per cycle; cycle length 400 = 4 whole waveform cycles
        CongoSampler::Params p; p.model = t.m; p.type = t.t; p.timePct = t.time; p.pitchSemis = t.semis; p.cycle = 400; p.hwRate = FS; p.quality = 10; s.setParams (p);
        const long N = (long) FS * 6, beat = 22050; std::vector<float> y ((size_t) N);
        for (long n = 0; n < N; ++n) { if (n % beat == 0) s.trigger (beat); float fr[1] = { 0.5f * (float) std::sin (2 * M_PI_ * 441.0 * n / FS) }; s.processFrame (fr); y[(size_t) n] = fr[0]; }
        const double want = 441.0 * std::exp2 (t.semis / 12.0);
        const double got = peakFreq (y, (size_t) FS * 4 + 2000, (size_t) FS * 4 + 2000 + 16384, FS);
        const double cents = 1200 * std::log2 (got / want);
        CHECK (std::fabs (cents) < 15.0 && s.getReanchorCount() == 0, "%s %s time %.0f%% pitch %+.0f: want %.1f Hz got %.1f Hz (%+.1f cents), re-anchors %d",
               t.m == CongoSampler::Model::S950 ? "S950 " : "S2000", t.t == CongoSampler::Type::Cyclic ? "CYCLIC" : "INTELL", t.time, t.semis, want, got, cents, s.getReanchorCount());
    }

    printf("== 3. Free-running (no host transport) stays bounded and keeps playing\n");
    for (double time : {25.0, 100.0, 2000.0}) for (double semis : {-24.0, 0.0, 24.0}) {
        CongoSampler s; s.prepare (FS, 2, 12);
        CongoSampler::Params p; p.model = CongoSampler::Model::S2000; p.type = CongoSampler::Type::Intelligent; p.timePct = time; p.pitchSemis = semis; p.hwRate = FS; s.setParams (p);
        s.trigger (0); double e = 0; bool bad = false; const long N = (long) FS * 8;
        for (long n = 0; n < N; ++n) { float fr[2] = { 0.3f * (float) std::sin (n * 0.05), 0.3f * (float) std::sin (n * 0.031) }; s.processFrame (fr);
            if (!std::isfinite (fr[0]) || !std::isfinite (fr[1]) || std::fabs (fr[0]) > 2.0f) bad = true;
            if (n > N - (long) FS) e += fr[0] * fr[0]; }
        CHECK (!bad && e > 1.0, "time %4.0f%% pitch %+3.0f: finite %s, still sounding (energy %.0f)", time, semis, bad ? "NO" : "yes", e);
    }

    printf("== 4. Moog resonance: self-oscillation begins at 97%%\n");
    for (float r : {0.95f, 0.96f, 0.97f, 0.98f, 1.0f}) {
        CBAFilter f; f.prepare (FS, 1, 2); CBAFilter::Params q; q.analog = 0; q.cutoffHz = 440; q.resonance = r; f.setParams (q);
        const int N = (int) FS * 6; std::vector<float> b ((size_t) N, 0.0f); b[100] = 0.5f; float* io[1] = { b.data() }; f.process (io, 1, N);
        double e1 = 0, e2 = 0; for (int n = N - (int) FS * 2; n < N - (int) FS; ++n) e1 += b[(size_t) n] * b[(size_t) n]; for (int n = N - (int) FS; n < N; ++n) e2 += b[(size_t) n] * b[(size_t) n];
        const bool osc = e2 > 1e-4 && e2 >= e1 * 0.999;
        if (r < 0.97f) CHECK (!osc, "res %.0f%%: rings and dies away (not oscillating)", r * 100);
        else { std::vector<float> y (b.begin() + (N - 8192), b.end()); const double f0 = peakFreq (y, 0, y.size(), FS);
               CHECK (osc && std::fabs (1200 * std::log2 (f0 / 440)) < 5, "res %.0f%%: self-oscillates at %.1f Hz, %.1f dBFS rms", r * 100, f0, 10 * std::log10 (e2 / FS)); }
    }

    printf("== 4b. Moog self-oscillation starts on its own (no input at all), like the real circuit\n");
    for (float r : {0.97f, 1.0f}) {
        CBAFilter f; f.prepare (FS, 1, 2); CBAFilter::Params q; q.analog = 0; q.cutoffHz = 440; q.resonance = r; f.setParams (q);
        std::vector<float> b ((size_t) FS * 3, 0.0f); float* io[1] = { b.data() }; f.process (io, 1, (int) b.size());
        float pk = 0; for (size_t i = b.size() - 4410; i < b.size(); ++i) pk = std::max (pk, std::fabs (b[i]));
        const double f0 = peakFreq (std::vector<float> (b.end() - 8192, b.end()), 0, 8192, FS);
        CHECK (pk > 0.05f && std::fabs (1200 * std::log2 (f0 / 440)) < 5, "res %.0f%%, pure silence in: sings at %.1f Hz (peak %.2f)", r * 100, f0, pk); }
    { CBAFilter f; f.prepare (FS, 1, 2); CBAFilter::Params q; q.cutoffHz = 440; q.resonance = 0.96f; f.setParams (q);
      std::vector<float> b ((size_t) FS, 0.0f); float* io[1] = { b.data() }; f.process (io, 1, (int) b.size());
      float pk = 0; for (float v : b) pk = std::max (pk, std::fabs (v));
      CHECK (pk == 0.0f, "res 96%%, pure silence in: stays exactly silent (no noise added below the threshold)"); }

    printf("== 4c. ANALOG is clearly audible: self-oscillation pitch wanders, L and R apart\n");
    for (float an : {0.0f, 0.5f, 1.0f}) {
        CBAFilter f; f.prepare (FS, 2, 2); CBAFilter::Params q; q.analog = an; q.cutoffHz = 440; q.resonance = 1.0f; f.setParams (q);
        const int N = (int) FS * 8; std::vector<float> L ((size_t) N, 0.0f), R ((size_t) N, 0.0f); L[100] = R[100] = 0.3f;
        float* io[2] = { L.data(), R.data() }; f.process (io, 2, N);
        double lo = 1e9, hi = 0, lrMax = 0;
        for (int w = 2; w < 8; ++w) {
            const double fl = peakFreq (std::vector<float> (L.begin() + w * (int) FS, L.begin() + w * (int) FS + 8192), 0, 8192, FS);
            const double fr = peakFreq (std::vector<float> (R.begin() + w * (int) FS, R.begin() + w * (int) FS + 8192), 0, 8192, FS);
            lo = std::min (lo, fl); hi = std::max (hi, fl); lrMax = std::max (lrMax, std::fabs (1200 * std::log2 (fl / fr))); }
        const double wander = 1200 * std::log2 (hi / lo);
        if (an == 0.0f) CHECK (wander < 2 && lrMax < 2, "ANALOG 0%%: rock steady (wander %.1f cents, L/R %.1f cents)", wander, lrMax);
        else CHECK (wander > (an > 0.75f ? 15 : 6) && lrMax > (an > 0.75f ? 10 : 4),
                    "ANALOG %.0f%%: pitch wanders %.0f cents over 6 s, L and R up to %.0f cents apart", an * 100, wander, lrMax); }

    printf("== 5. Moog Bass Keep (60 Hz, cutoff 2 kHz, LP24, res 90%%)\n");
    for (float bk : {0.0f, 1.0f}) {
        CBAFilter f; f.prepare (FS, 1, 2); CBAFilter::Params q; q.analog = 0; q.cutoffHz = 2000; q.resonance = 0.9f; q.bassKeep = bk; f.setParams (q);
        const int N = (int) FS; std::vector<float> b ((size_t) N); for (int n = 0; n < N; ++n) b[(size_t) n] = 0.01f * (float) std::sin (2 * M_PI_ * 60 * n / FS);
        float* io[1] = { b.data() }; f.process (io, 1, N); double e = 0; for (int n = N / 2; n < N; ++n) e += b[(size_t) n] * b[(size_t) n];
        const double db = 10 * std::log10 (e / (N / 2) / (0.5 * 1e-4));
        if (bk > 0.5f) CHECK (std::fabs (db) < 0.5, "Bass Keep 100%%: %+.2f dB", db); else CHECK (db < -6, "Bass Keep 0%% (authentic Moog): %+.2f dB", db);
    }

    printf("== 6. Front end: sample rate and bits\n");
    for (double tone : {2000.0, 9000.0}) { SamplerFrontEnd fe; fe.prepare (FS); fe.set (8000, 16, 1.0);
      const int N = 44100; double e = 0;
      for (int n = 0; n < N; ++n) { float fr[1] = { 0.5f * (float) std::sin (2 * M_PI_ * tone * n / FS) }; fe.encode (fr, 1); fe.decode (fr, 1); if (n > N / 2) e += fr[0] * fr[0]; }
      const double db = 10 * std::log10 (e / (N / 2) / 0.125 + 1e-30);
      if (tone < 3000) CHECK (db > -4, "rate 8 kHz: 2 kHz tone passes (%.1f dB)", db);
      else CHECK (db < -40, "rate 8 kHz: 9 kHz tone (aliases on a real sampler too) held to %.1f dB", db); }
    for (int bits : {4, 8, 12, 16}) {
        SamplerFrontEnd fe; fe.prepare (FS); fe.set (44100, bits, 1.0); double en = 0;
        for (int n = 0; n < 44100; ++n) { const float x = 0.5f * (float) std::sin (2 * M_PI_ * 1000 * n / FS); float fr[1] = { x }; fe.encode (fr, 1); en += (fr[0] - x) * (fr[0] - x); }
        const double snr = 10 * std::log10 (0.125 / (en / 44100));
        CHECK (std::fabs (snr - (6.02 * bits - 9.5)) < 3, "%2d bits: SNR %.1f dB (truncating ADC theory ~%.1f)", bits, snr, 6.02 * bits - 9.5); }

    printf("== 7. Test tone\n");
    { S950TestTone t; t.prepare (48000); t.setActive (true); std::vector<float> y (48000); for (auto& v : y) v = t.next();
      const double f = peakFreq (y, 4800, 4800 + 16384, 48000); CHECK (std::fabs (1200 * std::log2 (f / 261.68)) < 3, "C3 at 48 kHz host: %.2f Hz", f); }

    printf("== 7b. Extremes: 7.5 kHz sampler on a 192 kHz host\n");
    for (double semis : {-24.0, 0.0, 24.0}) for (double time : {25.0, 101.0, 2000.0}) for (int m = 0; m < 3; ++m) {
        CongoSampler s; s.prepare (192000, 2, 12);
        CongoSampler::Params p; p.model = m == 0 ? CongoSampler::Model::S950 : CongoSampler::Model::S2000; p.type = m == 2 ? CongoSampler::Type::Intelligent : CongoSampler::Type::Cyclic;
        p.timePct = time; p.pitchSemis = semis; p.cycle = m == 0 ? 9999 : 2000; p.hwRate = 7500; p.quality = 20; p.xfd = 20; p.maxLag = 384000; s.setParams (p);
        s.trigger (0); bool bad = false; double e = 0; const long N = 192000 * 4;
        for (long n = 0; n < N; ++n) { float fr[2] = { 0.3f * (float) std::sin (n * 0.01), 0.3f * (float) std::sin (n * 0.013) }; s.processFrame (fr);
            if (!std::isfinite (fr[0]) || std::fabs (fr[0]) > 2.0f) bad = true;
            if (n > N - 192000) e += fr[0] * fr[0]; }
        CHECK (!bad && e > 1.0, "%s time %4.0f%% pitch %+3.0f: finite, sounding", m == 0 ? "S950        " : (m == 1 ? "S2000 CYCLIC" : "S2000 INTELL"), time, semis);
    }

    printf("== 8. Stress: whole chain, random everything, 2 minutes of audio\n");
    {
        CongoSampler s; s.prepare (FS, 2, 12); SamplerFrontEnd fe; fe.prepare (FS); CBAFilter f; f.prepare (FS, 2, 2);
        fdsp::Rng r; bool bad = false; float pk = 0; std::vector<float> L (512), R (512);
        auto t0 = std::chrono::steady_clock::now();
        const int blocks = (int) (FS * 120 / 512);
        for (int blk = 0; blk < blocks; ++blk) {
            CongoSampler::Params p; p.model = (blk / 50) % 2 ? CongoSampler::Model::S2000 : CongoSampler::Model::S950;
            p.type = (blk / 23) % 2 ? CongoSampler::Type::Intelligent : CongoSampler::Type::Cyclic;
            p.timePct = 25.0 * std::pow (80.0, 0.5 + 0.5 * r.next()); p.pitchSemis = 24.5 * r.next(); p.cycle = (int) (5 * std::pow (2000.0, 0.5 + 0.5 * r.next()));
            p.poly = r.next() > 0; p.autoD = (blk / 7) % 3 == 0; p.quality = (int) (10 + 10 * r.next()); p.xfd = (int) (10 + 10 * r.next());
            p.hwRate = 7500 + 40500 * (0.5 + 0.5 * r.next()); s.setParams (p);
            fe.set (p.hwRate, (int) (8.5 + 7.5 * r.next()), std::exp2 (p.pitchSemis / 12));
            CBAFilter::Params q; q.engine = (blk / 31) % 2 ? CBAFilter::Engine::Akai : CBAFilter::Engine::Moog; q.mode = (CBAFilter::Mode) ((blk / 13) % 6);
            q.cutoffHz = 20 * std::pow (1000.0f, 0.5f + 0.5f * r.next()); q.resonance = 0.5f + 0.5f * r.next(); q.driveDb = 18 + 18 * r.next(); q.analog = 1; f.setParams (q);
            if (blk % 40 == 0) s.trigger ((blk / 80) % 2 ? 512 * 40 : 0);
            for (int i = 0; i < 512; ++i) {
                float fr[2] = { 1.5f * r.next(), 1.5f * r.next() };
                fe.encode (fr, 2); s.processFrame (fr); fe.decode (fr, 2); L[(size_t) i] = fr[0]; R[(size_t) i] = fr[1]; }
            float* io[2] = { L.data(), R.data() }; f.process (io, 2, 512);
            for (int i = 0; i < 512; ++i) { if (!std::isfinite (L[(size_t) i]) || !std::isfinite (R[(size_t) i])) bad = true; pk = std::max (pk, std::fabs (L[(size_t) i])); }
        }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (!bad && pk <= 1.0001f, "no NaN/Inf, peak %.3f", pk);
        printf("  [INFO] worst-case CPU (random extremes incl. INTELL at +24 st): %.1f%% of one core\n", 100 * sec / 120.0);
    }

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
