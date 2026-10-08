#include "PluginProcessor.h"
#include "PluginEditor.h"

CongoFilterProcessor::CongoFilterProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CONGO", congo::createLayout())
{
    const char* ids[pCount] = { congo::id::model, congo::id::type, congo::id::fade, congo::id::autoD,
        congo::id::retrig, congo::id::samplerOn, congo::id::time, congo::id::pitch, congo::id::fine,
        congo::id::cycle, congo::id::quality, congo::id::xfd, congo::id::bits, congo::id::rate,
        congo::id::filterOn, congo::id::akai, congo::id::fmode, congo::id::cutoff, congo::id::reso,
        congo::id::drive, congo::id::bassKeep, congo::id::analog, congo::id::mix, congo::id::output,
        congo::id::test, congo::id::mode, congo::id::trigger, congo::id::thresh, congo::id::kill,
        congo::id::freeze, congo::id::frzSource, congo::id::frzSync, congo::id::frzMs, congo::id::frzDiv,
        congo::id::syncOn, congo::id::syncRatio, congo::id::cycleDiv, congo::id::tempoHost, congo::id::tempo,
        congo::id::cycleSync, congo::id::nudge, congo::id::master, congo::id::frzHold, congo::id::masterPan, congo::id::tight };
    for (int i = 0; i < pCount; ++i)
    {
        p[i] = apvts.getRawParameterValue (ids[i]);
        jassert (p[i] != nullptr);
    }
    for (const auto& sp : cba::eb16::specs())
    {
        ebParams.push_back (apvts.getRawParameterValue (sp.id.c_str()));
        jassert (ebParams.back() != nullptr);
        ebValues.push_back (sp.def);
    }
}

bool CongoFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void CongoFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlock = juce::jmax (1, samplesPerBlock);

    sampler.prepare (sr, 2, 12.0);
    sustain.prepare (sr, 10.0);
    frontEnd.prepare (sr);
    filter.prepare (sr, 2, 2);
    testTone.prepare (sr);
    freeze.prepare (sr, 8.0);
    held.prepare (sr, 8.5);
    holdPrev = false;
    eb16.prepare (sr);

    latency = filter.getLatencySamples();
    for (int c = 0; c < 2; ++c) bypassDelay[c].reset (latency);
    const int nudgeMax = (int) std::ceil (0.25 * sr);                     // +-250 ms
    const int lookAheadMax = (int) std::ceil (0.1 * sr);                  // TIGHT: 100 ms
    dryPath.prepare (latency + nudgeMax + lookAheadMax, (int) (0.01 * sr));
    wetNudge.prepare (nudgeMax, (int) (0.01 * sr));
    inLook.prepare (lookAheadMax, (int) (0.01 * sr));
    encLook.prepare (lookAheadMax, (int) (0.01 * sr));
    reportedLatency = -1;

    dry.setSize (2, maxBlock, false, true, true);
    preFilter.setSize (2, maxBlock, false, true, true);
    killEnv.setSize (1, maxBlock, false, true, true);

    mixGain.reset (sr, 0.02);  outGain.reset (sr, 0.02);  masterGain.reset (sr, 0.02);
    panL.reset (sr, 0.02);  panR.reset (sr, 0.02);
    samplerGate.reset (sr, 0.01);  filterGate.reset (sr, 0.01);  modeGate.reset (sr, 0.01);
    updateParameters();
    mixGain.setCurrentAndTargetValue (mixGain.getTargetValue());
    outGain.setCurrentAndTargetValue (outGain.getTargetValue());
    masterGain.setCurrentAndTargetValue (masterGain.getTargetValue());
    panL.setCurrentAndTargetValue (panL.getTargetValue());
    panR.setCurrentAndTargetValue (panR.getTargetValue());
    samplerGate.setCurrentAndTargetValue (samplerGate.getTargetValue());
    filterGate.setCurrentAndTargetValue (filterGate.getTargetValue());
    modeGate.setCurrentAndTargetValue (modeGate.getTargetValue());

    envRelease  = (float) std::exp (-1.0 / (0.03 * sr));     // 30 ms level follower
    holdSamples = (int) (0.25 * sr);                          // phrase ends after 250 ms quiet
    killStepDown = 1.0f / (float) (0.010 * sr);               // KILL: 10 ms fade out
    killStepUp   = 1.0f / (float) (0.005 * sr);               //       5 ms back in, fresh

    applyNudge (true);
    lastStep = lastRecStep = std::numeric_limits<int64_t>::min();
    env = 0.0f;  gateOpen = false;  below = 0;  midiNote = -1;
    testPrev = testPhrase = killPrev = false;
    killStage = 0;  killGain = 1.0f;
    wasSustain = sustainMode;
}

