#include "PluginEditor.h"

namespace
{
    const char* const themeProp = "theme";              // legacy: built-in theme index
    const char* const themeKeys[4] = { "themeName", "themeBg", "themeMain", "themeSecondary" };
    const char* const scaleProp = "uiScale";
    const char* const subNames[4]  = { "DIST/EQ", "MOD", "ECHO", "MIX" };
    const char* const unitNames[4] = { "FX1", "FX2", "RV1", "RV2" };
}

CongoFilterEditor::CongoFilterEditor (CongoFilterProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (canvas);                     // first, so the resize corner sits above it
    canvas.onPaint     = [this] (juce::Graphics& g) { paintCanvas (g); };
    canvas.onPaintOver = [this] (juce::Graphics& g) { paintCanvasOver (g); };

    // ---- sampler ------------------------------------------------------------------
    setupToggle (toggles[tModel], congo::id::model, "950",
                 "Sampler model - press to switch. Loads that sampler's settings:\n"
                 "950 (Akai S950): 12-bit, 25 kHz (10 kHz bandwidth), D-time 1000, POLY fade, CYCLES only, Auto D.\n"
                 "2000 (Akai S2000): 16-bit, 44.1 kHz, CYCLES or CLEVER, cycle 1000, Quality 10, XFD 0.");
    toggles[tModel].button.onClick = [this]
    {
        congo::applySamplerDefaults (proc.apvts, toggles[tModel].button.getToggleState());
    };
    modeButton.setTooltip ("MODE - click to step through:\n"
                           "LIVE: follows the input and stays locked to the beat (cuts long phrases short).\n"
                           "SUSTAIN: samples each phrase and plays ALL of it stretched, even after the input stops;\n"
                           "up to 8 phrases overlay, each keeping the settings it started with.\n"
                           "SAMPLE: records each RETRIG step and plays it back from its start on the next step,\n"
                           "stretched and pitched like a sampler playing a sample - dead on the beat, one step later.");
    modeButton.onClick = [this]
    {
        const int m = (int) std::lround (proc.apvts.getRawParameterValue (congo::id::mode)->load());
        setParam (congo::id::mode, (float) ((m + 1) % 3));
    };
    canvas.addAndMakeVisible (modeButton);
    setupToggle (toggles[tTight], congo::id::tight, "TIMING: TIGHT",
                 "TIMING TIGHT: the plugin asks the host for 100 ms of look-ahead (compensated automatically), so\n"
                 "every stretch starts exactly on its beat whatever the pitch - like a real Akai playing a sample.\n"
                 "Anything monitored live through the plugin is 100 ms late. LIVE timing: no added latency.");
    setupChoice (choices[cTrigger], congo::id::trigger, "TRIGGER", "SUSTAIN: what starts a new phrase.\nLevel: input goes above THRESH (ends after 250 ms below).\nRetrig: each RETRIG step (transport playing).\nMIDI: a note starts it, note-off ends it; the note transposes it (C3 = none).");
    setupChoice (choices[cType],   congo::id::type,   "TYPE",    "2000 stretch type. CLEVER picks a new cycle length every period to match the waveform (the S2000's INTELL).");
    setupChoice (choices[cFade],   congo::id::fade,   "FADE",    "950 crossfade. POLY = equal-power sine fade (default). MONO = linear fade.");
    setupChoice (choices[cRetrig], congo::id::retrig, "RETRIG",  "Restart the stretch in time with the host. Free = run continuously.");
    setupToggle (toggles[tAutoD],     congo::id::autoD,     "AUTO D", "950 Auto D: at each retrigger, search for the cycle length that best matches the waveform.");
    setupToggle (toggles[tSamplerOn], congo::id::samplerOn, "SAMPLER", "Sampler section on/off.");
    setupToggle (toggles[tSync],      congo::id::syncOn,    "T SYNC",
                 "TIME SYNC: TIME steps through musical lengths - 1/4 .. 4/1 incl. triplets, dotted, 5:4, 5:3,\n"
                 "then 250 % steps to 2000 %. Changes land cleanly at the next cycle. Off = the free TIME knob.");
    setupToggle (toggles[tCycSync],   congo::id::cycleSync, "C SYNC",
                 "CYCLE SYNC: the sampler's jump rate locks to a note value at the tempo (1/256 .. 1/2, triplets incl.),\n"
                 "beyond the hardware limits if needed. Off = the free CYCLE knob. (CLEVER and Auto D pick their own.)");
    setupToggle (toggles[tHost], congo::id::tempoHost, "HOST",
                 "HOST on: follow the host's tempo and transport.\nHOST off: your own TEMPO (default 133.33 BPM) with its own clock - RETRIG and SYNC run without the transport.");
    if (auto* tp = proc.apvts.getParameter (congo::id::tempo))
    {
        tempoDisplay = std::make_unique<congo::TempoDisplay> (*tp,
            [this] { return proc.uiBpm.load(); },
            [this] { return proc.uiHostTempo.load(); },
            [this] { return proc.apvts.getRawParameterValue (congo::id::tempoHost)->load() > 0.5f; });
        canvas.addAndMakeVisible (*tempoDisplay);
    }

    setupKnob (knobs[kTime],    congo::id::time,    "TIME",    "Final length, 25..2000 % (the 2000's range). Below 100 % squeezes, above stretches. (T SYNC off)");
    setupKnob (knobs[kPitch],   congo::id::pitch,   "PITCH",   "Transpose -24..+24 semitones without changing the timing.");
    setupKnob (knobs[kFine],    congo::id::fine,    "FINE",    "Fine tune -50..+50 cents.");
    setupKnob (knobs[kCycle],   congo::id::cycle,   "CYCLE",   "Cycle length in samples at the sampler rate, 5..9999 (950 D-time; the real S2000 stops at 20..2000).");
    setupKnob (knobs[kQuality], congo::id::quality, "QUALITY", "2000 CLEVER search quality 0..20.");
    setupKnob (knobs[kXfd],     congo::id::xfd,     "XFD",     "2000 crossfade: (XFD+1) cycles, up to the whole period.");
    setupKnob (knobs[kBits],    congo::id::bits,    "BITS",    "Converter bit depth 1..16. The 950 engine itself always runs 12-bit.");
    setupKnob (knobs[kRate],    congo::id::rate,    "RATE",    "Sampler sample rate 7.5..48 kHz (950 bandwidth 3..19.2 kHz = 0.4 x rate), with tracking anti-alias and reconstruction filters.");
    setupKnob (knobs[kThresh],  congo::id::thresh,  "THRESH",  "SUSTAIN Level trigger: input level that starts a phrase.");
    setupKnob (knobs[kRatio],   congo::id::syncRatio, "TIME", "T SYNC on: TIME as a musical fraction of the original length - 1/4 .. 4/1 (triplets, dotted, 5:4, 5:3), then 250 % steps to 20/1 (2000 %).");
    // T SYNC: type any value ("140", "140%", "4/3") and it lands on the nearest ratio
    knobs[kRatio].slider.valueFromTextFunction = [] (const juce::String& text)
    {
        auto t = text.trim().removeCharacters ("% ");
        double pct = t.getDoubleValue();
        if (t.containsChar ('/'))
        {
            const double num = t.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
            const double den = t.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
            pct = den > 0.0 ? 100.0 * num / den : 100.0;
        }
        const auto& r = congo::syncRatios();
        int best = 0;
        for (int i = 1; i < (int) r.size(); ++i)
            if (std::abs (r[(size_t) i].ratio * 100.0 - pct) < std::abs (r[(size_t) best].ratio * 100.0 - pct)) best = i;
        return (double) best;
    };
    setupKnob (knobs[kCycleDiv], congo::id::cycleDiv, "CYCLE", "C SYNC on: the sampler's jump rate locked to a note value at the tempo (the cycle is worked out to match, beyond the hardware limits if needed).");

    // ---- filter -------------------------------------------------------------------
    setupToggle (toggles[tEngine],   congo::id::akai,     "MOOG",   "Filter engine: MOOG transistor ladder / AKAI S2000-style digital. Press to switch.");
    setupChoice (choices[cMode],     congo::id::fmode,    "MODE",   "LP24 / LP18 / LP12 / BP / HP / Notch. The Akai engine is 2-pole (LP modes all 12 dB).");
    setupToggle (toggles[tFilterOn], congo::id::filterOn, "FILTER", "Filter section on/off.");
    setupKnob (knobs[kCutoff],   congo::id::cutoff,   "CUTOFF",    "Cutoff 20 Hz..20 kHz.");
    setupKnob (knobs[kReso],     congo::id::reso,     "RESO",      "Resonance. The Moog ladder starts to self-oscillate at 97 % and sings at the cutoff pitch.");
    setupKnob (knobs[kDrive],    congo::id::drive,    "DRIVE",     "Drive 0..36 dB. Moog: saturates every transistor stage. Akai: hard digital clipping.");
    setupKnob (knobs[kBassKeep], congo::id::bassKeep, "BASS KEEP", "Moog LP modes: 100 % keeps full bass at any resonance; 0 % thins out like a real Moog.");
    setupKnob (knobs[kAnalog],   congo::id::analog,   "ANALOG",    "Moog component mismatch and slow thermal drift.");

    // ---- freeze / output ----------------------------------------------------------
    setupToggle (toggles[tFreeze],  congo::id::freeze,    "FREEZE", "FREEZE: loop the last SIZE of sound. Turn SIZE while frozen to change the loop - and the rate.");
    setupChoice (choices[cFrzSource], congo::id::frzSource, "SOURCE", "What FREEZE captures: the Input (then stretched), the Sampler output, or the final Output after the Effects Board.");
    setupToggle (toggles[tFrzSync], congo::id::frzSync,   "MS",     "SIZE in milliseconds (MS) or tempo divisions (SYNC).");
    setupToggle (toggles[tFrzLock], congo::id::frzLock,   "LOCK",
                 "SIZE LOCK: keep the same size when switching between MS and SYNC\n"
                 "(SYNC -> MS exactly; MS -> SYNC to the nearest note value).");
    toggles[tFrzSync].button.onClick = [this] { convertFreezeSize (toggles[tFrzSync].button.getToggleState()); };
    setupKnob (knobs[kFrzMs],  congo::id::frzMs,  "SIZE", "Freeze size 1 ms .. 2 s.");
    setupKnob (knobs[kFrzDiv], congo::id::frzDiv, "SIZE", "Freeze size in tempo divisions, 1/64 .. 1 bar (triplets included).");
    setupKnob (knobs[kMix],    congo::id::mix,    "MIX",    "Dry / wet.");

    // MASTER channel: fader with the live output meters behind it
    setupKnob (knobs[kMaster], congo::id::master, "MASTER",
               "MASTER: the final output level, after everything (-inf .. +6 dB). Double-click for 0 dB.\n"
               "The meters behind it show the output, left and right; click the strip to clear the red clip lights.");
    knobs[kMaster].slider.setSliderStyle (juce::Slider::LinearVertical);
    knobs[kMaster].slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
    knobs[kMaster].label.setVisible (false);
    setupKnob (knobs[kMasterPan], congo::id::masterPan, "PAN",
               "MASTER PAN (balance): centre leaves both sides untouched; turning one way fades the other side.");
    knobs[kMasterPan].slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 16);
    setupToggle (toggles[tMasterLock], congo::id::masterLock, "LOCK",
                 "MASTER LOCK: keep the fader and PAN where they are when you change preset.");
    setupToggle (toggles[tMixLock], congo::id::mixLock, "LOCK",
                 "MIX LOCK: keep MIX where it is when you change preset - the next preset comes in at the same mix.");
    knobs[kMaster].slider.setSliderSnapsToMousePosition (false);     // grab the cap; clicking the track doesn't jump
    masterStrip = std::make_unique<congo::MasterStrip> (knobs[kMaster].slider, lnf,
        [this] (int c) { return proc.uiMasterPeak[c].exchange (0.0f); });
    canvas.addAndMakeVisible (*masterStrip, 0);                       // behind everything
    clipLights = std::make_unique<congo::ClipLights> (*masterStrip);
    canvas.addAndMakeVisible (*clipLights);
    setupKnob (knobs[kOutput], congo::id::output, "OUTPUT",
               "FREEZE OUTPUT: the level of the frozen sound only (-inf .. +12 dB) - nothing else.\n"
               "With HOLD on, use it to fade the held loop in and out over the live sound.");
    setupToggle (toggles[tFrzHold], congo::id::frzHold, "HOLD",
                 "HOLD: the frozen loop keeps going, and the normal (unfrozen) sound comes back underneath it.\n"
                 "Fade the held loop in and out with OUTPUT."); 
    setupToggle (toggles[tTest], congo::id::test, "TEST", "Plays the Akai S950 factory test tone (C3) into the input of the whole chain.");

    themeButton.setTooltip ("Theme editor: pick a theme, or make and save your own from three colours.");
    themeButton.onClick = [this] { openThemeEditor(); };
    canvas.addAndMakeVisible (themeButton);

    killButton.setComponentID ("kill");
    killButton.setTooltip ("KILL: silences everything the plugin is playing (stretches, filter, effects tails, FREEZE, TEST) and starts fresh. Knob settings are kept.");
    killButton.onStateChange = [this]
    {
        const bool down = killButton.isDown();
        if (down == killDown) return;
        killDown = down;
        setParam (congo::id::kill, down ? 1.0f : 0.0f);
        if (down)
            for (const char* id : { congo::id::test, congo::id::freeze, congo::id::frzHold })
                if (auto* t = proc.apvts.getParameter (id))
                    if (t->getValue() > 0.5f) setParam (id, 0.0f);
    };
    canvas.addAndMakeVisible (killButton);

    resetAllButton.setTooltip ("RESET: back to the last save point - the preset you last loaded or saved, exactly as it\n"
                               "was saved (INIT when the plugin has just been opened). Locked settings are reset too.");
    resetAllButton.onClick = [this]
    {
        proc.presets.resetToLastSave();
        setParam (congo::id::test, 0.0f);
        const bool holding = proc.apvts.getRawParameterValue (congo::id::freeze)->load() > 0.5f
                          && proc.apvts.getRawParameterValue (congo::id::frzHold)->load() > 0.5f;
        if (! holding) setParam (congo::id::freeze, 0.0f);              // a held loop stays held
        refreshPresets();
    };
    canvas.addAndMakeVisible (resetAllButton);

    // NUDGE: move the processed sound earlier / later to put it back in time
    nudgeEarly.setTooltip ("NUDGE the processed sound 0.25 ms earlier. Earlier works by asking the host to\n"
                           "compensate a little latency, so it applies to playback in the DAW.");
    nudgeLate.setTooltip ("NUDGE the processed sound 0.25 ms later.");
    nudgeReset.setTooltip ("Reset NUDGE to 0 ms.");
    nudgeEarly.onClick = [this] { stepNudge (-0.25f); };
    nudgeLate.onClick  = [this] { stepNudge (0.25f); };
    nudgeCoarseEarly.setTooltip ("Coarse: 1 ms earlier per click.");   nudgeCoarseLate.setTooltip ("Coarse: 1 ms later per click.");
    nudgeFineEarly.setTooltip ("Fine: 0.05 ms earlier per click.");    nudgeFineLate.setTooltip ("Fine: 0.05 ms later per click.");
    nudgeCoarseEarly.onClick = [this] { stepNudge (-1.0f); };
    nudgeCoarseLate.onClick  = [this] { stepNudge (1.0f); };
    nudgeFineEarly.onClick   = [this] { stepNudge (-0.05f); };
    nudgeFineLate.onClick    = [this] { stepNudge (0.05f); };
    for (auto* l : { &nudgeCoarseLabel, &nudgeFineLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (congo::monoFont (10.0f));
        canvas.addAndMakeVisible (*l);
    }
    nudgeCoarseLabel.setText ("coarse", juce::dontSendNotification);
    nudgeFineLabel.setText ("fine", juce::dontSendNotification);
    for (juce::Component* c : { (juce::Component*) &nudgeCoarseEarly, (juce::Component*) &nudgeCoarseLate,
                                (juce::Component*) &nudgeFineEarly, (juce::Component*) &nudgeFineLate })
        canvas.addAndMakeVisible (*c);
    nudgeReset.onClick = [this] { setParam (congo::id::nudge, 0.0f); };
    nudgeReadout.setJustificationType (juce::Justification::centred);
    nudgeReadout.setTooltip ("NUDGE: how far the processed sound is moved (- earlier, + later), up to 250 ms.\n"
                             "Drag up / down to change it (0.5 ms per pixel, shift 0.05), or click to type a value, e.g. -3.4");
    nudgeReadout.setEditable (true, true, false);
    nudgeReadout.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    nudgeReadout.perPixel = 0.5f;  nudgeReadout.finePerPixel = 0.05f;
    nudgeReadout.getValue = [this] { return proc.apvts.getRawParameterValue (congo::id::nudge)->load(); };
    nudgeReadout.setValue = [this] (float v)
    {
        setParam (congo::id::nudge, juce::jlimit (-250.0f, 250.0f, std::round (v * 100.0f) / 100.0f));
    };
    nudgeReadout.onTextChange = [this]
    {
        auto t = nudgeReadout.getText().trim().removeCharacters ("+ ms");
        if (t.containsOnly ("-0123456789.") && t.isNotEmpty())
            setParam (congo::id::nudge, juce::jlimit (-250.0f, 250.0f, std::round (t.getFloatValue() * 100.0f) / 100.0f));
    };
    for (juce::Component* c : { (juce::Component*) &nudgeEarly, (juce::Component*) &nudgeReadout,
                                (juce::Component*) &nudgeLate, (juce::Component*) &nudgeReset })
        canvas.addAndMakeVisible (*c);

    buildEb16();

    // ---- PRESET bar ------------------------------------------------------------------
    presetPrev.setTooltip ("Previous preset.");
    presetNext.setTooltip ("Next preset.");
    presetPrev.onClick = [this] { proc.presets.step (-1);  refreshPresets(); };
    presetNext.onClick = [this] { proc.presets.step (1);   refreshPresets(); };
    presetBox.setTooltip ("Presets: the factory ones, then yours.");
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText();
        if (proc.presets.exists (name) && name != proc.presets.current()) { proc.presets.load (name);  refreshPresets(); }
    };
    presetSave.setTooltip ("SAVE the current settings as one of your presets: type a name and press Enter\n"
                           "(Esc cancels). Saving with the name of one of yours replaces it.");
    presetSave.onClick = [this] { beginSavePreset(); };
    presetDelete.setTooltip ("DELETE the selected preset of yours (click twice to confirm). Factory presets can't be deleted.");
    presetDelete.onClick = [this]
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (deleteArmedAt != 0 && now - deleteArmedAt < 3000)
        {
            if (proc.presets.remove (proc.presets.current()))
                proc.apvts.state.setProperty (juce::Identifier ("presetName"), "-", nullptr);
            deleteArmedAt = 0;
            refreshPresets();
        }
        else deleteArmedAt = now;
    };
    presetName.setInputRestrictions (32);
    presetName.onReturnKey = [this] { finishSavePreset (true); };
    presetName.onEscapeKey = [this] { finishSavePreset (false); };
    presetName.onFocusLost = [this] { finishSavePreset (false); };
    for (juce::Component* c : { (juce::Component*) &presetPrev, (juce::Component*) &presetBox, (juce::Component*) &presetNext,
                                (juce::Component*) &presetSave, (juce::Component*) &presetDelete })
        canvas.addAndMakeVisible (*c);
    canvas.addChildComponent (presetName);
    refreshPresets();

    {   // the session's theme (its colours are stored, so it survives even if deleted from the list)
        auto& st = proc.apvts.state;
        congo::NamedTheme t = themes.hasDefault() ? themes.defaultTheme()      // a new copy: your default theme
                                                  : themes.all()[(size_t) juce::jlimit (0, 2, (int) st.getProperty (juce::Identifier (themeProp), 0))];
        juce::Colour bg, fg, dim;
        if (congo::parseHex (st.getProperty (juce::Identifier (themeKeys[1])).toString(), bg)
            && congo::parseHex (st.getProperty (juce::Identifier (themeKeys[2])).toString(), fg)
            && congo::parseHex (st.getProperty (juce::Identifier (themeKeys[3])).toString(), dim))
        {
            t.name = st.getProperty (juce::Identifier (themeKeys[0])).toString();
            t.bg = bg;  t.fg = fg;  t.dim = dim;
            t.builtIn = themes.find (t.name) != nullptr && themes.find (t.name)->builtIn;
        }
        applyTheme (t);
        disco = (int) st.getProperty (juce::Identifier ("disco"), 0) != 0;
    }

    // resizable: never smaller than designed, up to double, fixed shape
    setResizable (true, true);
    setResizeLimits (baseW, baseH, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    const double scale = juce::jlimit (1.0, 2.0, (double) proc.apvts.state.getProperty (juce::Identifier (scaleProp), 1.0));
    setSize ((int) std::lround (baseW * scale), (int) std::lround (baseH * scale));

    layoutCanvas();
    showEbPage();
    startTimerHz (30);
    timerCallback();
}

