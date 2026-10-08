// Effects Board (EB16) + FREEZE tests. Build: g++ -std=c++17 -O2 -I../Source/dsp eb16_tests.cpp
#include "EB16.h"
#include "Freeze.h"
#include <chrono>
#include <cstdio>
#include <vector>
using namespace cba;
using namespace cba::eb16;
static int failures = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); printf("  [%s] ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++failures; } while (0)
static const double FS = 44100.0, PI_ = 3.14159265358979323846;

static std::vector<float> defaults() { std::vector<float> v; for (auto& s : specs()) v.push_back (s.def); return v; }
static int idx (const char* id) { auto& s = specs(); for (size_t i = 0; i < s.size(); ++i) if (s[i].id == id) return (int) i; printf ("no %s\n", id); return -1; }

// run a signal through a fresh board, return L
static std::vector<float> run (std::vector<float> v, const std::vector<float>& in, size_t total, std::vector<float>* rOut = nullptr)
{
    Board b; b.prepare (FS); std::vector<float> o (total);
    if (rOut) rOut->assign (total, 0.0f);
    for (size_t n = 0; n < total; ++n) { float L = n < in.size() ? in[n] : 0.0f, R = L; b.processFrame (v.data(), L, R); o[n] = L; if (rOut) (*rOut)[n] = R; }
    return o;
}
static double rmsDb (const std::vector<float>& y, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += y[i] * y[i]; return 10 * std::log10 (e / (double) (b - a) + 1e-30); }
static double sineGainDb (std::vector<float> v, double f) {
    std::vector<float> in (44100); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.1f * (float) std::sin (2 * PI_ * f * (double) i / FS);
    auto o = run (v, in, in.size()); return rmsDb (o, 22050, 44100) - rmsDb (in, 22050, 44100); }

int main()
{
    printf ("== spec table\n");
    { auto& s = specs(); bool ok = true; for (size_t i = 0; i < s.size(); ++i) { if (s[i].id.empty() || s[i].def < s[i].lo || s[i].def > s[i].hi) ok = false;
        for (size_t j = i + 1; j < s.size(); ++j) if (s[i].id == s[j].id) ok = false; }
      CHECK (ok && (int) s.size() == TOTAL, "%d Effects Board controls, unique ids, every default inside its range", TOTAL); }

    printf ("== defaults are transparent\n");
    { auto v = defaults(); std::vector<float> in (20000); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin (i * 0.01);
      auto o = run (v, in, in.size()); double md = 0; for (size_t i = 0; i < in.size(); ++i) md = std::max (md, (double) std::abs (o[i] - in[i]));
      CHECK (md < 1e-6, "all four units off (default): output = input (max diff %.1e)", md); }

    printf ("== 4-band EQ (FX1, send 99, mod/echo/reverb off, mix: DIST/EQ only)\n");
    auto eqBase = [] () { auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_mix")] = 100; v[idx ("fx1_eqOn")] = 1;
                          v[idx ("fx1_modLevel")] = 0; v[idx ("fx1_echoLevel")] = 0; return v; };
    { auto v = eqBase(); v[idx ("fx1_lowHz")] = 15; v[idx ("fx1_highHz")] = 15; v[idx ("fx1_bp1Db")] = -37; v[idx ("fx1_bp2Db")] = -37;
      double lo = 99, hi = -99; for (double f : {50.0, 200.0, 500.0, 1000.0, 3000.0, 10000.0}) { double g = sineGainDb (v, f); lo = std::min (lo, g); hi = std::max (hi, g); }
      CHECK (hi - lo < 0.2, "MPC 'zero setting' (LOW 500, HIGH 500, BPs off) is flat: %.2f..%+.2f dB", lo, hi); }
    { auto v = eqBase(); double lo = 99, hi = -99;
      for (double f : {50.0, 120.0, 300.0, 700.0, 1500.0, 3000.0, 6000.0, 12000.0}) { double g = sineGainDb (v, f); lo = std::min (lo, g); hi = std::max (hi, g); }
      printf ("  [INFO] ROM screen defaults (LOW 500, BP1 1k0, BP2 3k2, HIGH 6k3, all +00, WID 99): %.1f..%+.1f dB\n", lo, hi); }
    { auto v = eqBase(); v[idx ("fx1_lowDb")] = -37; v[idx ("fx1_highDb")] = -37; v[idx ("fx1_bp2Db")] = -37; v[idx ("fx1_bp1Hz")] = 18; v[idx ("fx1_bp1Wid")] = 20;
      const double at = sineGainDb (v, 1000), off = sineGainDb (v, 4000);
      CHECK (at > -1.5 && off < -12, "pure band-pass (BP1 1k0, others muted): %+.1f dB at 1 kHz, %+.1f dB at 4 kHz", at, off); }

    printf ("== echo\n");
    { auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_mix")] = 100; v[idx ("fx1_distLevel")] = 0; v[idx ("fx1_echoOn")] = 1; v[idx ("fx1_echoLevel")] = 99; v[idx ("fx1_echoMode")] = MonoLR; v[idx ("fx1_echoDelay")] = 250; v[idx ("fx1_echoFbk")] = 50;
      std::vector<float> in (10, 0.0f); in[0] = 0.8f; auto o = run (v, in, 44100);
      size_t pk = 0; for (size_t i = 1000; i < 20000; ++i) if (std::abs (o[i]) > std::abs (o[pk])) pk = i;
      CHECK (std::abs ((double) pk / FS - 0.25) < 0.002, "MONO L+R 250 ms: first repeat at %.1f ms", 1000.0 * pk / FS);
      v[idx ("fx1_echoFbk")] = 99; v[idx ("fx1_echoDamp")] = 30; std::vector<float> noise (44100); for (size_t i = 0; i < noise.size(); ++i) noise[i] = 0.9f * (float) std::sin (i * 0.37) * (float) std::sin (i * 0.0011);
      auto o2 = run (v, noise, (size_t) FS * 30); float pk2 = 0; for (float x : o2) pk2 = std::max (pk2, std::abs (x));
      CHECK (pk2 < 4.0f && std::isfinite (pk2), "feedback 99, no damping, 30 s: bounded by 24-bit memory saturation (peak %.2f)", pk2); }

    printf ("== reverbs\n");
    for (int type : {LargeHall, SmallHall, LargeRoom, SmallRoom}) for (int dec : {30, 80}) {
        auto v = defaults(); v[idx ("rv1_on")] = 1; v[idx ("rv1_mix")] = 100; v[idx ("rv1_type")] = (float) type; v[idx ("rv1_decay")] = (float) dec;
        v[idx ("rv1_lf")] = 0; v[idx ("rv1_hf")] = 30;
        std::vector<float> in (2205); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin (i * 0.11) * (float) std::sin (i * 0.013);   // ~0.5-1 kHz
        auto o = run (v, in, (size_t) FS * 25);
        // RT60 from the energy decay: time from -5 dB to -25 dB, x3
        std::vector<double> edc (o.size() + 1, 0.0); for (size_t i = o.size(); i-- > 0;) edc[i] = edc[i + 1] + o[i] * o[i];
        size_t a = 0, b = 0; for (size_t i = 2205; i < o.size(); ++i) { double d = 10 * std::log10 (edc[i] / edc[2205] + 1e-30); if (!a && d < -5) a = i; if (!b && d < -25) { b = i; break; } }
        const double rt = 3.0 * (double) (b - a) / FS;
        const bool hall = type <= SmallHall; const double rtMax = hall ? 20.0 : 8.0, want = 0.2 * std::pow (rtMax / 0.2, dec / 99.0);
        CHECK (b > a && std::abs (rt / want - 1.0) < 0.35, "%-10s decay %d: RT60 %.2f s (set %.2f s)", revTypeNames()[(size_t) type].c_str(), dec, rt, want); }
    { auto v = defaults(); v[idx ("rv1_on")] = 1; v[idx ("rv1_mix")] = 100; v[idx ("rv1_decay")] = 99; v[idx ("rv1_hf")] = 30; v[idx ("rv1_lf")] = 0;
      std::vector<float> in ((size_t) FS * 5); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.9f * (float) std::sin (i * 0.05);
      auto o = run (v, in, (size_t) FS * 60); float pk = 0; for (float x : o) pk = std::max (pk, std::abs (x));
      CHECK (std::isfinite (pk) && pk < 4.0f, "LARGE HALL decay 99 fed loud for 5 s, 60 s run: stable (peak %.2f)", pk); }
    for (int type : {Gated1, Gated2}) {
      auto v = defaults(); v[idx ("rv1_on")] = 1; v[idx ("rv1_mix")] = 100; v[idx ("rv1_type")] = (float) type; v[idx ("rv1_time")] = 30; v[idx ("rv1_predelay")] = 0;
      std::vector<float> in (4410); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin (i * 1.3) * (float) std::sin (i * 0.21);
      auto o = run (v, in, (size_t) FS * 2);
      const double during = rmsDb (o, 4410, 8000), after = rmsDb (o, (size_t) (0.75 * FS), (size_t) (1.0 * FS));
      CHECK (during - after > 40, "%s: dense while open (%.0f dB), cut after the gate time (%.0f dB)", revTypeNames()[(size_t) type].c_str(), during, after); }
    { auto v = defaults(); v[idx ("rv1_on")] = 1; v[idx ("rv1_mix")] = 100; v[idx ("rv1_type")] = Reverse; v[idx ("rv1_time")] = 50; v[idx ("rv1_predelay")] = 0;
      std::vector<float> in (10, 0.0f); in[0] = 0.9f; auto o = run (v, in, (size_t) FS);
      const double early = rmsDb (o, 100, 3000), late = rmsDb (o, 9000, 11500), after = rmsDb (o, 20000, 30000);
      CHECK (late - early > 8 && after < -100, "REVERSE: swells (%.0f dB -> %.0f dB) then stops dead (%.0f dB)", early, late, after); }

    printf ("== modulation\n");
    { auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_mix")] = 100; v[idx ("fx1_distLevel")] = 0; v[idx ("fx1_modOn")] = 1; v[idx ("fx1_modType")] = PitchShift; v[idx ("fx1_tuneL")] = 12; v[idx ("fx1_tuneR")] = 12;
      std::vector<float> in ((size_t) FS); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.4f * (float) std::sin (2 * PI_ * 233.0 * (double) i / FS);
      auto o = run (v, in, in.size());
      auto band = [&] (double lo, double hi) { double e = 0; for (double f = lo; f < hi; f += 2.0) { double re = 0, im = 0;
          for (size_t n = 20000; n < 40000; ++n) { re += o[n] * std::cos (2 * PI_ * f * (double) n / FS); im += o[n] * std::sin (2 * PI_ * f * (double) n / FS); } e += re * re + im * im; } return e; };
      const double inBand = band (436, 496), all = band (100, 1200);
      CHECK (inBand / all > 0.8, "PITCH SHIFT +12: 233 Hz in -> %.0f%% of the output energy within 466 +- 30 Hz (window sidebands included)", 100 * inBand / all); }
    { const char* names[] = { "PHASE", "FLANGE", "CHORUS", "ROTARY", "FMOD/AUTOPAN", "PITCH + FBK" };
      for (int k = 0; k < 6; ++k) {
        auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_mix")] = 100; v[idx ("fx1_modOn")] = 1; v[idx ("fx1_modDepth")] = 99; v[idx ("fx1_modFbk")] = 99; v[idx ("fx1_fmDepth")] = 99; v[idx ("fx1_fmFbk")] = 99;
        v[idx ("fx1_fbL")] = 99; v[idx ("fx1_fbR")] = 99; v[idx ("fx1_tuneL")] = 7; v[idx ("fx1_tuneR")] = -5;
        v[idx ("fx1_modType")] = (float) (k < 3 ? PhaFlaChor : (k == 3 ? Rotary : (k == 4 ? FmodApan : PitchFbk))); v[idx ("fx1_pfcType")] = (float) (k < 3 ? k : 0);
        std::vector<float> in ((size_t) FS * 4); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.7f * (float) std::sin (i * 0.02) * (float) std::sin (i * 0.0003);
        std::vector<float> r; auto o = run (v, in, in.size(), &r); float pk = 0; double diff = 0;
        for (size_t i = 0; i < o.size(); ++i) { pk = std::max (pk, std::abs (o[i])); diff += std::abs (o[i] - r[i]); }
        CHECK (std::isfinite (pk) && pk < 6.0f && diff > 1.0, "%-13s at full depth + feedback: stable (peak %.2f), stereo", names[k], pk); } }

    printf ("== series ORDER, WET/DRY, ON\n");
    { // distortion before vs after a reverb gives a different result; order names read correctly
      auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_mix")] = 100; v[idx ("fx1_distOn")] = 1; v[idx ("fx1_distDepth")] = 99;
      v[idx ("rv1_on")] = 1; v[idx ("rv1_mix")] = 100;
      std::vector<float> in (4410); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.3f * (float) std::sin (i * 0.05);
      int oA = -1, oB = -1;
      for (size_t k = 0; k < orders().size(); ++k) { if (orderName ((int) k) == "FX1 - FX2 - RV1 - RV2") oA = (int) k; if (orderName ((int) k) == "RV1 - FX1 - FX2 - RV2") oB = (int) k; }
      v[idx ("fx_order")] = (float) oA; auto a = run (v, in, 44100);
      v[idx ("fx_order")] = (float) oB; auto b = run (v, in, 44100);
      double diff = 0, ea = 0; for (size_t i = 0; i < a.size(); ++i) { diff += (a[i] - b[i]) * (a[i] - b[i]); ea += a[i] * a[i]; }
      CHECK (orders().size() == 24 && oA == 0 && oB >= 0 && diff > 0.1 * ea,
             "24 orders, default '%s'; 'FX1 - ..' vs 'RV1 - FX1 - ..' sound different (%.0f%% energy difference)", orderName (0).c_str(), 100 * diff / ea); }
    { // WET/DRY 50 % on a unit = exact half way between dry and fully wet
      auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_distOn")] = 1; v[idx ("fx1_distDepth")] = 60;
      std::vector<float> in (8000); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.4f * (float) std::sin (i * 0.03);
      v[idx ("fx1_mix")] = 100; auto w = run (v, in, in.size());
      v[idx ("fx1_mix")] = 50;  auto h = run (v, in, in.size());
      double md = 0; for (size_t i = 0; i < in.size(); ++i) md = std::max (md, (double) std::abs (h[i] - 0.5f * (in[i] + w[i])));
      CHECK (md < 1e-5, "WET/DRY 50%% = half dry + half wet (max diff %.1e)", md); }
    { // ON fades in 10 ms (no click: never a bigger jump than the effect itself makes) and off = untouched
      auto v = defaults(); v[idx ("fx1_mix")] = 100; v[idx ("fx1_distOn")] = 1; v[idx ("fx1_distDepth")] = 99;
      auto steps = [&] (bool switchOn, bool& untouched) {
          Board b; b.prepare (FS); float maxStep = 0, prev = 0; untouched = true;
          for (long n = 0; n < 44100; ++n) { v[idx ("fx1_on")] = (! switchOn || n >= 22050) ? 1.0f : 0.0f;
              float L = 0.2f * (float) std::sin (n * 0.002), R = L; const float x = L; b.processFrame (v.data(), L, R);
              if (switchOn && n < 22050 && L != x) untouched = false;
              if (n > 100) maxStep = std::max (maxStep, std::abs (L - prev));
              prev = L; }
          return maxStep; };
      bool u1, u2; const float alwaysOn = steps (false, u1), switching = steps (true, u2);
      CHECK (u2 && switching <= alwaysOn * 1.02f, "off = untouched; switching ON never jumps more than the effect itself (%.4f vs %.4f)", switching, alwaysOn); }


    printf ("== tempo sync\n");
    { auto firstRepeat = [&] (double bpm, int div) {
          auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_mix")] = 100; v[idx ("fx1_distLevel")] = 0; v[idx ("fx1_echoOn")] = 1; v[idx ("fx1_echoLevel")] = 99;
          v[idx ("fx1_echoMode")] = MonoLR; v[idx ("fx1_echoSync")] = 1; v[idx ("fx1_echoDiv")] = (float) div; v[idx ("fx1_echoFbk")] = 0;
          Board b; b.prepare (FS); b.setTempo (bpm, 0.0); std::vector<float> o;
          for (long n = 0; n < (long) FS * 2; ++n) { float L = n == 0 ? 0.8f : 0.0f, R = L; b.processFrame (v.data(), L, R); o.push_back (L); }
          size_t pk = 1; for (size_t i = 1; i < o.size(); ++i) if (std::abs (o[i]) > std::abs (o[pk])) pk = i;
          return 1000.0 * (double) pk / FS; };
      const double a = firstRepeat (120, 7), b2 = firstRepeat (60, 10);
      CHECK (std::abs (a - 250.0) < 0.2 && std::abs (b2 - 1000.0) < 0.2, "ECHO SYNC: 1/8 at 120 BPM repeats at %.1f ms; 1/4 at 60 BPM at %.1f ms (past the EB16's 999 ms)", a, b2); }
    { auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_eqOn")] = 1; v[idx ("fx1_eqSync")] = 1; v[idx ("fx1_bp1Div")] = 11; // 1 bar
      Board b; b.prepare (FS); b.setTempo (120.0, 7.0);                   // a bar = 4 beats; beat 7 is 3/4 through bar 2
      float L = 0, R = 0; b.processFrame (v.data(), L, R);
      const float atBeat7 = b.fmodPhase (0, 0);
      b.setTempo (120.0, 8.0); b.processFrame (v.data(), L, R);
      CHECK (std::abs (atBeat7 - 0.75f) < 1e-3f && std::abs (b.fmodPhase (0, 0)) < 1e-3f && std::abs (b.effectiveValue (FX1 + Bp1Rate) - 0.5f) < 1e-4f,
             "F-MOD SYNC: one sweep per bar (0.50 Hz at 120 BPM), locked to the bar - 3/4 through at beat 7, restarting on the downbeat"); }
    { auto v = defaults(); v[idx ("rv1_on")] = 1; v[idx ("rv1_mix")] = 100; v[idx ("rv1_type")] = Reverse; v[idx ("rv1_predelay")] = 0;
      v[idx ("rv1_sync")] = 1; v[idx ("rv1_timeDiv")] = 10;              // 1/4 = 500 ms at 120
      Board b; b.prepare (FS); b.setTempo (120.0, 0.0); std::vector<float> o;
      for (long n = 0; n < (long) FS; ++n) { float L = n == 0 ? 0.9f : 0.0f, R = L; b.processFrame (v.data(), L, R); o.push_back (L); }
      size_t last = 0; for (size_t i = 0; i < o.size(); ++i) if (std::abs (o[i]) > 1e-4f) last = i;
      const double ends = 1000.0 * (double) last / FS;                   // synced PREDELAY 1/64 = 31.25 ms, then 500 ms
      CHECK (std::abs (ends - 531.25) < 15.0, "REVERSE + SYNC at 120 BPM: PREDELAY 1/64 (31 ms) then a 1/4 swell (500 ms) - ends at %.0f ms", ends); }
    { auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_modOn")] = 1; v[idx ("fx1_modType")] = Rotary; v[idx ("fx1_rotSync")] = 1;
      v[idx ("fx1_rot1Div")] = 9; v[idx ("fx1_rot2Div")] = 2;             // 1/2 and 1/16
      Board b; b.prepare (FS); b.setTempo (120.0, 0.0); float L = 0, R = 0; b.processFrame (v.data(), L, R);
      CHECK (std::abs (b.effectiveValue (FX1 + RotSpeed1) - 1.0f) < 1e-4f && std::abs (b.effectiveValue (FX1 + RotSpeed2) - 8.0f) < 1e-4f,
             "ROT SYNC: SPEED1 1/2 = 1 turn a second, SPEED2 1/16 = 8 turns a second, at 120 BPM"); }
    { auto v = defaults(); v[idx ("fx1_on")] = 1; v[idx ("fx1_mix")] = 100; v[idx ("fx1_distLevel")] = 0; v[idx ("fx1_echoOn")] = 1; v[idx ("fx1_echoMode")] = Stereo;
      std::vector<float> in (4410, 0.0f); in[0] = 0.8f; auto o = run (v, in, 44100);
      float pk = 0; for (float x : o) pk = std::max (pk, std::abs (x));
      CHECK (pk == 0.0f && v[idx ("fx1_echoLevel")] == 0.0f, "ECHO MIX starts down: switching ECHO on is silent until MIX comes up"); }

    printf ("== FREEZE\n");
    { Freeze fz; fz.prepare (FS); std::vector<float> o;
      for (long n = 0; n < (long) FS * 2; ++n) { if (n == (long) FS) { fz.setSize (441.0); fz.setFrozen (true); }
        float L = 0.5f * (float) std::sin (n * 0.0731), R = L; fz.process (L, R); o.push_back (L); }
      // frozen 441-sample loop repeats every 10 ms -> the output is periodic with period 441
      double md = 0, step = 0; for (size_t i = (size_t) FS + 2000; i < o.size() - 441; ++i) { md = std::max (md, (double) std::abs (o[i] - o[i + 441])); step = std::max (step, (double) std::abs (o[i] - o[i - 1])); }
      CHECK (md < 1e-4 && step < 0.05, "10 ms freeze loops exactly every 441 samples (diff %.1e), no seam click (max step %.3f)", md, step); }
    { Freeze fz; fz.prepare (FS); float pk = 0; bool bad = false;
      unsigned s = 1; auto rnd = [&s] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; };
      for (long n = 0; n < (long) FS * 60; ++n) { if (n % 3001 == 0) fz.setFrozen (rnd() > 0); if (n % 997 == 0) fz.setSize (FS * 2.0 * (0.5 + 0.5 * rnd()) + 2);
        float L = 0.8f * rnd(), R = 0.8f * rnd(); fz.process (L, R); if (!std::isfinite (L)) bad = true; pk = std::max (pk, std::abs (L)); }
      CHECK (!bad && pk <= 0.81f, "60 s of random freeze on/off and size changes: clean (peak %.2f)", pk); }

    printf ("== CPU: everything on (FX1 + FX2 with all blocks, RV1 + RV2), stereo 44.1 kHz\n");
    { auto v = defaults();
      for (const char* id : {"fx1_on","fx2_on","rv1_on","rv2_on","fx1_distOn","fx1_eqOn","fx1_modOn","fx1_echoOn",
                             "fx2_distOn","fx2_eqOn","fx2_modOn","fx2_echoOn"}) v[idx (id)] = 1;
      v[idx ("fx1_modType")] = PhaFlaChor; v[idx ("fx2_modType")] = Rotary; v[idx ("fx1_echoMode")] = Stereo; v[idx ("fx2_echoMode")] = XoverLR;
      Board b; b.prepare (FS); auto t0 = std::chrono::steady_clock::now(); bool bad = false;
      for (long n = 0; n < (long) FS * 20; ++n) { float L = 0.3f * (float) std::sin (n * 0.01), R = L; b.processFrame (v.data(), L, R); if (!std::isfinite (L)) bad = true; }
      const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
      CHECK (!bad, "worst case runs clean: %.1f%% of one core", 100 * sec / 20.0); }

    printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "ALL EFFECTS BOARD TESTS PASSED", failures);
    return failures ? 1 : 0;
}
