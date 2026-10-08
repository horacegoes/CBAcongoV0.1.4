#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>
#include "Parameters.h"
#include "dsp/CongoSampler.h"
#include "dsp/SustainEngine.h"
#include "dsp/SamplerFrontEnd.h"
#include "dsp/CBAFilter.h"
#include "dsp/S950TestTone.h"
#include "dsp/Freeze.h"
#include "dsp/EB16.h"
#include "PresetManager.h"

class CongoFilterProcessor : public juce::AudioProcessor
{
public:
    CongoFilterProcessor();
    ~CongoFilterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Congo Filter V.0.1.4"; }
    bool acceptsMidi() const override  { return true; }     // MIDI trigger in SUSTAIN mode
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 300.0; }   // SUSTAIN phrases ring on

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    congo::PresetManager presets { apvts };              // factory + your presets

    // read by the editor (UI thread)
    std::atomic<int>   uiCycle { 0 }, uiAutoD { 0 }, uiVoices { 0 };
    std::atomic<float> uiPeak { 0.0f };
    std::atomic<bool>  uiSynced { false }, uiRecording { false }, uiHostTempo { false };
    std::atomic<float> uiBpm { 133.33f }, uiCycleMs { 0.0f };
    std::atomic<double> uiBeat { 0.0 };
    std::atomic<float>  uiLateMs { 0.0f };               // TIGHT: how late the stretch still is (needs more look-ahead)                  // beat position (host's when playing, else own clock) - for Disco
    std::atomic<float> uiMasterPeak[2] { { 0.0f }, { 0.0f } };      // after the MASTER fader, per side