cba::VoiceSettings CongoFilterProcessor::voiceSettings (double extraSemis) const noexcept
{
    auto v = [this] (P i) { return p[i]->load(); };
    cba::VoiceSettings s;
    s.model      = v (pModel) > 0.5f ? cba::CongoSampler::Model::S2000 : cba::CongoSampler::Model::S950;
    s.type       = v (pType)  > 0.5f ? cba::CongoSampler::Type::Intelligent : cba::CongoSampler::Type::Cyclic;
    s.timePct    = timePercent();
    s.pitchSemis = juce::jlimit (-24.5, 24.5, (double) v (pPitch) + v (pFine) / 100.0 + extraSemis);
    s.cycle      = (int) std::lround (v (pCycle));
    s.minDelay   = lookAhead;
    s.extended   = s.model == cba::CongoSampler::Model::S2000 || v (pCycleSync) > 0.5f;
    s.cycleHost  = syncedCycle (s.model == cba::CongoSampler::Model::S2000,
                                s.type == cba::CongoSampler::Type::Intelligent, v (pAutoD) > 0.5f);
    s.poly       = v (pFade)  > 0.5f;
    s.autoD      = v (pAutoD) > 0.5f;
    s.quality    = (int) std::lround (v (pQuality));
    s.xfd        = (int) std::lround (v (pXfd));
    s.hwRate     = v (pRate);
    s.bits       = (int) std::lround (v (pBits));
    return s;
}