CongoFilterEditor::~CongoFilterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void CongoFilterEditor::setParam (const char* id, float value)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void CongoFilterEditor::setupKnob (Knob& k, const char* paramID, const juce::String& title, const juce::String& tip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 92, 20);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setTooltip (tip);
    canvas.addAndMakeVisible (k.slider);
    k.att = std::make_unique<APVTS::SliderAttachment> (proc.apvts, paramID, k.slider);
    if (auto* param = proc.apvts.getParameter (paramID))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    k.label.setText (title, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setInterceptsMouseClicks (false, false);
    canvas.addAndMakeVisible (k.label);
}

void CongoFilterEditor::setupChoice (Choice& c, const char* paramID, const juce::String& title, const juce::String& tip)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramID)))
        c.box.addItemList (param->choices, 1);
    c.box.setTooltip (tip);
    canvas.addAndMakeVisible (c.box);
    c.att = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, paramID, c.box);

    c.label.setText (title, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setInterceptsMouseClicks (false, false);
    canvas.addAndMakeVisible (c.label);
}

void CongoFilterEditor::setupToggle (Toggle& t, const char* paramID, const juce::String& text, const juce::String& tip)
{
    t.button.setButtonText (text);
    t.button.setClickingTogglesState (true);
    t.button.setTooltip (tip);
    canvas.addAndMakeVisible (t.button);
    t.att = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, paramID, t.button);
}

