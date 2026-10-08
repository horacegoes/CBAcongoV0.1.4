// SUSTAIN mode tests. Build: g++ -std=c++17 -O2 -I../Source/dsp sustain_tests.cpp
#include "SustainEngine.h"
#include "CBAFilter.h"
#include <cstdio>
#include <fstream>
#include <vector>
using namespace cba;
static int failures = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); printf("  [%s] ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++failures; } while (0)
static const double FS = 44100.0, PI_ = 3.14159265358979323846;

static std::vector<int16_t> loadAmen16() {
    std::ifstream f ("amen16x3.raw", std::ios::binary); std::vector<int16_t> x (198450);
    f.read ((char*) x.data(), (std::streamsize) x.size() * 2); return x; }

// play a phrase (x, n samples) into a fresh engine, then silence; return output until all voices end
static std::vector<float> runPhrase (const std::vector<float>& in, const VoiceSettings& vs, long maxOut, long* phraseLen = nullptr)
{
    SustainEngine e; e.prepare (FS, 10.0);
    e.startPhrase (vs);
    std::vector<float> out;
    for (long n = 0; n < maxOut; ++n) {
        if (n == (long) in.size()) e.endPhrase();
        float fr[2] = { n < (long) in.size() ? in[(size_t) n] : 0.0f, n < (long) in.size() ? in[(size_t) n] : 0.0f }, o[2];
        e.processFrame (fr, o); out.push_back (o[0]);
        if (n > (long) in.size() && e.activeVoices() == 0) break; }
    if (phraseLen) *phraseLen = (long) in.size();
    return out;
}

static double freqOf (const std::vector<float>& y, size_t a, size_t n) {
    int z = 0; double f = -1, l = -1;
    for (size_t i = a + 1; i < a + n && i < y.size(); ++i) if (y[i-1] < 0 && y[i] >= 0) { double t = (double) i - 1 + (-y[i-1]) / (y[i] - y[i-1]); if (f < 0) f = t; l = t; ++z; }
    return z > 1 ? (z - 1) / ((l - f) / FS) : 0; }

int main()
{
    auto amen = loadAmen16();

    printf ("== 1. S950 phrase at pitch 0 vs the ROM model (whole phrase, start + end handling)\n");
    for (int pct : {200, 600, 50, 2000}) for (bool poly : {true, false}) {
        const long n = 44100;
        std::vector<float> in ((size_t) n); std::vector<int16_t> x12 ((size_t) n);
        for (long k = 0; k < n; ++k) { x12[(size_t) k] = (int16_t) (amen[(size_t) k] >> 4); in[(size_t) k] = x12[(size_t) k] / 2048.0f; }
        VoiceSettings vs; vs.model = CongoSampler::Model::S950; vs.timePct = pct; vs.cycle = 1000; vs.poly = poly; vs.hwRate = FS; vs.bypassConverter = true;
        auto out = runPhrase (in, vs, n * 25);
        auto ref = s950::renderOffline (x12.data(), n, 1000, pct, poly, 0, n);
        // find where playback starts, then compare every sample of the whole phrase
        long best = -1;
        for (long off = 0; off < n + 400; ++off) {
            long mis = 0; for (long k = 0; k < 2000 && mis < 5; ++k) if (std::lround (out[(size_t) (off + k)] * 2048.0f) != (ref[(size_t) k] >> 4)) ++mis;
            if (mis == 0) { best = off; break; } }
        long mism = 0;
        if (best >= 0) for (size_t k = 0; k < ref.size(); ++k) if (best + (long) k >= (long) out.size() || std::lround (out[(size_t) best + k] * 2048.0f) != (ref[k] >> 4)) ++mism;
        CHECK (best >= 0 && mism == 0, "S950 %s %4d%%: whole phrase (%zu samples out of %ld in) %s; playback starts %ld samples in",
               poly ? "POLY" : "MONO", pct, ref.size(), n, best >= 0 && mism == 0 ? "bit-exact" : "MISMATCH", best);
    }

    printf ("== 2. The 'whos the badman' case: a 1 s phrase keeps playing after the input stops\n");
    for (auto m : {0, 1, 2}) for (double t : {600.0, 2000.0}) {
        std::vector<float> in (44100); for (size_t k = 0; k < in.size(); ++k) in[k] = 0.4f * (float) std::sin (2 * PI_ * 220 * (double) k / FS) * (float) std::sin (PI_ * (double) k / 44100.0);
        VoiceSettings vs; vs.model = m == 0 ? CongoSampler::Model::S950 : CongoSampler::Model::S2000; vs.type = m == 2 ? CongoSampler::Type::Intelligent : CongoSampler::Type::Cyclic;
        vs.timePct = t; vs.hwRate = 44100; vs.bits = 16;
        auto out = runPhrase (in, vs, (long) (FS * 30));
        long last = 0; for (long k = 0; k < (long) out.size(); ++k) if (std::fabs (out[(size_t) k]) > 1e-3f) last = k;
        const double secs = last / FS;
        CHECK (std::fabs (secs - t / 100.0) < 0.12 * t / 100.0, "%s %4.0f%%: a 1 s phrase plays for %.2f s (expected ~%.1f s), then the voice ends",
               m == 0 ? "S950        " : (m == 1 ? "S2000 CYCLIC" : "S2000 INTELL"), t, secs, t / 100.0);
    }

    printf ("== 3. Pitch in SUSTAIN\n");
    for (double semis : {7.0, -12.0, 24.0}) {
        std::vector<float> in (88200); for (size_t k = 0; k < in.size(); ++k) in[k] = 0.4f * (float) std::sin (2 * PI_ * 441.0 * (double) k / FS);
        VoiceSettings vs; vs.model = CongoSampler::Model::S2000; vs.type = CongoSampler::Type::Intelligent; vs.timePct = 300; vs.pitchSemis = semis; vs.hwRate = 44100; vs.bits = 16;
        auto out = runPhrase (in, vs, (long) (FS * 10));
        const double want = 441.0 * std::exp2 (semis / 12.0), got = freqOf (out, 44100, 20000);
        CHECK (std::fabs (1200 * std::log2 (got / want)) < 10, "pitch %+3.0f: %.1f Hz (want %.1f)", semis, got, want);
    }

    printf ("== 4. Overlay: each voice keeps the settings it started with\n");
    {
        std::vector<float> a (30000), b (30000);
        for (size_t k = 0; k < a.size(); ++k) { a[k] = 0.3f * (float) std::sin (k * 0.05); b[k] = 0.3f * (float) std::sin (k * 0.021); }
        VoiceSettings va; va.timePct = 600; va.hwRate = 44100; va.bits = 16;
        VoiceSettings vb; vb.model = CongoSampler::Model::S2000; vb.timePct = 150; vb.pitchSemis = 12; vb.hwRate = 44100; vb.bits = 16;
        auto render = [&] (bool useA, bool useB) {
            SustainEngine e; e.prepare (FS, 10.0); std::vector<float> o;
            for (long n = 0; n < 300000; ++n) {
                if (n == 0 && useA) e.startPhrase (va);
                if (n == 30000) e.endPhrase();
                if (n == 40000 && useB) e.startPhrase (vb);
                if (n == 70000) e.endPhrase();
                float in[2] = { n < 30000 ? a[(size_t) n] : (n >= 40000 && n < 70000 ? b[(size_t) (n - 40000)] : 0.0f), 0 }, out[2];
                in[1] = in[0]; e.processFrame (in, out); o.push_back (out[0]); }
            return o; };
        auto A = render (true, false), B = render (false, true), AB = render (true, true);
        double md = 0, ea = 0; for (size_t k = 0; k < AB.size(); ++k) { md = std::max (md, (double) std::fabs (AB[k] - A[k] - B[k])); ea += A[k] * A[k]; }
        long lastA = 0; for (long k = 0; k < (long) A.size(); ++k) if (std::fabs (A[(size_t) k]) > 1e-3f) lastA = k;
        CHECK (md < 1e-5 && lastA > 160000, "600%% phrase + a later 150%% +12 phrase = the two played separately (max diff %.1e); the 600%% one still runs to %.1f s", md, lastA / FS);
    }

    printf ("== 5. Voice limit, KILL\n");
    {
        SustainEngine e; e.prepare (FS, 10.0); VoiceSettings vs; vs.timePct = 2000; vs.hwRate = 44100; vs.bits = 16;
        int maxActive = 0;
        for (long n = 0; n < 44100 * 3; ++n) {
            if (n % 10000 == 0) e.startPhrase (vs);
            float in[2] = { 0.3f * (float) std::sin (n * 0.03), 0 }, out[2]; in[1] = in[0]; e.processFrame (in, out);
            maxActive = std::max (maxActive, e.activeVoices()); }
        CHECK (maxActive == 8, "13 overlapping phrases -> at most %d voices (oldest fade out)", maxActive);
        e.killAll ((int) (0.01 * FS)); float pk = 0; long n;
        for (n = 0; n < 441; ++n) { float in[2] = { 0, 0 }, out[2]; e.processFrame (in, out); }
        for (n = 0; n < 44100; ++n) { float in[2] = { 0, 0 }, out[2]; e.processFrame (in, out); pk = std::max (pk, std::fabs (out[0])); }
        CHECK (pk == 0.0f && e.activeVoices() == 0, "KILL: silent within 10 ms and every voice freed (peak after %.1e)", pk);
    }

    printf ("== 6. Squeeze (time < 100%%) waits for the phrase, then plays it faster\n");
    {
        std::vector<float> in (44100); for (size_t k = 0; k < in.size(); ++k) in[k] = 0.3f * (float) std::sin (k * 0.04);
        VoiceSettings vs; vs.model = CongoSampler::Model::S2000; vs.timePct = 50; vs.hwRate = 44100; vs.bits = 16;
        auto out = runPhrase (in, vs, (long) FS * 4);
        long first = -1, last = 0; for (long k = 0; k < (long) out.size(); ++k) if (std::fabs (out[(size_t) k]) > 1e-3f) { if (first < 0) first = k; last = k; }
        CHECK (first >= 44100 && std::fabs ((last - first) / FS - 0.5) < 0.06, "50%%: starts when the phrase ends (%.2f s), lasts %.2f s", first / FS, (last - first) / FS);
    }

    printf ("== 7. Stress: random overlapping phrases, settings, converter, kills\n");
    {
        SustainEngine e; e.prepare (FS, 10.0); fdsp::Rng r; bool bad = false; float pk = 0;
        for (long n = 0; n < (long) FS * 120; ++n) {
            if (n % 7919 == 0) {
                VoiceSettings vs; vs.model = r.next() > 0 ? CongoSampler::Model::S2000 : CongoSampler::Model::S950; vs.type = r.next() > 0 ? CongoSampler::Type::Intelligent : CongoSampler::Type::Cyclic;
                vs.timePct = 25.0 * std::pow (80.0, 0.5 + 0.5 * r.next()); vs.pitchSemis = 24.5 * r.next(); vs.cycle = (int) (5 * std::pow (2000.0, 0.5 + 0.5 * r.next()));
                vs.poly = r.next() > 0; vs.autoD = r.next() > 0.5f; vs.quality = (int) (10 + 10 * r.next()); vs.xfd = (int) (10 + 10 * r.next());
                vs.hwRate = 7500 + 40500 * (0.5 + 0.5 * r.next()); vs.bits = (int) (8.5 + 7.5 * r.next());
                if (r.next() > -0.6f) e.startPhrase (vs); else e.endPhrase(); }
            if (n % 400000 == 0) e.killAll (441);
            float in[2] = { 0.8f * r.next(), 0.8f * r.next() }, out[2]; e.processFrame (in, out);
            if (!std::isfinite (out[0]) || !std::isfinite (out[1])) bad = true;
            pk = std::max (pk, std::fabs (out[0])); }
        CHECK (!bad, "2 minutes of random overlapping voices: no NaN/Inf (peak of the voice sum %.2f)", pk);
    }

    printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "ALL SUSTAIN TESTS PASSED", failures);
    return failures ? 1 : 0;
}