void CongoFilterProcessor::updateParameters()
{
    auto v = [this] (P i) { return p[i]->load(); };

    cba::CongoSampler::Params sp;
    sp.model      = v (pModel) > 0.5f ? cba::CongoSampler::Model::S2000 : cba::CongoSampler::Model::S950;
    sp.type       = v (pType)  > 0.5f ? cba::CongoSampler::Type::Intelligent : cba::CongoSampler::Type::Cyclic;
    sp.poly       = v (pFade)  > 0.5f;
    sp.autoD      = v (pAutoD) > 0.5f;
    sp.timePct    = timePercent();
    sp.pitchSemis = v (pPitch) + v (pFine) / 100.0;
    sp.cycle      = (int) std::lround (v (pCycle));
    sp.quality    = (int) std::lround (v (pQuality));
    sp.xfd        = (int) std::lround (v (pXfd));
    sp.hwRate     = v (pRate);
    sp.maxLag     = (int) (2.0 * sr);
    sp.minLag     = lookAhead;
    sp.extended   = sp.model == cba::CongoSampler::Model::S2000 || v (pCycleSync) > 0.5f;
    sp.cycleHost  = syncedCycle (sp.model == cba::CongoSampler::Model::S2000,
                                 sp.type == cba::CongoSampler::Type::Intelligent, sp.autoD);
    sampler.setParams (sp);

    pitchRatio = std::exp2 (sp.pitchSemis / 12.0);
    frontEnd.set (v (pRate), (int) std::lround (v (pBits)), pitchRatio,
                  sp.model == cba::CongoSampler::Model::S950 ? 0.4 : 0.45);

    cba::CBAFilter::Params fp;
    fp.engine    = v (pAkai) > 0.5f ? cba::CBAFilter::Engine::Akai : cba::CBAFilter::Engine::Moog;
    fp.mode      = (cba::CBAFilter::Mode) juce::jlimit (0, 5, (int) std::lround (v (pFmode)));
    fp.cutoffHz  = v (pCutoff);
    fp.resonance = v (pReso) / 100.0f;
    fp.driveDb   = v (pDrive);
    fp.bassKeep  = v (pBassKeep) / 100.0f;
    fp.analog    = v (pAnalog) / 100.0f;
    fp.outputDb  = 0.0f;
    filter.setParams (fp);

    mixGain.setTargetValue (v (pMix) / 100.0f);
    outGain.setTargetValue (v (pOutput) <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (v (pOutput)));   // frozen sound only
    masterGain.setTargetValue (v (pMaster) <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (v (pMaster)));
    {   // MASTER PAN as a balance: centre leaves both sides untouched, turning one way fades the other
        const float pan = juce::jlimit (-1.0f, 1.0f, v (pMasterPan) / 100.0f);
        panL.setTargetValue (pan > 0.0f ? std::cos (pan * juce::MathConstants<float>::halfPi) : 1.0f);
        panR.setTargetValue (pan < 0.0f ? std::cos (-pan * juce::MathConstants<float>::halfPi) : 1.0f);
    }
    samplerGate.setTargetValue (v (pSamplerOn) > 0.5f ? 1.0f : 0.0f);
    filterGate.setTargetValue  (v (pFilterOn)  > 0.5f ? 1.0f : 0.0f);

    const int modeIdx = juce::jlimit (0, 2, (int) std::lround (v (pMode)));
    sustainMode = modeIdx >= 1;                     // SUSTAIN and SAMPLE both play phrases from memory
    sampleMode  = modeIdx == 2;
    lookAhead   = v (pTight) > 0.5f ? (int) std::lround (0.1 * sr) : 0;
    modeGate.setTargetValue (sustainMode ? 1.0f : 0.0f);
    triggerMode = juce::jlimit (0, 2, (int) std::lround (v (pTrigger)));
    threshLin   = juce::Decibels::decibelsToGain (v (pThresh));

    testTone.setActive (v (pTest) > 0.5f);

    for (size_t k = 0; k < ebParams.size(); ++k) ebValues[k] = ebParams[k]->load();

    freezeSource = juce::jlimit (0, 2, (int) std::lround (v (pFrzSource)));
    if (held.active) freezeSource = lastFreezeSource;                  // HOLD: SOURCE waits until it's released
    if (freezeSource != lastFreezeSource) { freeze.reset();  lastFreezeSource = freezeSource; }
    {
        const double beat = 60.0 / juce::jmax (1.0, bpm);
        const auto& divs = congo::freezeDivs();
        const int di = juce::jlimit (0, (int) divs.size() - 1, (int) std::lround (v (pFrzDiv)));
        const double secs = v (pFrzSync) > 0.5f ? divs[(size_t) di].quarters * beat : v (pFrzMs) / 1000.0;
        freeze.setSize (secs * sr);
        freeze.setFrozen (v (pFreeze) > 0.5f);
    }

    static constexpr double div[] = { 0.0, 0.125, 0.25, 0.5, 1.0, 2.0, -1.0, -2.0 };
    stepQuarters = div[juce::jlimit (0, 7, (int) std::lround (v (pRetrig)))];
}

// NUDGE: later = delay the processed sound; earlier = ask the host to compensate extra
// latency and delay the dry path by the same amount, so only the processed sound moves earlier.
void CongoFilterProcessor::applyNudge (bool force)
{
    const double ms = p[pNudge]->load();
    const int late  = (int) std::lround (juce::jmax (0.0, ms) * 0.001 * sr);
    const int early = (int) std::lround (juce::jmax (0.0, -ms) * 0.001 * sr);
    wetNudge.setDelay (late);
    inLook.setDelay (lookAhead);
    encLook.setDelay (lookAhead);
    dryPath.setDelay (latency + early + lookAhead);
    const int want = latency + early + lookAhead;
    if (force)
    {
        wetNudge.cur = wetNudge.next = late;               wetNudge.fade = 0;
        dryPath.cur  = dryPath.next  = latency + early + lookAhead;    dryPath.fade  = 0;
        inLook.cur  = inLook.next  = lookAhead;  inLook.fade  = 0;
        encLook.cur = encLook.next = lookAhead;  encLook.fade = 0;
    }
    if (force || want != reportedLatency)
    {
        reportedLatency = want;
        setLatencySamples (want);
    }
}