// ---- Effects Board panel, built from the shared control table ----------------------------
void CongoFilterEditor::buildEb16()
{
    const auto& specs = cba::eb16::specs();
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const auto& sp = specs[i];
        auto c = std::make_unique<EbControl>();
        c->specIndex = (int) i;
        if (sp.kind == cba::eb16::Kind::Knob)
        {
            c->knob = std::make_unique<Knob>();
            setupKnob (*c->knob, sp.id.c_str(), sp.label, sp.name);
        }
        else if (sp.kind == cba::eb16::Kind::Choice)
        {
            c->choice = std::make_unique<Choice>();
            setupChoice (*c->choice, sp.id.c_str(), sp.label, sp.name);
        }
        else
        {
            c->toggle = std::make_unique<Toggle>();
            setupToggle (*c->toggle, sp.id.c_str(), sp.label, sp.name);
        }
        eb.push_back (std::move (c));
    }

    for (int k = 0; k < 4; ++k)
    {
        unitButtons[(size_t) k].setButtonText (unitNames[k]);
        unitButtons[(size_t) k].setTooltip (juce::String ("Show ") + unitNames[k] + "'s controls on the Effects Board panel.");
        unitButtons[(size_t) k].onClick = [this, k] { ebTab = k;  showEbPage(); };
        canvas.addAndMakeVisible (unitButtons[(size_t) k]);
    }
    for (int k = 0; k < 4; ++k)
    {
        ebSubTabs[(size_t) k].setButtonText (subNames[k]);
        ebSubTabs[(size_t) k].onClick = [this, k] { ebSub = k;  showEbPage(); };
        canvas.addAndMakeVisible (ebSubTabs[(size_t) k]);
    }
    // the unit strip's own controls (ON, WET, ORDER) get fixed places in layoutCanvas
    using namespace cba::eb16;
    for (int idx : { FX1 + FxOn, FX2 + FxOn, RV1 + RvOn, RV2 + RvOn })
        if (auto* c = findEb (idx)) c->toggle->button.setTooltip ("Switch this effect on / off.");
    for (int idx : { FX1 + FxMix, FX2 + FxMix, RV1 + RvMix, RV2 + RvMix })
        if (auto* c = findEb (idx)) c->knob->slider.setTooltip ("Wet / dry for this effect.");
    if (auto* c = findEb (GLOB + Order))
        c->choice->box.setTooltip ("ORDER: the order the signal runs through the four effects.");
    linkTunes (FX1);
    linkTunes (FX2);
}

