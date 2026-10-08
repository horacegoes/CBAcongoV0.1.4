#pragma once
// =============================================================================
// Congo Filter - parameter IDs, ranges and display text
// Ranges follow the hardware each control belongs to:
//   TIME   25 .. 2000 %     (S2000 TIMESTRETCH range)
//   PITCH  -24 .. +24 st    (S2000/S950 tune tables are +-24 semitones), FINE +-50 cents
//   CYCLE  5 .. 9999        (S950 D-time. The S2000's own 20 .. 2000 limit is lifted here;
//                            with C SYNC on both models go past the hardware limits)
//   QUALITY / XFD 0 .. 20   (S2000)
//   BITS   1 .. 16          RATE 7.5 .. 48 kHz (S950 ROM: bandwidth 3000..19200 Hz = 0.4 x rate)
// Defaults are the S950's (ROM: D-time 1000, POLY, bandwidth 10 kHz = 25 kHz rate, 12-bit).
// Pressing the S950/S2000 button loads that sampler's defaults (applySamplerDefaults).
// MODE: LIVE follows the input; SUSTAIN samples each phrase and plays all of it stretched,
// overlaying up to 8 phrases. TRIGGER (SUSTAIN): Level (default) / Retrig / MIDI note.
//   CUTOFF 20 Hz .. 20 kHz  RESO 0 .. 100 % (Moog self-oscillates from 97 %)
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include "dsp/EB16Spec.h"

