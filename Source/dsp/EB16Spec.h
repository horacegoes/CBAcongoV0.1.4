#pragma once
// =============================================================================
// EB16Spec.h  -  every Effects Board control (the Akai EB16), in one table shared by the
// DSP, the plugin parameters and the front panel.
//
// Plugin layout: one signal runs in series through four units - FX1, FX2, RV1, RV2 - in the
// order chosen by ORDER (default FX1 - FX2 - RV1 - RV2). Each unit has ON and WET/DRY.
// (On the hardware these are FX1, FX2, RV3, RV4 fed by sends; a plugin insert has one
// signal, so the sends became the series ORDER.)
//
// Taken from the S2000 OS 2.00 ROM's EB16 pages (names, units, value formats and the
// example values on each screen) plus the EB16 / MPC2000XL notes:
//   FX1, FX2 : RINGMOD/DISTORT -> 4-BAND EQUALISE (+ F-MOD on BP1/BP2) -> MOD. FUNCTION
//              (PHA/FLA/CHOR, ROTARY SPKRS, FMOD/AUTOPAN, PITCH SHIFT, PITCH + FBK)
//              -> ECHO (MONO LEFT, MONO L+R, XOVER L&R, STEREO), mixer. (The hardware's
//              per-FX reverb and its PATH CONTROL are left out: RV1 / RV2 + ORDER cover it.)
//   RV1, RV2 : the board's two dedicated reverbs (RV3 / RV4 on the hardware).
//   Frequencies step in ISO 1/3 octaves (the ROM shows 500, 1k0, 3k2, 6k3, 4k0Hz ...).
//   Rates 0.0-9.9 Hz, levels / depths 0-99, PFC feedback -99..+99, pans L50-MID-R50,
//   delays in ms (3 digits), rotary acceleration in seconds, ring mod up to 5000 Hz.
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace cba
{
namespace eb16
{
    enum class Kind { Knob, Choice, Toggle };
    enum class Fmt  { Int, Hz01, IsoHz, Ms, Db37, Pan, Semis, Sec, Signed, RmHz, Percent, DelayDiv, RateDiv };

    struct Spec
    {
        std::string id, name, page;      // page: "FX1/DIST/EQ" ... "RV1", "RV2", or "UNITS"
        Kind  kind = Kind::Knob;
        float lo = 0, hi = 99, step = 1, def = 0;
        Fmt   fmt = Fmt::Int;
        std::vector<std::string> choices;
        std::string label;               // short knob label
        bool newRow = false;             // panel: this knob starts a new row
        int  twinOf = -1;                // a note-value knob: shown in this (free) knob's place...
        int  syncBy = -1;                // ...while this SYNC button is on (both absolute indices)
    };

    // ISO third-octave centres (the ROM's display steps)
    inline const std::vector<float>& iso()
    {
        static const std::vector<float> t { 16, 20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250,
                                            315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150,
                                            4000, 5000, 6300, 8000, 10000, 12500, 16000 };
        return t;
    }
    inline float isoHz (float index)
    {
        const int i = (int) std::lround (index);
        return iso()[(size_t) (i < 0 ? 0 : (i > 30 ? 30 : i))];
    }
    inline std::string isoText (float index)
    {
        static const char* n[] = { "16", "20", "25", "31", "40", "50", "63", "80", "100", "125", "160", "200",
                                   "250", "315", "400", "500", "630", "800", "1k0", "1k25", "1k6", "2k0", "2k5",
                                   "3k2", "4k0", "5k0", "6k3", "8k0", "10k", "12k5", "16k" };
        const int i = (int) std::lround (index);
        return std::string (n[i < 0 ? 0 : (i > 30 ? 30 : i)]) + "Hz";
    }

    inline float level (float v)   { return v <= 0.0f ? 0.0f : (v / 99.0f) * (v / 99.0f); }   // 99 = unity
    inline float db37 (float db)   { return db <= -37.0f ? 0.0f : std::pow (10.0f, db / 20.0f); } // -37 = off
    inline float rmHz (float v)    { return 5000.0f * (v / 99.0f) * (v / 99.0f); }

    // ---- parameter indices --------------------------------------------------------------
    enum FxP
    {
        FxOn, FxMix,
        DistOn, EqOn, RmFreq, RmDepth, DistDepth, DistAtten,
        LowHz, LowDb, Bp1Hz, Bp1Db, Bp1Wid, Bp2Hz, Bp2Db, Bp2Wid, HighHz, HighDb,
        Bp1Rate, Bp1Depth, Bp2Rate, Bp2Depth,
        ModOn, ModType, PfcType, ApanMode, RotFast, TuneLink,
        ModRate, ModDepth, ModFbk, RotSpeed1, RotSpeed2, RotAccel, RotDepth,
        ApanRate, ApanDepth, FmRate, FmDepth, FmFbk, TuneL, TuneR, DelL, DelR, FbL, FbR,
        EchoOn, EchoMode, EchoLevel, EchoDelay, EchoDamp, EchoFbk, EchoOffs, LDelay, LDamp, LFbk, RDelay, RDamp, RFbk,
        DistLevel, DistPan, ModLevel, ModPan, ModWidth, EchoPan, EchoWidth,
        // tempo sync: a SYNC button per page, and a note-value twin for each time control
        EqSync, ModSync, RotSync, EchoSync,
        Bp1Div, Bp2Div, ModDiv, ApanDiv, FmDiv, DelLDiv, DelRDiv, Rot1Div, Rot2Div, EchoDiv, LDiv, RDiv,
        FxCount
    };
    enum RvP { RvOn, RvMix, RvType, RvPredelay, RvDecay, RvLF, RvHF, RvDiffuse, RvNear, RvTime,
               RvLevel, RvPan, RvSync, RvPreDiv, RvTimeDiv, RvCount };

    // ---- tempo-sync note values ------------------------------------------------------------
    struct Div { const char* name; double quarters; };
    inline const std::vector<Div>& delayDivs()      // a time: echoes, pre-delays, gate / reverse length
    {
        static const std::vector<Div> d {
            { "1/64", 0.0625 }, { "1/32T", 1.0 / 12.0 }, { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 },
            { "1/16D", 0.375 }, { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 },
            { "1/4", 1.0 }, { "1/4D", 1.5 }, { "1/2T", 4.0 / 3.0 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1 BAR", 4.0 } };
        return d;
    }
    inline const std::vector<Div>& rateDivs()       // a speed: one sweep / turn per note value
    {
        static const std::vector<Div> d {
            { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 },
            { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/2D", 3.0 },
            { "1 BAR", 4.0 }, { "2 BARS", 8.0 }, { "4 BARS", 16.0 }, { "8 BARS", 32.0 } };
        return d;
    }
    inline double divQuarters (const std::vector<Div>& d, float index)
    {
        const int i = (int) std::lround (index);
        return d[(size_t) (i < 0 ? 0 : (i >= (int) d.size() ? (int) d.size() - 1 : i))].quarters;
    }
    enum GP  { Order, GCount };

    constexpr int FX1 = 0, FX2 = FxCount, RV1 = 2 * FxCount, RV2 = RV1 + RvCount,
                  GLOB = RV2 + RvCount, TOTAL = GLOB + GCount;

    enum ModTypes  { PhaFlaChor, Rotary, FmodApan, PitchShift, PitchFbk };
    enum PfcTypes  { Phase, Flange, Chorus };
    enum ApanModes { Pan, LtoR, RtoL, Trem };
    enum EchoModes { MonoLeft, MonoLR, XoverLR, Stereo };

    // the 24 series orders of the four units, written the way they sound
    enum Unit { UFX1, UFX2, URV1, URV2 };
    inline const std::vector<std::array<int, 4>>& orders()
    {
        static const std::vector<std::array<int, 4>> o = []
        {
            std::vector<std::array<int, 4>> v;
            std::array<int, 4> a { UFX1, UFX2, URV1, URV2 };
            do v.push_back (a); while (std::next_permutation (a.begin(), a.end()));
            return v;                                    // first entry: FX1 - FX2 - RV1 - RV2
        }();
        return o;
    }
    inline std::string orderName (int i)
    {
        static const char* n[] = { "FX1", "FX2", "RV1", "RV2" };
        const auto& o = orders()[(size_t) i];
        return std::string (n[o[0]]) + " - " + n[o[1]] + " - " + n[o[2]] + " - " + n[o[3]];
    }
    enum RevTypes  { LargeHall, SmallHall, LargeRoom, SmallRoom, Gated1, Gated2, Reverse };

    inline const std::vector<std::string>& revTypeNames()
    {
        static const std::vector<std::string> n { "LARGE HALL", "SMALL HALL", "LARGE ROOM", "SMALL ROOM",
                                                  "GATED 1", "GATED 2", "REVERSE" };
        return n;
    }

    // ---- the table ---------------------------------------------------------------------
    inline std::vector<Spec> buildSpecs()
    {
        std::vector<Spec> s ((size_t) TOTAL);
        auto K = [&s] (int i, const std::string& id, const std::string& name, const std::string& page,
                       const std::string& label, float lo, float hi, float step, float def, Fmt f)
        {
            Spec& x = s[(size_t) i];
            x.id = id; x.name = name; x.page = page; x.label = label; x.kind = Kind::Knob;
            x.lo = lo; x.hi = hi; x.step = step; x.def = def; x.fmt = f;
        };
        auto C = [&s] (int i, const std::string& id, const std::string& name, const std::string& page,
                       const std::string& label, std::vector<std::string> ch, int def)
        {
            Spec& x = s[(size_t) i];
            x.id = id; x.name = name; x.page = page; x.label = label; x.kind = Kind::Choice;
            x.lo = 0; x.hi = (float) ch.size() - 1; x.step = 1; x.def = (float) def; x.choices = std::move (ch);
        };
        auto T = [&s] (int i, const std::string& id, const std::string& name, const std::string& page,
                       const std::string& label, bool def)
        {
            Spec& x = s[(size_t) i];
            x.id = id; x.name = name; x.page = page; x.label = label; x.kind = Kind::Toggle;
            x.lo = 0; x.hi = 1; x.step = 1; x.def = def ? 1.0f : 0.0f;
        };

        const std::vector<std::string> revTypes = revTypeNames();

        for (int n = 0; n < 2; ++n)
        {
            const int b = n == 0 ? FX1 : FX2;
            const std::string p = n == 0 ? "fx1_" : "fx2_", P = n == 0 ? "FX1 " : "FX2 ";
            const std::string pg = n == 0 ? "FX1" : "FX2";
            const std::string dist = pg + "/DIST/EQ", mod = pg + "/MOD", ech = pg + "/ECHO", mix = pg + "/MIX";

            T (b + FxOn,  p + "on",  P + "On", "UNITS", "ON", false);
            K (b + FxMix, p + "mix", P + "Wet/Dry", "UNITS", "WET", 0, 100, 1, 100, Fmt::Percent);

            T (b + DistOn, p + "distOn", P + "Distort On", dist, "DIST", false);
            T (b + EqOn,   p + "eqOn",   P + "EQ On",      dist, "EQ",   false);
            K (b + RmFreq,    p + "rmFreq",    P + "Ring Mod Freq",  dist, "RM FREQ",  0, 99, 1, 0, Fmt::RmHz);
            K (b + RmDepth,   p + "rmDepth",   P + "Ring Mod Depth", dist, "RM DEPTH", 0, 99, 1, 0, Fmt::Int);
            K (b + DistDepth, p + "distDepth", P + "Dist Depth",     dist, "DIST",     0, 99, 1, 0, Fmt::Int);
            K (b + DistAtten, p + "distAtten", P + "Dist Atten",     dist, "ATTEN",    0, 99, 1, 0, Fmt::Int);
            K (b + LowHz,  p + "lowHz",  P + "EQ Low Freq",  dist, "LOW",     0, 15, 1, 15, Fmt::IsoHz);
            K (b + LowDb,  p + "lowDb",  P + "EQ Low dB",    dist, "LOW dB", -37, 12, 1, 0, Fmt::Db37);
            K (b + Bp1Hz,  p + "bp1Hz",  P + "EQ BP1 Freq",  dist, "BP1",     4, 26, 1, 18, Fmt::IsoHz);
            K (b + Bp1Db,  p + "bp1Db",  P + "EQ BP1 dB",    dist, "BP1 dB", -37, 12, 1, 0, Fmt::Db37);
            K (b + Bp1Wid, p + "bp1Wid", P + "EQ BP1 Width", dist, "BP1 WID", 0, 99, 1, 99, Fmt::Int);
            K (b + Bp2Hz,  p + "bp2Hz",  P + "EQ BP2 Freq",  dist, "BP2",     4, 26, 1, 23, Fmt::IsoHz);
            K (b + Bp2Db,  p + "bp2Db",  P + "EQ BP2 dB",    dist, "BP2 dB", -37, 12, 1, 0, Fmt::Db37);
            K (b + Bp2Wid, p + "bp2Wid", P + "EQ BP2 Width", dist, "BP2 WID", 0, 99, 1, 99, Fmt::Int);
            K (b + HighHz, p + "highHz", P + "EQ High Freq", dist, "HIGH",   15, 30, 1, 26, Fmt::IsoHz);
            K (b + HighDb, p + "highDb", P + "EQ High dB",   dist, "HIGH dB",-37, 12, 1, 0, Fmt::Db37);
            K (b + Bp1Rate,  p + "bp1Rate",  P + "BP1 F-MOD Rate",  dist, "BP1 RATE", 0, 9.9f, 0.1f, 0.5f, Fmt::Hz01);
            K (b + Bp1Depth, p + "bp1Depth", P + "BP1 F-MOD Depth", dist, "BP1 DEP",  0, 99, 1, 0, Fmt::Int);
            K (b + Bp2Rate,  p + "bp2Rate",  P + "BP2 F-MOD Rate",  dist, "BP2 RATE", 0, 9.9f, 0.1f, 1.2f, Fmt::Hz01);
            K (b + Bp2Depth, p + "bp2Depth", P + "BP2 F-MOD Depth", dist, "BP2 DEP",  0, 99, 1, 0, Fmt::Int);

            T (b + ModOn,    p + "modOn",    P + "Mod On", mod, "MOD", false);
            C (b + ModType,  p + "modType",  P + "Mod Function", mod, "FUNCTION",
               { "PHA/FLA/CHOR", "ROTARY SPKRS", "FMOD/AUTOPAN", "PITCH SHIFT", "PITCH + FBK" }, PhaFlaChor);
            C (b + PfcType,  p + "pfcType",  P + "PFC Type", mod, "PFC", { "PHASE", "FLANGE", "CHORUS" }, Chorus);
            C (b + ApanMode, p + "apanMode", P + "Auto Pan Mode", mod, "A.PAN", { "PAN", "L>R", "R>L", "TREM" }, Pan);
            T (b + RotFast,  p + "rotFast",  P + "Rotary Speed 2", mod, "SPEED 2", false);
            T (b + TuneLink, p + "tuneLink", P + "Pitch Tune Link", mod, "TUNE LINK", false);
            K (b + ModRate,  p + "modRate",  P + "PFC Rate",  mod, "RATE",  0, 9.9f, 0.1f, 0.8f, Fmt::Hz01);
            K (b + ModDepth, p + "modDepth", P + "PFC Depth", mod, "DEPTH", 0, 99, 1, 50, Fmt::Int);
            K (b + ModFbk,   p + "modFbk",   P + "PFC Feedback", mod, "FBK", -99, 99, 1, 0, Fmt::Signed);
            K (b + RotSpeed1, p + "rotSpeed1", P + "Rotary Speed1", mod, "SPEED1", 0, 9.9f, 0.1f, 0.7f, Fmt::Hz01);
            K (b + RotSpeed2, p + "rotSpeed2", P + "Rotary Speed2", mod, "SPEED2", 0, 9.9f, 0.1f, 6.3f, Fmt::Hz01);
            K (b + RotAccel,  p + "rotAccel",  P + "Rotary Accel",  mod, "ACCEL", 0.1f, 9.9f, 0.1f, 2.0f, Fmt::Sec);
            K (b + RotDepth,  p + "rotDepth",  P + "Rotary Depth",  mod, "ROT DEP", 0, 99, 1, 80, Fmt::Int);
            K (b + ApanRate,  p + "apanRate",  P + "Auto Pan Rate", mod, "PAN RATE", 0, 9.9f, 0.1f, 1.0f, Fmt::Hz01);
            K (b + ApanDepth, p + "apanDepth", P + "Auto Pan Depth", mod, "PAN DEP", 0, 99, 1, 99, Fmt::Int);
            K (b + FmRate,  p + "fmRate",  P + "FM Rate",  mod, "FM RATE", 0, 9.9f, 0.1f, 5.0f, Fmt::Hz01);
            K (b + FmDepth, p + "fmDepth", P + "FM Depth", mod, "FM DEP",  0, 99, 1, 0, Fmt::Int);
            K (b + FmFbk,   p + "fmFbk",   P + "FM Feedback", mod, "FM FBK", 0, 99, 1, 0, Fmt::Int);
            K (b + TuneL, p + "tuneL", P + "Pitch Tune Left",  mod, "TUNE L", -24, 24, 0.01f, 0, Fmt::Semis);
            K (b + TuneR, p + "tuneR", P + "Pitch Tune Right", mod, "TUNE R", -24, 24, 0.01f, 0, Fmt::Semis);
            K (b + DelL,  p + "delL",  P + "Pitch Delay Left",  mod, "DEL L", 0, 500, 1, 0, Fmt::Ms);
            K (b + DelR,  p + "delR",  P + "Pitch Delay Right", mod, "DEL R", 0, 500, 1, 0, Fmt::Ms);
            K (b + FbL,   p + "fbL",   P + "Pitch Fbk Left",  mod, "FB L", 0, 99, 1, 0, Fmt::Int);
            K (b + FbR,   p + "fbR",   P + "Pitch Fbk Right", mod, "FB R", 0, 99, 1, 0, Fmt::Int);

            T (b + EchoOn,   p + "echoOn",   P + "Echo On", ech, "ECHO", false);
            C (b + EchoMode, p + "echoMode", P + "Echo Mode", ech, "MODE",
               { "MONO LEFT", "MONO L+R", "XOVER L&R", "STEREO" }, Stereo);
            K (b + EchoLevel, p + "echoLevel", P + "Echo Mix",   ech, "MIX", 0, 99, 1, 0, Fmt::Int);   // starts down
            K (b + EchoDelay, p + "echoDelay", P + "Echo Delay", ech, "DELAY", 1, 999, 1, 200, Fmt::Ms);
            K (b + EchoDamp,  p + "echoDamp",  P + "Echo Damp",  ech, "DAMP", 15, 30, 1, 26, Fmt::IsoHz);
            K (b + EchoFbk,   p + "echoFbk",   P + "Echo Feedback", ech, "FBK", 0, 99, 1, 40, Fmt::Int);
            K (b + EchoOffs,  p + "echoOffs",  P + "Echo L/R Offset", ech, "L/R OFFS", -50, 50, 1, 0, Fmt::Pan);
            K (b + LDelay, p + "lDelay", P + "Echo L Delay", ech, "L DELAY", 1, 999, 1, 200, Fmt::Ms);
            s[(size_t) (b + LDelay)].newRow = true;                   // ECHO page: row 2 = left
            K (b + LDamp,  p + "lDamp",  P + "Echo L Damp",  ech, "L DAMP", 15, 30, 1, 26, Fmt::IsoHz);
            K (b + LFbk,   p + "lFbk",   P + "Echo L Feedback", ech, "L FBK", 0, 99, 1, 40, Fmt::Int);
            K (b + RDelay, p + "rDelay", P + "Echo R Delay", ech, "R DELAY", 1, 999, 1, 300, Fmt::Ms);
            s[(size_t) (b + RDelay)].newRow = true;                   // row 3 = right
            K (b + RDamp,  p + "rDamp",  P + "Echo R Damp",  ech, "R DAMP", 15, 30, 1, 26, Fmt::IsoHz);
            K (b + RFbk,   p + "rFbk",   P + "Echo R Feedback", ech, "R FBK", 0, 99, 1, 40, Fmt::Int);


            K (b + DistLevel, p + "distLevel", P + "Dist/EQ Level", mix, "DIST/EQ", 0, 99, 1, 99, Fmt::Int);
            K (b + DistPan,   p + "distPan",   P + "Dist/EQ Pan",   mix, "D PAN", -50, 50, 1, 0, Fmt::Pan);
            K (b + ModLevel,  p + "modLevel",  P + "Mod Level",     mix, "MOD", 0, 99, 1, 99, Fmt::Int);
            s[(size_t) (b + ModLevel)].newRow = true;                 // MIX page: row 2 = MOD
            K (b + ModPan,    p + "modPan",    P + "Mod Pan",       mix, "M PAN", -50, 50, 1, 0, Fmt::Pan);
            K (b + ModWidth,  p + "modWidth",  P + "Mod Width",     mix, "M WID", 0, 99, 1, 99, Fmt::Int);
            K (b + EchoPan,   p + "echoPan",   P + "Echo Pan",      mix, "E PAN", -50, 50, 1, 0, Fmt::Pan);
            s[(size_t) (b + EchoPan)].newRow = true;                  // row 3 = ECHO pan / width
            K (b + EchoWidth, p + "echoWidth", P + "Echo Width",    mix, "E WID", 0, 99, 1, 99, Fmt::Int);

            // ---- tempo sync (a SYNC per page; each note-value knob takes its free knob's place)
            T (b + EqSync,   p + "eqSync",   P + "F-MOD Sync",  dist, "SYNC", false);
            T (b + ModSync,  p + "modSync",  P + "Mod Sync",    mod,  "SYNC", false);
            T (b + RotSync,  p + "rotSync",  P + "Rotary Sync", mod,  "ROT SYNC", false);
            T (b + EchoSync, p + "echoSync", P + "Echo Sync",   ech,  "SYNC", false);
            auto D = [&] (int idx, const std::string& id, const std::string& name, const std::string& page, const std::string& label,
                          bool rate, float def, int twin, int sync)
            {
                const float hi = (float) ((rate ? rateDivs() : delayDivs()).size() - 1);
                K (b + idx, p + id, P + name, page, label, 0, hi, 1, def, rate ? Fmt::RateDiv : Fmt::DelayDiv);
                s[(size_t) (b + idx)].twinOf = b + twin;  s[(size_t) (b + idx)].syncBy = b + sync;
            };
            D (Bp1Div,  "bp1Div",  "BP1 F-MOD Note",    dist, "BP1 RATE", true, 11, Bp1Rate, EqSync);    // 1 bar
            D (Bp2Div,  "bp2Div",  "BP2 F-MOD Note",    dist, "BP2 RATE", true, 12, Bp2Rate, EqSync);    // 2 bars
            D (ModDiv,  "modDiv",  "PFC Rate Note",     mod,  "RATE",     true, 9,  ModRate, ModSync);   // 1/2
            D (ApanDiv, "apanDiv", "Auto Pan Note",     mod,  "PAN RATE", true, 9,  ApanRate, ModSync);
            D (FmDiv,   "fmDiv",   "FM Rate Note",      mod,  "FM RATE",  true, 2,  FmRate, ModSync);    // 1/16
            D (DelLDiv, "delLDiv", "Pitch Delay L Note", mod, "DEL L",    false, 0, DelL, ModSync);      // 1/64
            D (DelRDiv, "delRDiv", "Pitch Delay R Note", mod, "DEL R",    false, 0, DelR, ModSync);
            D (Rot1Div, "rot1Div", "Rotary Speed1 Note", mod, "SPEED1",   true, 9,  RotSpeed1, RotSync); // 1/2
            D (Rot2Div, "rot2Div", "Rotary Speed2 Note", mod, "SPEED2",   true, 2,  RotSpeed2, RotSync); // 1/16
            D (EchoDiv, "echoDiv", "Echo Delay Note",   ech,  "DELAY",    false, 8, EchoDelay, EchoSync);  // 1/8D
            D (LDiv,    "lDiv",    "Echo L Delay Note", ech,  "L DELAY",  false, 7, LDelay, EchoSync);     // 1/8
            D (RDiv,    "rDiv",    "Echo R Delay Note", ech,  "R DELAY",  false, 10, RDelay, EchoSync);    // 1/4
        }

        for (int n = 0; n < 2; ++n)
        {
            const int b = n == 0 ? RV1 : RV2;
            const std::string p = n == 0 ? "rv1_" : "rv2_", P = n == 0 ? "RV1 " : "RV2 ", pg = n == 0 ? "RV1" : "RV2";
            T (b + RvOn,   p + "on",   P + "On", "UNITS", "ON", false);
            K (b + RvMix,  p + "mix",  P + "Wet/Dry", "UNITS", "WET", 0, 100, 1, 30, Fmt::Percent);
            C (b + RvType, p + "type", P + "Type", pg, "TYPE", revTypes, n == 0 ? LargeHall : LargeRoom);
            K (b + RvPredelay, p + "predelay", P + "Predelay", pg, "PREDELAY", 0, 200, 1, 20, Fmt::Ms);
            K (b + RvDecay,    p + "decay",    P + "Decay",    pg, "DECAY", 0, 99, 1, 60, Fmt::Int);
            K (b + RvLF,       p + "lf",       P + "LF Damp",  pg, "LF DAMP", 0, 15, 1, 8, Fmt::IsoHz);
            K (b + RvHF,       p + "hf",       P + "HF Damp",  pg, "HF DAMP", 18, 30, 1, 24, Fmt::IsoHz);
            K (b + RvDiffuse,  p + "diffuse",  P + "Diffuse",  pg, "DIFFUSE", 0, 99, 1, 99, Fmt::Int);
            K (b + RvNear,     p + "near",     P + "Near",     pg, "NEAR", 0, 99, 1, 0, Fmt::Int);
            K (b + RvTime,     p + "time",     P + "Gate/Rev Time", pg, "TIME", 0, 99, 1, 30, Fmt::Int);
            K (b + RvLevel,    p + "level",    P + "Level",    pg, "LEVEL", 0, 99, 1, 99, Fmt::Int);
            K (b + RvPan,      p + "pan",      P + "Pan",      pg, "PAN", -50, 50, 1, 0, Fmt::Pan);
            T (b + RvSync, p + "sync", P + "Sync", pg, "SYNC", false);
            K (b + RvPreDiv,  p + "preDiv",  P + "Predelay Note", pg, "PREDELAY", 0, (float) (delayDivs().size() - 1), 1, 0, Fmt::DelayDiv);
            s[(size_t) (b + RvPreDiv)].twinOf = b + RvPredelay;  s[(size_t) (b + RvPreDiv)].syncBy = b + RvSync;
            K (b + RvTimeDiv, p + "timeDiv", P + "Gate/Rev Time Note", pg, "TIME", 0, (float) (delayDivs().size() - 1), 1, 10, Fmt::DelayDiv);
            s[(size_t) (b + RvTimeDiv)].twinOf = b + RvTime;  s[(size_t) (b + RvTimeDiv)].syncBy = b + RvSync;
        }

        {
            std::vector<std::string> names;
            for (size_t i = 0; i < orders().size(); ++i) names.push_back (orderName ((int) i));
            C (GLOB + Order, "fx_order", "Effects Order", "UNITS", "ORDER", names, 0);
        }
        return s;
    }

    inline const std::vector<Spec>& specs()
    {
        static const std::vector<Spec> s = buildSpecs();
        return s;
    }

    inline std::string formatValue (const Spec& sp, float v)
    {
        char buf[32];
        switch (sp.fmt)
        {
            case Fmt::Hz01:   std::snprintf (buf, sizeof buf, "%.1fHz", v); return buf;
            case Fmt::IsoHz:  return isoText (v);
            case Fmt::Ms:     std::snprintf (buf, sizeof buf, "%03dmS", (int) std::lround (v)); return buf;
            case Fmt::Db37:   if (v <= -37.0f) return "OFF";
                              std::snprintf (buf, sizeof buf, "%+03ddB", (int) std::lround (v)); return buf;
            case Fmt::Pan:    { const int p = (int) std::lround (v);
                                if (p == 0) return "MID";
                                std::snprintf (buf, sizeof buf, "%s%02d", p < 0 ? "L" : "R", std::abs (p)); return buf; }
            case Fmt::Semis:  std::snprintf (buf, sizeof buf, "%+06.2f", v); return buf;
            case Fmt::Sec:    std::snprintf (buf, sizeof buf, "%.1f Sec", v); return buf;
            case Fmt::Signed: std::snprintf (buf, sizeof buf, "%+03d", (int) std::lround (v)); return buf;
            case Fmt::RmHz:   std::snprintf (buf, sizeof buf, "%dHz", (int) std::lround (rmHz (v))); return buf;
            case Fmt::Percent: std::snprintf (buf, sizeof buf, "%d %%", (int) std::lround (v)); return buf;
            case Fmt::DelayDiv: { const auto& d = delayDivs(); const int i = std::clamp ((int) std::lround (v), 0, (int) d.size() - 1); return d[(size_t) i].name; }
            case Fmt::RateDiv:  { const auto& d = rateDivs();  const int i = std::clamp ((int) std::lround (v), 0, (int) d.size() - 1); return d[(size_t) i].name; }
            default:          std::snprintf (buf, sizeof buf, "%02d", (int) std::lround (v)); return buf;
        }
    }
} // namespace eb16
} // namespace cba