void CongoFilterEditor::refreshPresets (bool forceRebuild)
{
    const auto all = proc.presets.allNames();
    if (forceRebuild || all != shownPresetList)                  // rebuild only when the list changed
    {
        shownPresetList = all;
        presetBox.clear (juce::dontSendNotification);
        int id = 1;
        presetBox.addSectionHeading ("FACTORY");
        for (auto& n : proc.presets.factoryNames()) presetBox.addItem (n, id++);
        const auto mine = proc.presets.userNames();
        if (! mine.isEmpty())
        {
            presetBox.addSectionHeading ("YOUR PRESETS");
            for (auto& n : mine) presetBox.addItem (n, id++);
        }
    }
    presetBox.setText (proc.presets.current(), juce::dontSendNotification);
    presetDelete.setEnabled (proc.presets.exists (proc.presets.current()) && ! proc.presets.isFactory (proc.presets.current()));
}

void CongoFilterEditor::beginSavePreset()
{
    const auto cur = proc.presets.current();
    presetName.setText (proc.presets.isFactory (cur) || cur == "-" ? juce::String() : cur, juce::dontSendNotification);
    presetName.setBounds (presetBox.getBounds());
    presetName.setVisible (true);
    presetName.grabKeyboardFocus();
    presetName.selectAll();
}

void CongoFilterEditor::finishSavePreset (bool keep)
{
    if (! presetName.isVisible()) return;
    presetName.setVisible (false);
    if (keep && presetName.getText().trim().isNotEmpty()) proc.presets.save (presetName.getText());
    refreshPresets();
}

void CongoFilterEditor::convertFreezeSize (bool toSync)
{
    if (proc.apvts.getRawParameterValue (congo::id::frzLock)->load() < 0.5f) return;
    const double quarterMs = 60000.0 / juce::jmax (1.0f, proc.uiBpm.load());
    const auto& divs = congo::freezeDivs();
    if (toSync)
    {
        const double ms = proc.apvts.getRawParameterValue (congo::id::frzMs)->load();
        int best = 0;
        for (int i = 1; i < (int) divs.size(); ++i)
            if (std::abs (std::log (divs[(size_t) i].quarters * quarterMs / ms))
                < std::abs (std::log (divs[(size_t) best].quarters * quarterMs / ms))) best = i;
        setParam (congo::id::frzDiv, (float) best);
    }
    else
    {
        const int i = juce::jlimit (0, (int) divs.size() - 1,
                                    (int) std::lround (proc.apvts.getRawParameterValue (congo::id::frzDiv)->load()));
        setParam (congo::id::frzMs, juce::jlimit (1.0f, 2000.0f, (float) (divs[(size_t) i].quarters * quarterMs)));
    }
}

void CongoFilterEditor::discoStep()
{
    // readable at random: a dark background, a bright main colour, a mid secondary
    auto& r = discoRng;
    congo::NamedTheme t;
    t.name = "DISCO";
    t.bg  = juce::Colour::fromHSV (r.nextFloat(), 0.5f + 0.4f * r.nextFloat(), 0.04f + 0.08f * r.nextFloat(), 1.0f);
    t.fg  = juce::Colour::fromHSV (r.nextFloat(), 0.6f + 0.4f * r.nextFloat(), 0.85f + 0.15f * r.nextFloat(), 1.0f);
    t.dim = juce::Colour::fromHSV (r.nextFloat(), 0.5f + 0.4f * r.nextFloat(), 0.35f + 0.15f * r.nextFloat(), 1.0f);
    applyTheme (t);
}

void CongoFilterEditor::stepNudge (float msPerClick)
{
    const float now = proc.apvts.getRawParameterValue (congo::id::nudge)->load();
    setParam (congo::id::nudge, juce::jlimit (-250.0f, 250.0f, std::round ((now + msPerClick) * 100.0f) / 100.0f));
}

// TUNE LINK: with it on, turning TUNE L or TUNE R moves the other one too
void CongoFilterEditor::linkTunes (int fxBase)
{
    using namespace cba::eb16;
    auto* l = findEb (fxBase + TuneL);
    auto* r = findEb (fxBase + TuneR);
    auto* link = findEb (fxBase + TuneLink);
    if (l == nullptr || r == nullptr || link == nullptr) return;
    auto follow = [this, link] (juce::Slider& from, juce::Slider& to)
    {
        return [this, link, &from, &to]
        {
            if (mirroringTune || ! link->toggle->button.getToggleState()) return;
            mirroringTune = true;
            to.setValue (from.getValue(), juce::sendNotificationSync);
            mirroringTune = false;
        };
    };
    l->knob->slider.onValueChange = follow (l->knob->slider, r->knob->slider);
    r->knob->slider.onValueChange = follow (r->knob->slider, l->knob->slider);
    link->toggle->button.onClick = [l, r, link]                // switching LINK on: R snaps to L
    {
        if (link->toggle->button.getToggleState())
            r->knob->slider.setValue (l->knob->slider.getValue(), juce::sendNotificationSync);
    };
    link->toggle->button.setTooltip ("TUNE LINK: TUNE L and TUNE R move together.");
}

CongoFilterEditor::EbControl* CongoFilterEditor::findEb (int specIndex) const noexcept
{
    for (auto& c : eb) if (c->specIndex == specIndex) return c.get();
    return nullptr;
}

juce::String CongoFilterEditor::ebPageName() const
{
    if (ebTab < 2) return juce::String (unitNames[ebTab]) + "/" + subNames[ebSub];
    return unitNames[ebTab];
}

