#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "CongoLookAndFeel.h"
#include "ThemeStore.h"
#include "UiParts.h"

class CongoFilterEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit CongoFilterEditor (CongoFilterProcessor&);
    ~CongoFilterEditor() override;

    void resized() override;

    static constexpr int baseW = 1908, baseH = 610;           // drawn at this size, scaled up to 2x

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    // everything is drawn on a fixed-size canvas that is scaled as the window grows
    struct Canvas : public juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint, onPaintOver;
        void paint (juce::Graphics& g) override             { if (onPaint) onPaint (g); }
        void paintOverChildren (juce::Graphics& g) override { if (onPaintOver) onPaintOver (g); }
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<APVTS::SliderAttachment> att;          // destroyed first
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<APVTS::ComboBoxAttachment> att;
    };
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<APVTS::ButtonAttachment> att;
    };

    // one Effects Board control built from the shared table
    struct EbControl
    {
        int specIndex = 0;
        std::unique_ptr<Knob>   knob;
        std::unique_ptr<Choice> choice;
        std::unique_ptr<Toggle> toggle;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const char* paramID, const juce::String& title, const juce::String& tip);
    void setupChoice (Choice&, const char* paramID, const juce::String& title, const juce::String& tip);
    void setupToggle (Toggle&, const char* paramID, const juce::String& text, const juce::String& tip);
    void applyTheme (const congo::NamedTheme& t);
    // Disco: new random colours on every beat
    bool disco = false;
    double lastBeat = -1.0;
    juce::Random discoRng;
    void discoStep();
    void convertFreezeSize (bool toSync);
    void openThemeEditor();
    void paintCanvas (juce::Graphics&);
    void paintCanvasOver (juce::Graphics&);
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title) const;
    void placeKnobs (Knob* const* knobs, int count, juce::Rectangle<int> area);
    void layoutCanvas();
    void buildEb16();
    void showEbPage();
    void updateSyncTwins();
    void setParam (const char* id, float value);
    juce::String ebPageName() const;

    CongoFilterProcessor& proc;
    congo::CongoLookAndFeel lnf;                                // must outlive every control
    Canvas canvas;

    enum { kTime, kPitch, kFine, kCycle, kQuality, kXfd, kBits, kRate, kThresh, kRatio, kCycleDiv,
           kCutoff, kReso, kDrive, kBassKeep, kAnalog, kMix, kOutput, kFrzMs, kFrzDiv, kMaster, kMasterPan, kNumKnobs };
    std::array<Knob, kNumKnobs> knobs;

    enum { cTrigger, cRetrig, cType, cFade, cMode, cFrzSource, kNumChoices };
    std::array<Choice, kNumChoices> choices;

    enum { tModel, tMode, tAutoD, tSamplerOn, tSync, tCycSync, tFilterOn, tEngine, tTest, tFreeze, tFrzSync, tHost, tFrzHold,
           tMixLock, tMasterLock, tFrzLock, tTight, kNumToggles };
    std::array<Toggle, kNumToggles> toggles;

    juce::TextButton themeButton { "THEME" };
    // PRESET bar
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "SAVE" }, presetDelete { "DELETE" };
    juce::ComboBox presetBox;
    juce::TextEditor presetName;
    juce::uint32 deleteArmedAt = 0;
    void refreshPresets (bool forceRebuild = false);
    void beginSavePreset();
    void finishSavePreset (bool keep);
    congo::ThemeStore themes;
    congo::NamedTheme currentTheme;
    std::unique_ptr<congo::TempoDisplay> tempoDisplay;
    juce::TextButton killButton { "KILL AUDIO" };    // momentary: press = kill
    juce::TextButton resetAllButton { "RESET" };      // back to the last save point (last preset loaded / saved)
    juce::StringArray shownPresetList;                 // what the preset list was last built from
    juce::TextButton nudgeEarly { "< EARLY" }, nudgeLate { "LATE >" }, nudgeReset { "RESET" };
    juce::TextButton nudgeCoarseEarly { "< EARLY" }, nudgeCoarseLate { "LATE >" };
    juce::TextButton nudgeFineEarly { "< EARLY" }, nudgeFineLate { "LATE >" };
    juce::Label nudgeCoarseLabel, nudgeFineLabel;
    congo::DragNumberLabel nudgeReadout;                // drag up / down, or click to type
    juce::Rectangle<int> nudgeArea, masterArea;
    std::unique_ptr<congo::MasterStrip> masterStrip;
    std::unique_ptr<congo::ClipLights>  clipLights;
    juce::TextButton modeButton { "LIVE" };            // LIVE / SUSTAIN / SAMPLE (click to step through)
    bool mirroringTune = false;
    void stepNudge (float msPerClick);
    void linkTunes (int fxBase);
    bool killDown = false;

    // Effects Board: unit buttons (FX1 FX2 RV1 RV2) open a unit's page; FX pages have sub-pages
    std::vector<std::unique_ptr<EbControl>> eb;
    std::array<juce::TextButton, 4> unitButtons;
    std::array<juce::TextButton, 4> ebSubTabs;
    int ebTab = 0, ebSub = 0;                         // FX1 / DIST/EQ first
    EbControl* findEb (int specIndex) const noexcept;

    juce::TooltipWindow tooltips { this, 600 };

    juce::Rectangle<int> samplerArea, filterArea, unitsArea, outputArea, ebArea, statusArea, meterArea;
    juce::String status;
    float meter = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CongoFilterEditor)
};