double CongoFilterProcessor::timePercent() const noexcept
{
    if (p[pSyncOn]->load() > 0.5f)
    {
        const auto& r = congo::syncRatios();
        const int i = juce::jlimit (0, (int) r.size() - 1, (int) std::lround (p[pSyncRatio]->load()));
        return r[(size_t) i].ratio * 100.0;
    }
    return p[pTime]->load();
}

// C SYNC: lock the audible jump rate (the period) to a note value, and work the cycle out
// from it. Period P = C * s / |s - 1| at the internal stretch s (TIME x pitch ratio), and
// the period plays back at the pitch ratio r, so P_internal = note length x r.
double CongoFilterProcessor::syncedCycle (bool s2000, bool intell, bool autoD) const noexcept
{
    (void) s2000;
    if (p[pCycleSync]->load() < 0.5f || intell || autoD) return 0.0;   // these pick their own cycle
    const auto& d = congo::cycleDivs();
    const int i = juce::jlimit (0, (int) d.size() - 1, (int) std::lround (p[pCycleDiv]->load()));
    const double r = std::exp2 ((p[pPitch]->load() + p[pFine]->load() / 100.0) / 12.0);
    const double s = timePercent() / 100.0 * r;
    const double periodOut = d[(size_t) i].quarters * 60.0 / juce::jmax (1.0, bpm) * sr;
    const double gap = std::abs (s - 1.0);
    if (gap < 1.0e-4) return periodOut * r;                            // 100 %: no jumps at all
    return juce::jmax (1.0, periodOut * r * gap / s);
}

// FREEZE at its chosen point. OUTPUT is the frozen sound's own level; HOLD keeps the loop
// going with the live sound back underneath it.
void CongoFilterProcessor::freezeAt (int where, float& l, float& r) noexcept
{
    if (where != freezeSource) return;
    const float level = outGain.getNextValue();                      // once per sample
    // while HOLD's printed layer plays, the freeze here steps aside: live sound back, loop muted
    freeze.process (l, r, held.active ? 0.0f : level, held.active || p[pFrzHold]->load() > 0.5f);
}

// shiftSamples: TIGHT plays the stretch 'lookAhead' late and the host makes up for it, so a
// LIVE retrigger fires that much later (reading from the true step position)
bool CongoFilterProcessor::stepBoundary (int64_t blockSample, double shiftSamples, int64_t& last) noexcept
{
    if (! synced) { last = std::numeric_limits<int64_t>::min();  return false; }
    const double ppq  = ppqAtBlock + ((double) blockSample - shiftSamples) * bpm / (60.0 * sr);
    const auto   step = (int64_t) std::floor (ppq / stepQuarters + 1.0e-9);
    if (step == last) return false;
    last = step;
    return true;
}

void CongoFilterProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (! sustainMode || sampleMode || triggerMode != 2 || killStage == 1) return;
    if (m.isNoteOn())
    {
        midiNote = m.getNoteNumber();
        sustain.startPhrase (voiceSettings (midiNote - 60));          // C3 (60) = no transpose
    }
    else if ((m.isNoteOff() && m.getNoteNumber() == midiNote) || m.isAllNotesOff())
    {
        sustain.endPhrase();
        midiNote = -1;
    }
}

void CongoFilterProcessor::startKill() noexcept
{
    killStage = 1;
    sustain.killAll ((int) (0.010 * sr));
    gateOpen = false;  testPhrase = false;  midiNote = -1;
}

void CongoFilterProcessor::finishKill() noexcept
{
    // everything forgotten: live history, phrases, converter and filter state
    sampler.reset();
    sustain.clear();
    wetNudge.clear();
    frontEnd.reset();
    freeze.reset();
    held.clear();
    holdPrev = false;
    filterResetPending = true;            // at the next block start, once only silence goes in
    env = 0.0f;  below = 0;
    lastStep = lastRecStep = std::numeric_limits<int64_t>::min();
    killStage = 2;
}

void CongoFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);

    const int nc = juce::jmin (2, numOut, buffer.getNumChannels());
    if (nc <= 0 || n <= 0) return;

    updateParameters();
    applyNudge (false);

    // ---- host transport ---------------------------------------------------------------
    // ---- tempo: HOST on = the host's tempo and transport; HOST off = your own tempo with an
    //      internal clock that always runs (RETRIG / sync work without the transport) -------
    const bool hostMode = p[pTempoHost]->load() > 0.5f;
    playing = false;
    bool hostBpm = false;
    double barQuarters = 4.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) { if (hostMode) bpm = *b;  hostBpm = true; }
            if (auto q = pos->getPpqPosition()) ppqAtBlock = *q; else playing = false;
            if (auto ts = pos->getTimeSignature())
                if (ts->denominator > 0) barQuarters = 4.0 * ts->numerator / ts->denominator;
        }
    }
    if (! hostMode || ! hostBpm) bpm = p[pTempo]->load();
    const bool hostPlaying = hostMode && playing;
    if (! hostMode) { playing = true;  ppqAtBlock = internalPpq; }
    internalPpq += (double) n * bpm / (60.0 * sr);
    uiBeat.store (hostPlaying ? ppqAtBlock + (double) n * bpm / (60.0 * sr) : internalPpq);
    {   // Effects Board tempo sync: the beat the processed sound belongs to (TIGHT plays it look-ahead late)
        const double blockBeat = hostPlaying ? ppqAtBlock : internalPpq - (double) n * bpm / (60.0 * sr);
        eb16.setTempo (bpm, blockBeat - (double) lookAhead * bpm / (60.0 * sr));
    }
    if (stepQuarters < 0.0) stepQuarters = -stepQuarters * barQuarters;
    synced = playing && stepQuarters > 0.0 && bpm > 1.0;
    uiBpm.store ((float) bpm);
    uiHostTempo.store (hostMode && hostBpm);
    const int64_t stepSamples = synced ? (int64_t) std::llround (stepQuarters * 60.0 / bpm * sr) : 0;

    // ---- buttons: KILL, TEST, mode change (block start) --------------------------------
    const bool killNow = p[pKill]->load() > 0.5f;
    if (killNow && ! killPrev) startKill();
    killPrev = killNow;

    if (wasSustain && ! sustainMode) sustain.killAll ((int) (0.010 * sr));   // leaving SUSTAIN
    wasSustain = sustainMode;

    const bool testNow = p[pTest]->load() > 0.5f;
    if (testNow && ! testPrev && killStage != 1)
    {
        if (sampleMode) {}                                             // SAMPLE records it in the next step
        else if (sustainMode) { sustain.startPhrase (voiceSettings (0.0));  testPhrase = true; }
        else             sampler.trigger (stepSamples, false);        // hear the tone straight away (not a grid event)
    }
    if (! testNow && testPrev && testPhrase) { sustain.endPhrase();  testPhrase = false; }
    testPrev = testNow;

    auto midiIt = midi.begin();
    float peak = 0.0f, mPeak[2] = { 0.0f, 0.0f };

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        float* ch[2] = { buffer.getWritePointer (0, start),
                         buffer.getWritePointer (nc > 1 ? 1 : 0, start) };

        if (filterResetPending)
        {
            filter.reset();
            eb16.reset();
            for (auto& d : bypassDelay) d.reset (latency);
            filterResetPending = false;
        }

        testTone.replaceInput (ch, nc, len);                     // TEST feeds the whole chain
        for (int c = 0; c < nc; ++c) dry.copyFrom (c, 0, ch[c], len);
        float* kenv = killEnv.getWritePointer (0, 0);

        for (int i = 0; i < len; ++i)
        {
            const int64_t t = (int64_t) (start + i);

            // MIDI notes at their exact sample
            while (midiIt != midi.end() && (*midiIt).samplePosition <= (int) t)
            {
                handleMidi ((*midiIt).getMessage());
                ++midiIt;
            }

            float in[2] = { ch[0][i], nc > 1 ? ch[1][i] : ch[0][i] };
            freezeAt (0, in[0], in[1]);                                      // FREEZE: Input
            // LIVE: shifted for TIGHT, and fired one retrigger-fade early so the fade is done by the beat
            const bool step = stepBoundary (t, (double) (lookAhead - cba::CongoSampler::declickLen), lastStep);
            const bool recStep = stepBoundary (t, 0.0, lastRecStep);            // SUSTAIN / SAMPLE recording

            // ---- LIVE: follows the input --------------------------------------------------
            if (! sustainMode && killStage != 1)
            {
                if (synced) { if (step) sampler.trigger (stepSamples); }
                else if (! sampler.isActive()) sampler.trigger (0);        // free-run
            }
            float live[2] = { in[0], in[1] };
            frontEnd.encode (live, 2);
            float encLate[2] = { live[0], live[1] };
            encLook.process (encLate[0], encLate[1]);                        // TIGHT: pass-through waits too
            const bool samplerPlaying = sampler.isActive();
            sampler.processFrame (live);                                     // always fed
            if (! samplerPlaying) { live[0] = encLate[0];  live[1] = encLate[1]; }
            frontEnd.decode (live, 2);
            float inLate[2] = { in[0], in[1] };                              // the un-stretched input, in time
            inLook.process (inLate[0], inLate[1]);

            // ---- SUSTAIN: phrase triggering -------------------------------------------------
            if (sampleMode && killStage != 1)
            {
                // SAMPLE: each RETRIG step is recorded, then played from its start (stretched and
                // pitched, like a sampler playing a sample) on the next step; it cuts the last one off
                if (recStep)
                {
                    auto vs = voiceSettings (0.0);
                    vs.waitForEnd = true;  vs.choke = true;  vs.delayAfterEnd = lookAhead;  vs.minDelay = 0;
                    sustain.endPhrase (&vs);                                  // the step just recorded: play it
                    sustain.startPhrase (vs);                                 // and record the next one
                }
                else if (! synced && sustain.isRecording()) sustain.endPhrase();
            }
            else if (sustainMode && killStage != 1 && ! testPhrase)
            {
                if (triggerMode == 0)                                        // Level
                {
                    const float a = juce::jmax (std::abs (in[0]), std::abs (in[1]));
                    env = juce::jmax (a, env * envRelease);
                    if (! gateOpen)
                    {
                        if (env > threshLin) { gateOpen = true;  below = 0;  sustain.startPhrase (voiceSettings (0.0)); }
                    }
                    else
                    {
                        if (env < threshLin * 0.5f) { if (++below > holdSamples) { gateOpen = false;  sustain.endPhrase(); } }
                        else below = 0;
                        if (gateOpen && ! sustain.isRecording() && env > threshLin)   // memory full: next phrase
                            sustain.startPhrase (voiceSettings (0.0));
                    }
                }
                else if (triggerMode == 1)                                   // Retrig
                {
                    if (recStep) sustain.startPhrase (voiceSettings (0.0));
                    else if (! synced && sustain.isRecording()) sustain.endPhrase();
                }
            }
            float sus[2] = { 0.0f, 0.0f };
            sustain.processFrame (in, sus);

            // ---- mode crossfade, sampler on/off, KILL envelope -----------------------------
            const float mg = modeGate.getNextValue();
            const float g  = samplerGate.getNextValue();
            float smp[2];
            for (int c = 0; c < 2; ++c)
            {
                const float wet = live[c] + mg * (sus[c] - live[c]);
                smp[c] = inLate[c] + g * (wet - inLate[c]);
            }
            freezeAt (1, smp[0], smp[1]);                                    // FREEZE: Sampler
            for (int c = 0; c < nc; ++c) ch[c][i] = smp[c];

            if (killStage == 1)
            {
                killGain -= killStepDown;
                if (killGain <= 0.0f) { killGain = 0.0f;  finishKill(); }
            }
            else if (killStage == 2)
            {
                killGain += killStepUp;
                if (killGain >= 1.0f) { killGain = 1.0f;  killStage = 0; }
            }
            kenv[i] = killGain;
            for (int c = 0; c < nc; ++c) ch[c][i] *= killGain;          // nothing old reaches the filter
        }

        // ---- filter (bypass path delayed by the same latency) -----------------------------
        for (int c = 0; c < nc; ++c) preFilter.copyFrom (c, 0, ch[c], len);
        filter.process (ch, nc, len);
        for (int i = 0; i < len; ++i)
        {
            const float g = filterGate.getNextValue();
            for (int c = 0; c < nc; ++c)
            {
                const float b = bypassDelay[c].process (preFilter.getSample (c, i));
                ch[c][i] = b + g * (ch[c][i] - b);
            }
            // ---- Effects Board, after the filter -----------------------------------------
            float l = ch[0][i], r = nc > 1 ? ch[1][i] : ch[0][i];
            eb16.processFrame (ebValues.data(), l, r);
            ch[0][i] = l;
            if (nc > 1) ch[1][i] = r;
        }

        // ---- NUDGE + mix + output (KILL silences the processed signal; live dry passes) ---
        for (int i = 0; i < len; ++i)
        {
            const float m = mixGain.getNextValue();
            float dl = dry.getSample (0, i), dr = nc > 1 ? dry.getSample (1, i) : dl;
            dryPath.process (dl, dr);
            float wl = ch[0][i] * kenv[i], wr = (nc > 1 ? ch[1][i] : ch[0][i]) * kenv[i];
            wetNudge.process (wl, wr);
            const float dIn[2] = { dl, dr }, wIn[2] = { wl, wr };
            for (int c = 0; c < nc; ++c)
                ch[c][i] = dIn[c] + m * (wIn[c] - dIn[c]);
            const float frozenPart[2] = { m * wl, m * wr };                   // the wet (frozen) share
            float l = ch[0][i], r = nc > 1 ? ch[1][i] : ch[0][i];
            freezeAt (2, l, r);                                              // FREEZE: Output
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
            const float mg = masterGain.getNextValue();                      // MASTER fader + pan
            const float pl = nc > 1 ? panL.getNextValue() : 1.0f, pr = panR.getNextValue();
            l *= mg * pl;  r *= mg * pr;

            // HOLD: keep a record of what the frozen sound sounds like at the output; print one loop
            // of it the moment HOLD goes on, then play that as its own layer, after everything
            if (freezeSource == 2) held.record (l, r);
            else held.record (frozenPart[0] * mg * pl, (nc > 1 ? frozenPart[1] : frozenPart[0]) * mg * pr);
            held.play (l, r, outGain.getCurrentValue());
            // (capture after playing, so the held layer takes over from the very next sample)
            const bool holdNow = p[pFreeze]->load() > 0.5f && p[pFrzHold]->load() > 0.5f && freeze.isFrozen();
            if (holdNow && ! holdPrev) held.capture (freeze.currentLoopLength(), outGain.getCurrentValue());
            if (! holdNow && holdPrev) held.release();
            holdPrev = holdNow;
            mPeak[0] = juce::jmax (mPeak[0], std::abs (l));
            mPeak[1] = juce::jmax (mPeak[1], std::abs (nc > 1 ? r : l));
            ch[0][i] = l;
            if (nc > 1) ch[1][i] = r;
        }
    }

    // ---- status for the front panel ---------------------------------------------------
    const float rate = p[pRate]->load();
    const int cyc = sustainMode ? sustain.newestCycle() : sampler.getCurrentCycle();
    uiCycleMs.store ((float) (cyc * 1000.0 / sr));
    {
        const double late = sustainMode ? (double) sustain.newestShortfall() : sampler.getShortfallSamples();
        uiLateMs.store (lookAhead > 0 && ! sampleMode ? (float) (late * 1000.0 / sr) : 0.0f);
    }
    uiCycle.store ((int) std::lround (cyc * (double) rate / sr));
    uiAutoD.store ((int) std::lround (sampler.getLastAutoD() * (double) rate / sr));
    uiSynced.store (synced);
    uiVoices.store (sustain.activeVoices());
    uiRecording.store (sustain.isRecording());
    float prev = uiPeak.load();
    while (peak > prev && ! uiPeak.compare_exchange_weak (prev, peak)) {}
    for (int c = 0; c < 2; ++c)
    {
        float pm = uiMasterPeak[c].load();
        while (mPeak[c] > pm && ! uiMasterPeak[c].compare_exchange_weak (pm, mPeak[c])) {}
    }
}

void CongoFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CongoFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    for (const char* id : { congo::id::test, congo::id::kill, congo::id::freeze })   // never reopen pressed
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (0.0f);
}

juce::AudioProcessorEditor* CongoFilterProcessor::createEditor()
{
    return new CongoFilterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CongoFilterProcessor();
}