void CongoFilterEditor::showEbPage()
{
    const auto page = ebPageName();
    const auto& specs = cba::eb16::specs();
    const bool fx = ebTab < 2;

    for (int k = 0; k < 4; ++k) unitButtons[(size_t) k].setToggleState (k == ebTab, juce::dontSendNotification);
    for (int k = 0; k < 4; ++k)
    {
        ebSubTabs[(size_t) k].setToggleState (k == ebSub, juce::dontSendNotification);
        ebSubTabs[(size_t) k].setVisible (fx);
    }

    auto area = ebArea.reduced (14, 0).withTrimmedTop (fx ? 54 : 22).withTrimmedBottom (8);

    // header (buttons and menus) wraps onto a second line when a page has more than fit
    int headerW = 0, lines = 1, knobsOnPage = 0;
    for (auto& c : eb)
    {
        if (specs[(size_t) c->specIndex].page != page.toStdString()) continue;
        if (c->knob)
        {
            const auto& sp = specs[(size_t) c->specIndex];
            if (sp.twinOf >= 0) continue;                                     // shares its free knob's spot
            if (sp.newRow && knobsOnPage % 6 != 0) knobsOnPage += 6 - knobsOnPage % 6;   // row break
            ++knobsOnPage;
            continue;
        }
        const int w = c->choice ? 140 : 100;
        if (headerW + w > area.getWidth()) { ++lines;  headerW = 0; }
        headerW += w;
    }
    auto header = area.removeFromTop (48 * lines);
    area.removeFromTop (6);
    const int cols = 6, cellW = area.getWidth() / cols;
    const int rows = juce::jmax (1, (knobsOnPage + cols - 1) / cols);
    const int cellH = juce::jmin (116, area.getHeight() / rows);
    int knobIndex = 0;
    juce::Rectangle<int> headerLine = header.removeFromTop (48);

    for (auto& c : eb)
    {
        if (specs[(size_t) c->specIndex].page == "UNITS") continue;            // always shown
        const bool on = juce::String (specs[(size_t) c->specIndex].page) == page;
        if (c->knob)   { c->knob->slider.setVisible (on);  c->knob->label.setVisible (on); }
        if (c->choice) { c->choice->box.setVisible (on);   c->choice->label.setVisible (on); }
        if (c->toggle) { c->toggle->button.setVisible (on); }
        if (! on) continue;

        if (c->knob)
        {
            if (specs[(size_t) c->specIndex].twinOf >= 0) continue;           // placed below, on its free knob
            if (specs[(size_t) c->specIndex].newRow && knobIndex % cols != 0) knobIndex += cols - knobIndex % cols;
            const int col = knobIndex % cols, row = knobIndex / cols;
            juce::Rectangle<int> cell (area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH);
            c->knob->label.setBounds (cell.removeFromTop (18));
            c->knob->slider.setBounds (cell.reduced (4, 0));
            ++knobIndex;
        }
        else
        {
            const int w = c->choice ? 140 : 100;
            if (headerLine.getWidth() < w)                                     // next header line
                headerLine = header.removeFromTop (48);
            auto cell = headerLine.removeFromLeft (w);
            if (c->choice)
            {
                c->choice->label.setBounds (cell.removeFromTop (20).withWidth (132));
                c->choice->box.setBounds (cell.withWidth (132).withHeight (28));
            }
            else
            {
                cell.removeFromTop (20);
                c->toggle->button.setBounds (cell.withWidth (92).withHeight (28));
            }
        }
    }
    // note-value knobs sit exactly where their free knob is
    for (auto& c : eb)
    {
        const auto& sp = specs[(size_t) c->specIndex];
        if (sp.twinOf < 0 || sp.page != page.toStdString() || ! c->knob) continue;
        if (auto* free = findEb (sp.twinOf))
        {
            c->knob->slider.setBounds (free->knob->slider.getBounds());
            c->knob->label.setBounds (free->knob->label.getBounds());
        }
    }
    updateSyncTwins();
    canvas.repaint();
}

// SYNC on a page: its time knobs switch to note values (in the same places)
void CongoFilterEditor::updateSyncTwins()
{
    const auto page = ebPageName().toStdString();
    const auto& specs = cba::eb16::specs();
    for (auto& c : eb)
    {
        const auto& sp = specs[(size_t) c->specIndex];
        if (sp.twinOf < 0 || sp.page != page || ! c->knob) continue;
        const bool synced = proc.apvts.getRawParameterValue (specs[(size_t) sp.syncBy].id.c_str())->load() > 0.5f;
        if (auto* free = findEb (sp.twinOf))
        {
            free->knob->slider.setVisible (! synced);  free->knob->label.setVisible (! synced);
            c->knob->slider.setVisible (synced);       c->knob->label.setVisible (synced);
            c->knob->slider.setEnabled (free->knob->slider.isEnabled());
        }
    }
}

void CongoFilterEditor::openThemeEditor()
{
    auto ed = std::make_unique<congo::ThemeEditor> (themes, currentTheme,
                                                    [this] (const congo::NamedTheme& t) { applyTheme (t); },
                                                    disco,
                                                    [this] (bool on)
                                                    {
                                                        disco = on;  lastBeat = -1.0;
                                                        proc.apvts.state.setProperty (juce::Identifier ("disco"), on ? 1 : 0, nullptr);
                                                    });
    juce::CallOutBox::launchAsynchronously (std::move (ed), getLocalArea (&canvas, themeButton.getBounds()), this);
}

void CongoFilterEditor::applyTheme (const congo::NamedTheme& t)
{
    currentTheme = t;
    lnf.setTheme (congo::Theme::fromColours (t.bg, t.fg, t.dim));
    auto& st = proc.apvts.state;
    st.setProperty (juce::Identifier (themeKeys[0]), t.name, nullptr);
    st.setProperty (juce::Identifier (themeKeys[1]), congo::toHex (t.bg), nullptr);
    st.setProperty (juce::Identifier (themeKeys[2]), congo::toHex (t.fg), nullptr);
    st.setProperty (juce::Identifier (themeKeys[3]), congo::toHex (t.dim), nullptr);
    auto styleKnob = [this] (Knob& k)
    {
        k.label.setColour (juce::Label::textColourId, lnf.getTheme().fg);
        k.slider.setColour (juce::Slider::textBoxTextColourId, lnf.getTheme().fg);
    };
    for (auto& k : knobs) styleKnob (k);
    for (auto* l : { &nudgeCoarseLabel, &nudgeFineLabel })
        l->setColour (juce::Label::textColourId, lnf.getTheme().dim.brighter (0.6f));
    for (auto& c : choices) c.label.setColour (juce::Label::textColourId, lnf.getTheme().dim.brighter (0.6f));
    for (auto& c : eb)
    {
        if (c->knob) styleKnob (*c->knob);
        if (c->choice) c->choice->label.setColour (juce::Label::textColourId, lnf.getTheme().dim.brighter (0.6f));
    }
    sendLookAndFeelChange();
    canvas.repaint();
}