private:
    struct Delay
    {
        static constexpr int N = 256;
        std::array<float, N> d {};
        int pos = 0, len = 0;
        void reset (int latency) { d.fill (0.0f); pos = 0; len = juce::jlimit (0, N - 1, latency); }
        float process (float x) noexcept
        {
            d[(size_t) pos] = x;
            const float y = d[(size_t) ((pos - len + N) % N)];
            pos = (pos + 1) % N;
            return y;
        }
    };

    enum P { pModel, pType, pFade, pAutoD, pRetrig, pSamplerOn, pTime, pPitch, pFine, pCycle,
             pQuality, pXfd, pBits, pRate, pFilterOn, pAkai, pFmode, pCutoff, pReso, pDrive,
             pBassKeep, pAnalog, pMix, pOutput, pTest, pMode, pTrigger, pThresh, pKill,
             pFreeze, pFrzSource, pFrzSync, pFrzMs, pFrzDiv, pSyncOn, pSyncRatio, pCycleDiv,
             pTempoHost, pTempo, pCycleSync, pNudge, pMaster, pFrzHold, pMasterPan, pTight, pCount };

    // a delay whose length can change without clicks (10 ms crossfade between old and new)
    struct GlideDelay
    {
        std::vector<float> b[2];
        int size = 1, w = 0, cur = 0, next = 0, fade = 0, fadeLen = 1;
        void prepare (int maxSamples, int crossfade)
        {
            size = juce::jmax (16, maxSamples + 8);
            for (auto& x : b) x.assign ((size_t) size, 0.0f);
            w = cur = next = fade = 0;  fadeLen = juce::jmax (1, crossfade);
        }
        void clear() { for (auto& x : b) std::fill (x.begin(), x.end(), 0.0f); }
        void setDelay (int d) noexcept
        {
            d = juce::jlimit (0, size - 4, d);
            if (d == next) return;
            if (fade == 0) { next = d;  fade = fadeLen; } else next = d;   // finish into the newest
        }
        void process (float& l, float& r) noexcept
        {
            b[0][(size_t) w] = l;  b[1][(size_t) w] = r;
            auto rd = [this] (int c, int d) { return b[c][(size_t) ((w - d + size) % size)]; };
            if (fade > 0)
            {
                const float t = 1.0f - (float) fade / (float) fadeLen;
                l = rd (0, cur) + t * (rd (0, next) - rd (0, cur));
                r = rd (1, cur) + t * (rd (1, next) - rd (1, cur));
                if (--fade == 0) cur = next;
            }
            else { l = rd (0, cur);  r = rd (1, cur); }
            w = (w + 1) % size;
        }
    };

    // HOLD: the frozen loop printed exactly as heard (one loop, crossfaded seam) and played as its
    // own layer at the very end, so nothing but OUTPUT (fade), HOLD / FREEZE and KILL can change it
    struct HeldLoop
    {
        std::vector<float> hist[2], held[2];
        int histLen = 1, w = 0, len = 0, xf = 0, pos = 0;
        float fade = 0.0f, step = 0.01f, captureGain = 1.0f;
        bool active = false;
        double rate = 44100.0;
        void prepare (double sampleRate, double seconds)
        {
            rate = sampleRate;
            histLen = juce::jmax (64, (int) (seconds * sampleRate));
            for (int c = 0; c < 2; ++c) { hist[c].assign ((size_t) histLen, 0.0f);  held[c].assign ((size_t) histLen, 0.0f); }
            step = 1.0f / (float) (0.01 * sampleRate);
            clear();
        }
        void clear() noexcept
        {
            for (int c = 0; c < 2; ++c) { std::fill (hist[c].begin(), hist[c].end(), 0.0f);  std::fill (held[c].begin(), held[c].end(), 0.0f); }
            w = len = pos = 0;  fade = 0.0f;  active = false;
        }
        void record (float l, float r) noexcept { hist[0][(size_t) w] = l;  hist[1][(size_t) w] = r;  w = (w + 1) % histLen; }
        void capture (int loopLen, float gainNow) noexcept
        {
            xf = juce::jmin ((int) (0.005 * rate), loopLen / 4);
            len = juce::jlimit (8, histLen - xf - 1, loopLen);
            const int total = len + xf;
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < total; ++k)
                    held[c][(size_t) k] = hist[c][(size_t) ((w - total + k + 2 * histLen) % histLen)];
            pos = 0;  fade = 1.0f;  active = true;  captureGain = gainNow;
        }
        void release() noexcept { active = false; }
        bool playing() const noexcept { return active || fade > 0.0f; }
        // adds the held loop; gain = OUTPUT now, relative to OUTPUT when it was captured
        void play (float& l, float& r, float gainNow) noexcept
        {
            if (! playing()) return;
            if (! active) fade = juce::jmax (0.0f, fade - step);
            const float g = fade * juce::jlimit (0.0f, 4.0f, gainNow / juce::jmax (captureGain, 1.0e-4f));
            float v[2];
            for (int c = 0; c < 2; ++c)
            {
                v[c] = held[c][(size_t) (xf + pos)];
                if (pos >= len - xf && xf > 0)                     // seam: blend into what came before the loop
                {
                    const float t = (float) (pos - (len - xf) + 1) / (float) xf;
                    v[c] += t * (held[c][(size_t) (pos - (len - xf))] - v[c]);
                }
            }
            l += v[0] * g;  r += v[1] * g;
            if (++pos >= len) pos = 0;
        }
    };

    void updateParameters();
    bool stepBoundary (int64_t blockSample, double shiftSamples, int64_t& last) noexcept;   // retrig step reached (synced)
    cba::VoiceSettings voiceSettings (double extraSemis) const noexcept;
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void startKill() noexcept;
    void finishKill() noexcept;
    double timePercent() const noexcept;                 // TIME knob, or the SYNC ratio
    void applyNudge (bool force);
    double syncedCycle (bool s2000, bool intell, bool autoD) const noexcept;   // host samples, 0 = off
    void freezeAt (int where, float& l, float& r) noexcept;

    cba::CongoSampler    sampler;
    cba::SustainEngine   sustain;
    cba::SamplerFrontEnd frontEnd;
    cba::CBAFilter       filter;
    cba::S950TestTone    testTone;
    cba::Freeze          freeze;
    HeldLoop             held;
    bool                 holdPrev = false;
    cba::eb16::Board     eb16;                          // the Effects Board
    std::vector<float>               ebValues;          // EB16Spec order, refreshed per block
    std::vector<std::atomic<float>*> ebParams;
    int freezeSource = 1, lastFreezeSource = 1;

    juce::AudioBuffer<float> dry, preFilter, killEnv;
    Delay bypassDelay[2];
    GlideDelay dryPath, wetNudge;                       // dry: filter latency (+ early nudge); wet: late nudge
    GlideDelay inLook, encLook;                         // TIGHT: the un-stretched paths wait the look-ahead too
    int reportedLatency = -1;
    juce::SmoothedValue<float> mixGain, outGain, samplerGate, filterGate, modeGate, masterGain, panL, panR;

    double sr = 44100.0;
    int    maxBlock = 512, latency = 0;
    double pitchRatio = 1.0;

    // transport / retrigger
    bool    playing = false, synced = false;
    double  ppqAtBlock = 0.0, bpm = 133.33, stepQuarters = 4.0;
    double  internalPpq = 0.0;                           // own clock when HOST is off
    int64_t lastStep = std::numeric_limits<int64_t>::min(), lastRecStep = std::numeric_limits<int64_t>::min();
    int     lookAhead = 0;                               // TIGHT: reported look-ahead (samples), 0 in LIVE timing
    bool    sampleMode = false;

    // SUSTAIN triggering
    bool  sustainMode = false, wasSustain = false;
    int   triggerMode = 0;                                   // 0 Level, 1 Retrig, 2 MIDI
    float threshLin = 0.0158f, env = 0.0f, envRelease = 0.999f;
    bool  gateOpen = false;
    int   below = 0, holdSamples = 11025, midiNote = -1;
    bool  testPrev = false, testPhrase = false;

    // KILL
    bool  killPrev = false;
    int   killStage = 0;                                     // 0 none, 1 fading out, 2 fading in
    float killGain = 1.0f, killStepDown = 0.01f, killStepUp = 0.01f;
    bool  filterResetPending = false;

    std::atomic<float>* p[pCount] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CongoFilterProcessor)
};