namespace congo
{
namespace id
{
    inline constexpr const char* model     = "model";
    inline constexpr const char* type      = "type";
    inline constexpr const char* fade      = "fade";
    inline constexpr const char* autoD     = "autoD";
    inline constexpr const char* retrig    = "retrig";
    inline constexpr const char* samplerOn = "samplerOn";
    inline constexpr const char* time      = "time";
    inline constexpr const char* pitch     = "pitch";
    inline constexpr const char* fine      = "fine";
    inline constexpr const char* cycle     = "cycle";
    inline constexpr const char* quality   = "quality";
    inline constexpr const char* xfd       = "xfd";
    inline constexpr const char* bits      = "bits";
    inline constexpr const char* rate      = "rate";
    inline constexpr const char* filterOn  = "filterOn";
    inline constexpr const char* akai      = "akai";
    inline constexpr const char* fmode     = "fmode";
    inline constexpr const char* cutoff    = "cutoff";
    inline constexpr const char* reso      = "reso";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* bassKeep  = "bassKeep";
    inline constexpr const char* analog    = "analog";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* test      = "test";
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* trigger   = "trigger";
    inline constexpr const char* thresh    = "thresh";
    inline constexpr const char* kill      = "kill";
    inline constexpr const char* freeze    = "freeze";
    inline constexpr const char* frzSource = "frzSource";
    inline constexpr const char* frzSync   = "frzSync";
    inline constexpr const char* frzMs     = "frzMs";
    inline constexpr const char* frzDiv    = "frzDiv";
    inline constexpr const char* syncOn    = "syncOn";
    inline constexpr const char* syncRatio = "syncRatio";
    inline constexpr const char* cycleDiv  = "cycleDiv";
    inline constexpr const char* cycleSync = "cycleSync";
    inline constexpr const char* nudge     = "nudge";
    inline constexpr const char* master    = "master";
    inline constexpr const char* frzHold   = "frzHold";
    inline constexpr const char* mixLock   = "mixLock";
    inline constexpr const char* frzLock   = "frzLock";
    inline constexpr const char* tight     = "tight";
    inline constexpr const char* masterPan = "masterPan";
    inline constexpr const char* masterLock= "masterLock";
    inline constexpr const char* tempoHost = "tempoHost";
    inline constexpr const char* tempo     = "tempo";
}

// SYNC: CYCLE as note values. The audible jump rate (the period) is locked to the note value.
struct CycleDiv { const char* name; double quarters; };
inline const std::vector<CycleDiv>& cycleDivs()
{
    static const std::vector<CycleDiv> d {
        { "1/256", 4.0 / 256.0 }, { "1/128T", 4.0 / 192.0 }, { "1/128", 4.0 / 128.0 }, { "1/64T", 4.0 / 96.0 },
        { "1/64", 4.0 / 64.0 },   { "1/32T", 4.0 / 48.0 },   { "1/32", 4.0 / 32.0 },   { "1/16T", 4.0 / 24.0 },
        { "1/16", 0.25 },         { "1/8T", 1.0 / 3.0 },     { "1/8", 0.5 },           { "1/4T", 2.0 / 3.0 },
        { "1/4", 1.0 },           { "1/2", 2.0 } };
    return d;
}

// stretch SYNC: final length as musical ratios (triplets, dotted, polyrhythms),
// then 250 % steps from 500 % to 2000 %
struct SyncRatio { const char* name; double ratio; };
inline const std::vector<SyncRatio>& syncRatios()
{
    static const std::vector<SyncRatio> r {
        { "1/4  25%", 0.25 },        { "1/3  33.3%", 1.0 / 3.0 }, { "1/2  50%", 0.5 },
        { "2/3  66.7%", 2.0 / 3.0 }, { "3/4  75%", 0.75 },        { "1/1  100%", 1.0 },
        { "5/4  125%", 1.25 },       { "4/3  133.3%", 4.0 / 3.0 },{ "3/2  150%", 1.5 },
        { "5/3  166.7%", 5.0 / 3.0 },{ "2/1  200%", 2.0 },        { "5/2  250%", 2.5 },
        { "8/3  266.7%", 8.0 / 3.0 },{ "3/1  300%", 3.0 },        { "4/1  400%", 4.0 },
        { "5/1  500%", 5.0 },        { "15/2  750%", 7.5 },       { "10/1  1000%", 10.0 },
        { "25/2  1250%", 12.5 },     { "15/1  1500%", 15.0 },     { "35/2  1750%", 17.5 },
        { "20/1  2000%", 20.0 } };
    return r;
}

// FREEZE size in tempo divisions (quarter notes)
struct FreezeDiv { const char* name; double quarters; };
inline const std::vector<FreezeDiv>& freezeDivs()
{
    static const std::vector<FreezeDiv> d {
        { "1/64", 0.0625 }, { "1/32T", 1.0 / 12.0 }, { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 },
        { "1/16", 0.25 },   { "1/8T", 1.0 / 3.0 },   { "1/8", 0.5 },    { "1/4T", 2.0 / 3.0 },
        { "1/4", 1.0 },     { "1/2", 2.0 },          { "1 Bar", 4.0 } };
    return d;
}

inline const juce::StringArray& retrigNames()
{
    static const juce::StringArray n { "Free", "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars" };
    return n;
}

// logarithmic range with optional snapping (step 0 = continuous)
inline juce::NormalisableRange<float> logRange (float lo, float hi, float step = 0.0f)
{
    return juce::NormalisableRange<float> (lo, hi,
        [] (float s, float e, float v) { return s * std::pow (e / s, v); },
        [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
        [step] (float s, float e, float v)
        {
            v = juce::jlimit (s, e, v);
            return step > 0.0f ? juce::jlimit (s, e, std::round (v / step) * step) : v;
        });
}

inline juce::String hzText (float v)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                        : juce::String ((int) std::round (v)) + " Hz";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;

    auto pf = [&L] (const char* pid, const char* name, NormalisableRange<float> r, float def,
                    std::function<String (float, int)> text)
    {
        L.add (std::make_unique<AudioParameterFloat> (ParameterID { pid, 1 }, name, r, def,
                   AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };
    auto pc = [&L] (const char* pid, const char* name, StringArray choices, int def)
    {
        L.add (std::make_unique<AudioParameterChoice> (ParameterID { pid, 1 }, name, choices, def));
    };
    auto pb = [&L] (const char* pid, const char* name, bool def)
    {
        L.add (std::make_unique<AudioParameterBool> (ParameterID { pid, 1 }, name, def));
    };

    // ---- sampler ------------------------------------------------------------------
    pc (id::model,  "Model",       { "S950", "S2000" }, 0);
    pc (id::type,   "Stretch Type", { "Cycles", "Clever" }, 0);
    pc (id::fade,   "S950 Fade",   { "Mono", "Poly" }, 1);
    pb (id::autoD,  "Auto D", false);
    pc (id::retrig, "Retrigger",   retrigNames(), 6);
    pb (id::samplerOn, "Sampler On", true);

    pf (id::time, "Time", logRange (25.0f, 2000.0f), 100.0f,
        [] (float v, int) { return String (v, v < 100.0f ? 1 : 0) + " %"; });
    pf (id::pitch, "Pitch", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
        [] (float v, int) { const int s = (int) std::round (v); return (s > 0 ? "+" : "") + String (s) + " st"; });
    pf (id::fine, "Fine", NormalisableRange<float> (-50.0f, 50.0f, 1.0f), 0.0f,
        [] (float v, int) { const int s = (int) std::round (v); return (s > 0 ? "+" : "") + String (s) + " ct"; });
    pf (id::cycle, "Cycle Length", logRange (5.0f, 9999.0f, 1.0f), 1000.0f,
        [] (float v, int) { return String ((int) std::round (v)); });
    pf (id::quality, "Quality", NormalisableRange<float> (0.0f, 20.0f, 1.0f), 10.0f,
        [] (float v, int) { return String ((int) std::round (v)); });
    pf (id::xfd, "XFD", NormalisableRange<float> (0.0f, 20.0f, 1.0f), 0.0f,
        [] (float v, int) { return String ((int) std::round (v)); });
    pf (id::bits, "Bit Depth", NormalisableRange<float> (1.0f, 16.0f, 1.0f), 12.0f,
        [] (float v, int) { return String ((int) std::round (v)) + " bit"; });
    pf (id::rate, "Sample Rate", logRange (7500.0f, 48000.0f, 1.0f), 25000.0f,
        [] (float v, int) { return hzText (v); });

    // ---- filter -------------------------------------------------------------------
    pb (id::filterOn, "Filter On", true);
    pb (id::akai,     "Filter Engine Akai", false);
    pc (id::fmode,    "Filter Mode", { "LP24", "LP18", "LP12", "BP", "HP", "Notch" }, 0);
    pf (id::cutoff, "Cutoff", logRange (20.0f, 20000.0f), 20000.0f,
        [] (float v, int) { return hzText (v); });
    pf (id::reso, "Resonance", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        [] (float v, int) { return String (v, 1) + " %"; });
    pf (id::drive, "Drive", NormalisableRange<float> (0.0f, 36.0f, 0.1f), 0.0f,
        [] (float v, int) { return String (v, 1) + " dB"; });
    pf (id::bassKeep, "Bass Keep", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        [] (float v, int) { return String ((int) std::round (v)) + " %"; });
    pf (id::analog, "Analog", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        [] (float v, int) { return String ((int) std::round (v)) + " %"; });

    // ---- output -------------------------------------------------------------------
    pf (id::mix, "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        [] (float v, int) { return String ((int) std::round (v)) + " %"; });
    pf (id::output, "Freeze Output", NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        [] (float v, int) { return v <= -59.95f ? String ("-inf dB") : String (v, 1) + " dB"; });

    pb (id::test, "Test Tone", false);

    // ---- SUSTAIN mode ------------------------------------------------------------------
    pc (id::mode,    "Mode",    { "Live", "Sustain", "Sample" }, 0);
    pb (id::tight,   "Timing Tight", true);       // TIGHT: host-compensated look-ahead, stretches land on time
    pc (id::trigger, "Trigger", { "Level", "Retrig", "MIDI" }, 0);
    pf (id::thresh, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -36.0f,
        [] (float v, int) { return String (v, 1) + " dB"; });
    pb (id::kill, "Kill", false);

    // ---- FREEZE ------------------------------------------------------------------------
    pb (id::freeze,    "Freeze", false);
    pb (id::frzHold,   "Freeze Hold", false);
    pc (id::frzSource, "Freeze Source", { "Input", "Sampler", "Output" }, 1);
    pb (id::frzSync,   "Freeze Size Sync", true);                 // SYNC is the default
    pb (id::frzLock,   "Freeze Size Lock", false);                // keep the size when switching MS / SYNC
    pf (id::frzMs, "Freeze Size", logRange (1.0f, 2000.0f), 125.0f,
        [] (float v, int) { return v < 100.0f ? String (v, 1) + " ms" : String ((int) std::round (v)) + " ms"; });
    {
        StringArray n;  for (auto& d : freezeDivs()) n.add (d.name);
        pc (id::frzDiv, "Freeze Size Division", n, 4);
    }

    // ---- stretch SYNC: T SYNC = TIME as musical ratios, C SYNC = CYCLE as note values ----
    pb (id::syncOn, "Time Sync", false);
    pb (id::cycleSync, "Cycle Sync", false);
    {
        StringArray n;  for (auto& r : syncRatios()) n.add (r.name);
        pc (id::syncRatio, "Stretch Sync Ratio", n, 5);
    }
    {
        StringArray n;  for (auto& d : cycleDivs()) n.add (d.name);
        pc (id::cycleDiv, "Stretch Sync Cycle", n, 6);
    }

    // ---- MASTER fader: the very last stage (-inf .. +6 dB, 0 dB about 3/4 of the way up) ----
    {
        NormalisableRange<float> r (-60.0f, 6.0f, 0.1f);
        r.setSkewForCentre (-12.0f);
        pf (id::master, "Master", r, 0.0f,
            [] (float v, int) { return v <= -59.95f ? String ("-inf dB") : (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; });
    }

    pf (id::masterPan, "Master Pan", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        [] (float v, int) { const int p = (int) std::lround (v);
                            return p == 0 ? String ("C") : (p < 0 ? "L" : "R") + String (std::abs (p)); });

    // ---- LOCKs: keep MIX / MASTER (fader + pan) when presets change ----------------------
    pb (id::mixLock,    "Mix Lock", false);
    pb (id::masterLock, "Master Lock", false);

    // ---- NUDGE: move the processed sound earlier (-) or later (+) --------------------------
    pf (id::nudge, "Nudge", NormalisableRange<float> (-250.0f, 250.0f, 0.01f), 0.0f,
        [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 2) + " ms"; });

    // ---- tempo: the host's, or your own (default 133.33 BPM) ----------------------------
    pb (id::tempoHost, "Tempo From Host", true);
    pf (id::tempo, "Tempo", NormalisableRange<float> (20.0f, 300.0f, 0.01f), 133.33f,
        [] (float v, int) { return String (v, 2) + " BPM"; });

    // ---- Effects Board (every control from the shared table) ----------------------------
    for (const auto& sp : cba::eb16::specs())
    {
        const ParameterID pid { sp.id.c_str(), 1 };
        switch (sp.kind)
        {
            case cba::eb16::Kind::Toggle:
                L.add (std::make_unique<AudioParameterBool> (pid, sp.name, sp.def > 0.5f));
                break;
            case cba::eb16::Kind::Choice:
            {
                StringArray n;  for (auto& c : sp.choices) n.add (c);
                L.add (std::make_unique<AudioParameterChoice> (pid, sp.name, n, (int) sp.def));
                break;
            }
            default:
            {
                const auto* spec = &sp;
                L.add (std::make_unique<AudioParameterFloat> (pid, sp.name,
                           NormalisableRange<float> (sp.lo, sp.hi, sp.step), sp.def,
                           AudioParameterFloatAttributes().withStringFromValueFunction (
                               [spec] (float v, int) { return String (cba::eb16::formatValue (*spec, v)); })));
                break;
            }
        }
    }
    return L;
}

// Sampler-specific settings for each machine. Called when the S950/S2000 button is
// pressed (user action only - never on preset/session load or host automation).
//   S950 : 12-bit, 25 kHz (ROM default bandwidth 10 kHz), D-time 1000, POLY, Auto D off
//   S2000: 16-bit, 44.1 kHz, CYCLIC, cycle 1000, Quality 10, XFD 0
inline void applySamplerDefaults (juce::AudioProcessorValueTreeState& s, bool s2000)
{
    auto set = [&s] (const char* pid, float value)
    {
        if (auto* prm = s.getParameter (pid))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            prm->endChangeGesture();
        }
    };
    set (id::bits,  s2000 ? 16.0f : 12.0f);
    set (id::rate,  s2000 ? 44100.0f : 25000.0f);
    set (id::cycle, 1000.0f);
    if (s2000)
    {
        set (id::type, 0.0f);       // CYCLIC
        set (id::quality, 10.0f);
        set (id::xfd, 0.0f);
    }
    else
    {
        set (id::fade, 1.0f);       // POLY
        set (id::autoD, 0.0f);
    }
}
} // namespace congo