// ---------------------------------------------------------------------------------
void CongoFilterEditor::timerCallback()
{
    auto& st = proc.apvts;
    auto val = [&st] (const char* pid) { return st.getRawParameterValue (pid)->load(); };

    const bool s2000  = val (congo::id::model) > 0.5f;
    const bool intell = s2000 && val (congo::id::type) > 0.5f;
    const bool akai   = val (congo::id::akai) > 0.5f;
    const bool sync   = val (congo::id::syncOn) > 0.5f;
    const bool csync  = val (congo::id::cycleSync) > 0.5f;
    const bool frzSync = val (congo::id::frzSync) > 0.5f;

    // context: dim controls that don't apply
    choices[cType].box.setEnabled (s2000);
    choices[cFade].box.setEnabled (! s2000);
    toggles[tAutoD].button.setEnabled (! s2000);
    knobs[kQuality].slider.setEnabled (intell);
    knobs[kXfd].slider.setEnabled (s2000);
    knobs[kCycle].slider.setEnabled (! intell);
    knobs[kBassKeep].slider.setEnabled (! akai);
    knobs[kAnalog].slider.setEnabled (! akai);
    knobs[kTime].slider.setVisible (! sync);       knobs[kTime].label.setVisible (! sync);
    knobs[kRatio].slider.setVisible (sync);        knobs[kRatio].label.setVisible (sync);
    knobs[kCycle].slider.setVisible (! csync);     knobs[kCycle].label.setVisible (! csync);
    knobs[kCycleDiv].slider.setVisible (csync);    knobs[kCycleDiv].label.setVisible (csync);
    toggles[tCycSync].button.setEnabled (! intell && val (congo::id::autoD) < 0.5f);
    knobs[kCycleDiv].slider.setEnabled (! intell && val (congo::id::autoD) < 0.5f);
    if (tempoDisplay) tempoDisplay->refresh();
    if (disco)
    {
        const double beat = proc.uiBeat.load();
        if (lastBeat < 0.0 || std::floor (beat) != std::floor (lastBeat)) { if (lastBeat >= 0.0) discoStep();  lastBeat = beat; }
    }
    if (! presetName.isVisible() && ! presetBox.isPopupActive() && presetBox.getText() != proc.presets.current())
        refreshPresets();
    {
        const bool armed = deleteArmedAt != 0 && juce::Time::getMillisecondCounter() - deleteArmedAt < 3000;
        if (! armed) deleteArmedAt = 0;
        presetDelete.setButtonText (armed ? "SURE?" : "DELETE");
    }
    knobs[kMaster].label.setVisible (false);
    if (masterStrip) masterStrip->tick (1.0f / 30.0f);
    if (clipLights) clipLights->repaint();
    {
        const float nd = val (congo::id::nudge);
        if (! nudgeReadout.isBeingEdited())
            nudgeReadout.setText (juce::String (nd > 0.0f ? "+" : "") + juce::String (nd, 2) + " ms",
                                  juce::dontSendNotification);
        nudgeReset.setEnabled (std::abs (nd) > 0.001f);
    }
    knobs[kFrzMs].slider.setVisible (! frzSync);   knobs[kFrzMs].label.setVisible (! frzSync);
    knobs[kFrzDiv].slider.setVisible (frzSync);    knobs[kFrzDiv].label.setVisible (frzSync);
    toggles[tFrzSync].button.setButtonText (frzSync ? "SYNC" : "MS");
    toggles[tEngine].button.setButtonText (akai ? "AKAI" : "MOOG");
    toggles[tModel].button.setButtonText (s2000 ? "2000" : "950");
    const int  modeIdx = juce::jlimit (0, 2, (int) std::lround (val (congo::id::mode)));
    const bool sustainMode = modeIdx == 1, sampleMode = modeIdx == 2;
    const int  trig = (int) std::lround (val (congo::id::trigger));
    static const char* modeNames[] = { "LIVE", "SUSTAIN", "SAMPLE" };
    modeButton.setButtonText (modeNames[modeIdx]);
    toggles[tTight].button.setButtonText (val (congo::id::tight) > 0.5f ? "TIMING: TIGHT" : "TIMING: LIVE");
    choices[cTrigger].box.setEnabled (sustainMode);
    knobs[kThresh].slider.setEnabled (sustainMode && trig == 0);
    choices[cRetrig].box.setEnabled (! sustainMode || trig == 1);

    // Effects Board: dim the controls the current type / mode does not use
    {
        using namespace cba::eb16;
        const auto& specs = cba::eb16::specs();
        const auto page = ebPageName();
        for (auto& c : eb)
        {
            if (juce::String (specs[(size_t) c->specIndex].page) != page) continue;   // visible page only
            const int i = c->specIndex;
            bool enabled = true;
            if (i < RV1)                                                      // FX1 / FX2
            {
                const int base = i < FX2 ? FX1 : FX2, r = i - base;
                auto fv = [&] (int rel) { return st.getRawParameterValue (specs[(size_t) (base + rel)].id.c_str())->load(); };
                const int mt = (int) std::lround (fv (ModType));
                switch (r)
                {
                    case PfcType: case ModRate: case ModDepth: case ModFbk: enabled = mt == PhaFlaChor; break;
                    case RotFast: case RotSpeed1: case RotSpeed2: case RotAccel: case RotDepth: enabled = mt == Rotary; break;
                    case ApanMode: case ApanRate: case ApanDepth: case FmRate: case FmDepth: case FmFbk: enabled = mt == FmodApan; break;
                    case TuneL: case TuneR: case DelL: case DelR: enabled = mt == PitchShift || mt == PitchFbk; break;
                    case FbL: case FbR: enabled = mt == PitchFbk; break;
                    default: break;
                }
                const int em = (int) std::lround (fv (EchoMode));
                const bool mono = em == MonoLeft || em == MonoLR;
                if (r == ModType || (r >= PfcType && r <= FbR)) enabled = enabled && fv (ModOn) > 0.5f;
                if (r >= EchoMode && r <= RFbk) enabled = fv (EchoOn) > 0.5f;
                if (r == EchoDelay || r == EchoDamp || r == EchoFbk || r == EchoOffs) enabled = enabled && mono;
                if (r >= LDelay && r <= RFbk) enabled = enabled && ! mono;
            }
            else if (i < GLOB)                                                // RV1 / RV2
            {
                const int base = i < RV2 ? RV1 : RV2, r = i - base;
                const int rt = (int) std::lround (st.getRawParameterValue (specs[(size_t) (base + RvType)].id.c_str())->load());
                const bool gr = rt >= Gated1;
                if (r == RvTime) enabled = gr;
                if (r == RvDecay || r == RvLF || r == RvHF || r == RvNear) enabled = ! gr || (r == RvDecay && rt != Reverse);
            }
            if (c->knob)   c->knob->slider.setEnabled (enabled);
            if (c->choice) c->choice->box.setEnabled (enabled);
        }
    }

    updateSyncTwins();

    // status line: steady facts first; the live, ever-changing cycle readout goes last so it
    // never pushes anything else along (and the voice count / REC keep a fixed width)
    const int cycleKnob = (int) std::lround (val (congo::id::cycle));
    static const char* trigNames[] = { "LEVEL", "RETRIG", "MIDI" };
    juce::String s = sampleMode ? juce::String ("SAMPLE ") + congo::retrigNames()[(int) val (congo::id::retrig)]
                                  + (proc.uiSynced.load() ? juce::String() : juce::String (" (WAITING FOR STEPS)"))
                   : sustainMode ? juce::String ("SUSTAIN ") + trigNames[juce::jlimit (0, 2, trig)]
                                   + " " + juce::String (proc.uiVoices.load()) + "/8"
                                   + (proc.uiRecording.load() ? " REC" : "    ")
                                 : juce::String ("LIVE");
    if (val (congo::id::tight) > 0.5f)
    {
        const float late = proc.uiLateMs.load();
        s << "  |  TIGHT" << (late > 0.5f ? " (+" + juce::String (late, 0) + " ms - more look-ahead needed)" : juce::String());
    }
    s << "  |  " << (s2000 ? (intell ? "2000 CLEVER" : "2000 CYCLES") : "950 CYCLES");
    if (sync)
        s << "  |  TIME " << congo::syncRatios()[(size_t) juce::jlimit (0, (int) congo::syncRatios().size() - 1,
                                                                       (int) std::lround (val (congo::id::syncRatio)))].name;
    s << "  |  " << (s2000 ? "16" : "12") << "-BIT ENGINE";
    if (! s2000) s << "  |  BW " << juce::String (val (congo::id::rate) * 0.4f / 1000.0f, 1) << " kHz";
    if (! sustainMode && ! sampleMode)
        s << "  |  " << (proc.uiSynced.load() ? "SYNC " + congo::retrigNames()[(int) val (congo::id::retrig)]
                                             : juce::String ("FREE RUN"));
    s << "  |  FILTER " << (akai ? "AKAI" : "MOOG");
    if (! akai && val (congo::id::reso) >= 97.0f) s << " SELF-OSC";
    {
        juce::String fxOn;
        for (const char* u : { "fx1", "fx2", "rv1", "rv2" })
            if (val ((juce::String (u) + "_on").toRawUTF8()) > 0.5f) fxOn << " " << juce::String (u).toUpperCase();
        s << "  |  EFFECTS" << (fxOn.isEmpty() ? juce::String (" OFF") : fxOn);
    }
    if (val (congo::id::freeze) > 0.5f) s << "  |  FROZEN";
    // ---- last: the cycle ----
    const bool cycleLocked = csync && ! intell && val (congo::id::autoD) < 0.5f;
    if (cycleLocked)
        s << "  |  CYCLE " << congo::cycleDivs()[(size_t) juce::jlimit (0, (int) congo::cycleDivs().size() - 1,
                                                                      (int) std::lround (val (congo::id::cycleDiv)))].name
          << " (" << juce::String (proc.uiCycleMs.load(), 1) << " ms)";
    else if (intell)
        s << "  |  CYCLE NOW " << proc.uiCycle.load();
    else if (s2000)
        s << "  |  CYCLE " << cycleKnob << (cycleKnob < 20 || cycleKnob > 2000 ? " (BEYOND THE S2000)" : "");
    else if (val (congo::id::autoD) > 0.5f)
        s << "  |  AUTO D " << proc.uiAutoD.load();
    else
        s << "  |  D-TIME " << cycleKnob;
    if (s != status) { status = s;  canvas.repaint (statusArea); }

    const float pk = proc.uiPeak.exchange (0.0f);
    const float m = juce::jmax (pk, meter * 0.82f);
    if (std::abs (m - meter) > 0.002f) { meter = m;  canvas.repaint (meterArea.expanded (2)); }
}

// ---------------------------------------------------------------------------------
void CongoFilterEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title) const
{
    const auto& t = lnf.getTheme();
    g.setColour (t.dim);
    g.drawRect (r.toFloat(), 1.2f);
    const auto font = congo::monoFont (14.0f, true);
    g.setFont (font);
    const juce::String label = "[ " + title + " ]";
    const int tw = juce::GlyphArrangement::getStringWidthInt (font, label) + 12;
    const juce::Rectangle<int> tr (r.getX() + 14, r.getY() - 9, tw, 18);
    g.setColour (t.bg);
    g.fillRect (tr);
    g.setColour (t.fg);
    g.drawText (label, tr, juce::Justification::centred, false);
}

void CongoFilterEditor::paintCanvas (juce::Graphics& g)
{
    const auto& t = lnf.getTheme();
    g.fillAll (t.bg.darker (0.6f));

    const auto screen = juce::Rectangle<int> (0, 0, baseW, baseH).toFloat().reduced (4.0f);
    juce::ColourGradient glass (t.bg.brighter (0.06f), screen.getCentreX(), screen.getCentreY(),
                                t.bg.darker (0.5f), screen.getX(), screen.getY(), true);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (screen, 26.0f);

    const juce::Rectangle<int> title (24, 12, 520, 40);
    g.setFont (congo::monoFont (30.0f, true));
    g.setColour (t.glow);
    g.drawText ("CONGO FILTER", title.translated (1, 1), juce::Justification::centredLeft, false);
    g.setColour (t.fg);
    g.drawText ("CONGO FILTER", title, juce::Justification::centredLeft, false);
    g.setFont (congo::monoFont (11.0f, true));
    g.setColour (t.dim.brighter (0.5f));
    g.drawText ("V.0.1.4", 238, 34, 60, 14, juce::Justification::centredLeft, false);
    g.setFont (congo::monoFont (12.0f));
    g.setColour (t.dim.brighter (0.5f));
    g.drawText ("950 / 2000 STRETCH  //  MOOG + AKAI FILTER  //  EB16 EFFECTS",
                304, 26, 440, 16, juce::Justification::centredLeft, false);
    g.setFont (congo::monoFont (11.0f, true));
    g.setColour (t.dim.brighter (0.5f));
    g.drawText ("PRESET", presetBox.getX(), 8, 120, 13, juce::Justification::centredLeft, false);

    drawSection (g, samplerArea, "SAMPLER");
    drawSection (g, filterArea, "FILTER");
    drawSection (g, unitsArea, "EFFECTS");
    drawSection (g, outputArea, "FREEZE / OUT");
    drawSection (g, ebArea, "EFFECTS BOARD  >  " + ebPageName());
    drawSection (g, masterArea, "MASTER");
    drawSection (g, nudgeArea, "NUDGE");

    // meter
    g.setColour (t.dim);
    g.drawRect (meterArea.toFloat(), 1.0f);
    const float level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (meter, -60.0f) + 60.0f) / 60.0f);
    const int segs = 24;
    const float segW = (float) (meterArea.getWidth() - 4) / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const bool lit = (float) i < level * (float) segs;
        const float x = (float) meterArea.getX() + 2.0f + (float) i * segW;
        g.setColour (lit ? (i >= segs - 2 ? t.fg.brighter (0.4f) : t.fg) : t.dim.withAlpha (0.25f));
        g.fillRect (x + 1.0f, (float) meterArea.getY() + 3.0f, segW - 2.0f, (float) meterArea.getHeight() - 6.0f);
    }

    g.setFont (congo::monoFont (13.0f));
    g.setColour (t.fg);
    g.drawText ("> " + status + "_", statusArea, juce::Justification::centredLeft, true);
}

void CongoFilterEditor::paintCanvasOver (juce::Graphics& g)
{
    const auto& t = lnf.getTheme();
    g.setColour (juce::Colours::black.withAlpha (lnf.getTheme().light() ? 0.035f : 0.16f));
    for (int y = 0; y < baseH; y += 3)
        g.fillRect (0, y, baseW, 1);

    const auto screen = juce::Rectangle<int> (0, 0, baseW, baseH).toFloat().reduced (4.0f);
    juce::ColourGradient vignette (juce::Colours::transparentBlack, screen.getCentreX(), screen.getCentreY(),
                                   juce::Colours::black.withAlpha (lnf.getTheme().light() ? 0.12f : 0.45f),
                                   screen.getX(), screen.getY(), true);
    g.setGradientFill (vignette);
    g.fillRoundedRectangle (screen, 26.0f);

    g.setColour (t.dim.darker (0.3f));
    g.drawRoundedRectangle (screen, 26.0f, 3.0f);
}

// ---------------------------------------------------------------------------------
void CongoFilterEditor::placeKnobs (Knob* const* ks, int count, juce::Rectangle<int> area)
{
    const int w = area.getWidth() / count;
    for (int i = 0; i < count; ++i)
    {
        auto cell = area.removeFromLeft (w);
        ks[i]->label.setBounds (cell.removeFromTop (18));
        ks[i]->slider.setBounds (cell.reduced (4, 0));
    }
}

void CongoFilterEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    canvas.setBounds (0, 0, baseW, baseH);
    canvas.setTransform (juce::AffineTransform::scale (s));
    proc.apvts.state.setProperty (juce::Identifier (scaleProp), (double) s, nullptr);
}

void CongoFilterEditor::layoutCanvas()
{
    {   // PRESET bar
        juce::Rectangle<int> r (756, 22, 460, 28);
        presetPrev.setBounds (r.removeFromLeft (28));  r.removeFromLeft (4);
        presetBox.setBounds (r.removeFromLeft (240));  r.removeFromLeft (4);
        presetNext.setBounds (r.removeFromLeft (28));  r.removeFromLeft (8);
        presetSave.setBounds (r.removeFromLeft (64));  r.removeFromLeft (4);
        presetDelete.setBounds (r.removeFromLeft (76));
    }
    toggles[tTest].button.setBounds (baseW - 206, 22, 88, 28);
    resetAllButton.setBounds (baseW - 430, 22, 96, 28);
    if (tempoDisplay) tempoDisplay->setBounds (baseW - 590, 8, 150, 46);
    toggles[tHost].button.setBounds (baseW - 656, 22, 60, 28);
    killButton.setBounds  (baseW - 328, 22, 114, 28);
    themeButton.setBounds (baseW - 112, 22, 88, 28);

    samplerArea = { 18, 70, 840, 262 };
    toggles[tTight].button.setBounds (samplerArea.getRight() - 150, samplerArea.getY() - 10, 136, 20);   // on the title line
    filterArea  = { 18, 348, 840, 214 };
    unitsArea   = { 872, 70, 230, 262 };
    outputArea  = { 872, 348, 230, 214 };
    ebArea      = { 1116, 70, 620, 492 };
    statusArea  = { 26, 572, 1710, 24 };
    masterArea  = { 1750, 70, 140, 492 };
    nudgeArea   = { 1758, 392, 124, 162 };
    {   // MASTER: fader with meters behind, the scale on the left
        auto col = masterArea.reduced (8, 0).withTrimmedTop (18).withBottom (nudgeArea.getY() - 14);
        auto panRow = col.removeFromTop (70);
        clipLights->setBounds (col.removeFromTop (16));                  // clip lights: above the fader's travel
        col.removeFromTop (4);
        knobs[kMasterPan].label.setBounds (panRow.removeFromLeft (34).withTrimmedTop (16).withHeight (16));
        knobs[kMasterPan].slider.setBounds (panRow.withTrimmedRight (20));
        toggles[tMasterLock].button.setBounds (col.removeFromBottom (20).reduced (14, 0));
        col.removeFromBottom (4);
        masterStrip->setBounds (col);
        knobs[kMaster].slider.setBounds (col.withTrimmedLeft (34).withTrimmedRight (6));
    }
    {   // NUDGE under the fader (it moves the output): EARLY/LATE, the readout, coarse, fine, RESET
        auto r = nudgeArea.reduced (6, 0).withTrimmedTop (13).withTrimmedBottom (4);
        auto pair = [] (juce::Rectangle<int> row, juce::Button& a, juce::Button& b)
        {
            a.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
            row.removeFromLeft (4);
            b.setBounds (row);
        };
        pair (r.removeFromTop (22), nudgeEarly, nudgeLate);
        r.removeFromTop (2);
        nudgeReadout.setBounds (r.removeFromTop (20));
        nudgeCoarseLabel.setBounds (r.removeFromTop (12));
        pair (r.removeFromTop (22), nudgeCoarseEarly, nudgeCoarseLate);
        nudgeFineLabel.setBounds (r.removeFromTop (12));
        pair (r.removeFromTop (22), nudgeFineEarly, nudgeFineLate);
        r.removeFromTop (4);
        nudgeReset.setBounds (r.withSizeKeepingCentre (64, juce::jmin (18, r.getHeight())));
    }

    // sampler: selector row + 10 knobs
    {
        auto row = samplerArea.reduced (14, 0).withTrimmedTop (18).withHeight (52);
        auto button = [&row] (juce::Button& b, int w)
        {
            auto cell = row.removeFromLeft (w + 6);
            cell.removeFromTop (20);
            b.setBounds (cell.withWidth (w).withHeight (28));
        };
        auto choice = [&row] (Choice& c, int w)
        {
            auto cell = row.removeFromLeft (w + 6);
            c.label.setBounds (cell.removeFromTop (20).withWidth (w));
            c.box.setBounds (cell.withWidth (w).withHeight (28));
        };
        button (toggles[tModel].button, 80);
        button (modeButton, 88);
        choice (choices[cTrigger], 86);
        choice (choices[cRetrig], 80);
        choice (choices[cType], 80);
        choice (choices[cFade], 72);
        button (toggles[tAutoD].button, 66);
        button (toggles[tSamplerOn].button, 76);
        button (toggles[tSync].button, 62);
        button (toggles[tCycSync].button, 62);

        Knob* ks[] = { &knobs[kTime], &knobs[kPitch], &knobs[kFine], &knobs[kCycle], &knobs[kQuality],
                       &knobs[kXfd], &knobs[kBits], &knobs[kRate], &knobs[kThresh] };
        placeKnobs (ks, 9, samplerArea.reduced (10, 0).withTrimmedTop (84).withTrimmedBottom (10));
        // SYNC on: TIME and CYCLE switch to their musical versions in the same places
        knobs[kRatio].slider.setBounds (knobs[kTime].slider.getBounds());
        knobs[kRatio].label.setBounds (knobs[kTime].label.getBounds());
        knobs[kCycleDiv].slider.setBounds (knobs[kCycle].slider.getBounds());
        knobs[kCycleDiv].label.setBounds (knobs[kCycle].label.getBounds());
    }

    // filter: engine button + mode + on, 5 knobs
    {
        auto row = filterArea.reduced (16, 0).withTrimmedTop (18).withHeight (52);
        auto e = row.removeFromLeft (150);
        e.removeFromTop (20);
        toggles[tEngine].button.setBounds (e.withHeight (28).withWidth (140));
        auto mc = row.removeFromLeft (140);
        choices[cMode].label.setBounds (mc.removeFromTop (20).withWidth (132));
        choices[cMode].box.setBounds (mc.withWidth (132).withHeight (28));
        row.removeFromTop (20);
        toggles[tFilterOn].button.setBounds (row.removeFromLeft (96).withHeight (28));

        Knob* ks[] = { &knobs[kCutoff], &knobs[kReso], &knobs[kDrive], &knobs[kBassKeep], &knobs[kAnalog] };
        placeKnobs (ks, 5, filterArea.reduced (10, 0).withTrimmedTop (84).withTrimmedBottom (8));
    }

    // effects units: ORDER, then FX1 FX2 RV1 RV2 - name (opens its page), ON, WET
    {
        using namespace cba::eb16;
        auto col = unitsArea.reduced (12, 0).withTrimmedTop (18).withTrimmedBottom (6);
        if (auto* c = findEb (GLOB + Order))
        {
            c->choice->label.setVisible (false);                  // the menu reads "FX1 - FX2 - RV1 - RV2"
            c->choice->box.setBounds (col.removeFromTop (26));
        }
        col.removeFromTop (8);
        const int onIdx[4]  = { FX1 + FxOn,  FX2 + FxOn,  RV1 + RvOn,  RV2 + RvOn };
        const int mixIdx[4] = { FX1 + FxMix, FX2 + FxMix, RV1 + RvMix, RV2 + RvMix };
        const int cw = col.getWidth() / 2, ch = (col.getHeight() - 6) / 2;
        for (int u = 0; u < 4; ++u)
        {
            juce::Rectangle<int> cell (col.getX() + (u % 2) * cw, col.getY() + (u / 2) * (ch + 6), cw, ch);
            cell = cell.reduced (3, 0);
            auto head = cell.removeFromTop (26);
            unitButtons[(size_t) u].setBounds (head.removeFromLeft (head.getWidth() - 44).withTrimmedRight (4));
            if (auto* c = findEb (onIdx[u])) c->toggle->button.setBounds (head);
            if (auto* c = findEb (mixIdx[u]))
            {
                c->knob->label.setVisible (false);                // the value reads "100 %"; tooltip says WET
                c->knob->slider.setBounds (cell.withTrimmedTop (2));
            }
        }
    }

    // freeze / output, level with the filter
    {
        auto col = outputArea.reduced (12, 0).withTrimmedTop (18).withTrimmedBottom (6);
        auto top = col.removeFromTop (28);
        toggles[tFreeze].button.setBounds (top.removeFromLeft (76));
        top.removeFromLeft (4);
        toggles[tFrzHold].button.setBounds (top.removeFromLeft (60));
        top.removeFromLeft (4);
        toggles[tFrzSync].button.setBounds (top);
        col.removeFromTop (6);
        auto src = col.removeFromTop (24);
        choices[cFrzSource].label.setBounds (src.removeFromLeft (64));
        choices[cFrzSource].box.setBounds (src);
        col.removeFromTop (4);
        meterArea = col.removeFromBottom (14);
        col.removeFromBottom (4);
        Knob* sz[] = { &knobs[kFrzMs], &knobs[kMix], &knobs[kOutput] };
        placeKnobs (sz, 3, col);
        Knob* sz2[] = { &knobs[kFrzDiv] };
        placeKnobs (sz2, 1, col.withWidth (col.getWidth() / 3));
        // SIZE's LOCK shares SIZE's label line (both SIZE knobs use the same spot)
        auto sizeLabel = knobs[kFrzMs].label.getBounds();
        toggles[tFrzLock].button.setBounds (sizeLabel.removeFromRight (34).withTrimmedTop (1).withTrimmedBottom (1));
        knobs[kFrzMs].label.setBounds (sizeLabel);
        knobs[kFrzDiv].label.setBounds (sizeLabel);
        // MIX's LOCK shares MIX's own label line, so the knobs keep their full size
        auto mixLabel = knobs[kMix].label.getBounds();
        toggles[tMixLock].button.setBounds (mixLabel.removeFromRight (34).withTrimmedTop (1).withTrimmedBottom (1));
        knobs[kMix].label.setBounds (mixLabel);
    }

    // Effects Board sub-pages (FX1 / FX2)
    {
        auto subs = ebArea.reduced (14, 0).withTrimmedTop (18).withHeight (26);
        const int tw = (subs.getWidth() - 3 * 6) / 4;
        for (int k = 0; k < 4; ++k) { ebSubTabs[(size_t) k].setBounds (subs.removeFromLeft (tw));  subs.removeFromLeft (6); }
    }
}
