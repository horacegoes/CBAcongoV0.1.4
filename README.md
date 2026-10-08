# Congo Filter V.0.1.4

Akai sampler timestretch + aggressive analog filter + the Effects Board (a recreation of the
Akai EB16), as a VST3 (and standalone app).
Signal flow: SAMPLER (top) -> FILTER (bottom) -> EFFECTS (middle strip + panel on the right).
The window can be dragged larger from the bottom-right corner (up to 2x, never smaller).
V.0.1.4 has its own plugin ID and file name ("Congo Filter V.0.1.4"), so it installs alongside
older versions without replacing them.

* **Sampler**: Akai **S950** (default) and **S2000** timestretch, recreated from the
  machines' own ROM code: S950 CYCLIC (MONO / POLY fades, Auto D) and S2000 CYCLIC / INTELL
  (Quality, XFD). Bit-exact with the ROM arithmetic at pitch 0.
* **Pitch** independent of time, like playing the stretched sample on a key.
* **Converter**: sample rate 7.5–48 kHz with S950-style tracking filters, 1–16 bits.
* **Filter**: Moog transistor ladder (self-oscillates from 97 % resonance, Bass Keep, Analog
  drift, LP24/18/12, BP, HP, Notch) or Akai S2000-style digital filter — one button.
* **MODE: LIVE / SUSTAIN / SAMPLE** (the button steps through them): LIVE follows the input,
  locked to the beat. SUSTAIN samples each phrase and plays **all** of it stretched, even after the
  input stops - up to 8 phrases overlay, each keeping the settings it started with. SAMPLE records
  each RETRIG step and plays it from its start on the next step, stretched and pitched, like a
  sampler playing a sample - dead on the beat, one step later.
* **TIMING: TIGHT / LIVE** (on the SAMPLER title line, TIGHT by default): see Timing below.
* The two models are called **950** and **2000** (the Akai S950 and S2000), and the 2000's stretch
  types **CYCLES** and **CLEVER** (the S2000's CYCLIC and INTELL).
* **SYNC** locks the stretch to musical lengths (1/4 .. 4/1 incl. triplets, dotted, 5:4, 5:3,
  then 250 % steps to 2000 %); **FREEZE** loops the last SIZE (ms or tempo) of the input, the
  sampler output or the final output (after the effects).
* **Effects Board** (Akai EB16): FX1 and FX2 multi-effects (ring mod / distortion, 4-band EQ
  with F-MOD, phaser / flanger / chorus, rotary speakers, FM + auto pan, pitch shift, echo,
  mixer) plus two reverbs, RV1 and RV2, in series in any ORDER.
* **TEST** plays the genuine Akai S950 factory test tone (C3) through the whole chain;
  **KILL AUDIO** (red) silences everything the plugin is playing and starts fresh.
* **NUDGE** moves the processed sound earlier or later (up to 50 ms) to put it back in time.
* **RESET** (next to KILL AUDIO) goes back to the last save point - the preset you last loaded or
  saved, as it was saved (INIT when the plugin has just been opened).
* **LOCK** buttons keep MIX, and the MASTER fader + PAN, where they are when you change preset.

## Build (Windows, Visual Studio 2022)

Needs CMake 3.22+ and Visual Studio 2022 with "Desktop development with C++".
JUCE is downloaded automatically on the first configure.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Results:

* VST3: `build/CongoFilter_artefacts/Release/VST3/Congo Filter V.0.1.4.vst3`
* Standalone: `build/CongoFilter_artefacts/Release/Standalone/Congo Filter V.0.1.4.exe`

Copy the whole `Congo Filter V.0.1.4.vst3` folder to `C:\Program Files\Common Files\VST3\`, then in
Ableton: Options → Preferences → Plug-Ins → turn on "Use VST3 Plug-In System Folders" →
Rescan. It appears under Plug-Ins → CBA → Congo Filter V.0.1.4.

**GitHub**: push this folder to a repository and the included workflow
(`.github/workflows/build.yml`) builds the VST3 on Windows, runs the DSP tests, and
attaches the VST3 and standalone app as downloadable artifacts on the Actions page.

To use your pinned JUCE commit instead of 8.0.4:
`cmake -S . -B build -DCONGO_JUCE_GIT_TAG=<full 40-character hash>` (JUCE 8 required —
the UI uses JUCE 8 font APIs).

## Controls

| Section | Control | Range | Notes |
|---|---|---|---|
| Top | PRESET | <, list, >, SAVE, DELETE | Factory presets and your own |
| | HOST, TEMPO | button, display | Host tempo, or your own (default 133.33 BPM) |
| | TEST | button | S950 test tone into the chain; restarts the stretch so it's heard at once |
| | RESET | button | Back to the last preset loaded / saved, as saved (INIT on launch) |
| | KILL AUDIO | button (red) | Silences all stretches, filter and TEST in 10 ms; knobs are kept. MIDI-mappable |
| | THEME | button | Theme editor: built-ins and your own three-colour themes |
| Sampler | S950 / S2000 | button | Pressing loads that sampler's settings (below) |
| | LIVE / SUSTAIN | button | LIVE follows the input; SUSTAIN plays whole phrases out, 8 overlaid voices |
| | TRIGGER | Level / Retrig / MIDI | SUSTAIN: what starts a phrase (default Level) |
| | THRESH | -60 – 0 dB | Level trigger threshold; a phrase ends after 250 ms below half of it |
| | TYPE | Cyclic / Intell | S2000 only |
| | FADE | Mono / Poly | S950 only (ROM default Poly) |
| | RETRIG | Free, 1/32 … 2 Bars | Restarts the stretch in time with the host |
| | AUTO D | on/off | S950: finds the best cycle at each retrigger |
| | T SYNC, C SYNC | buttons | TIME as musical ratios / CYCLE as note values, independently |
| | TIME | 25 – 2000 % (T SYNC: ratios) | S2000 range; < 100 % squeezes |
| | PITCH / FINE | ±24 st / ±50 ct | ±24 matches the Akai tune tables |
| | CYCLE | 5 – 9999 (C SYNC: note values) | S950 D-time; S2000 limit lifted |
| | QUALITY / XFD | 0 – 20 | S2000 |
| | BITS | 1 – 16 | S950 engine itself is always 12-bit |
| | RATE | 7.5 – 48 kHz | S950: bandwidth = 0.4 × rate |
| Filter | MOOG / AKAI | button | Click-free switch |
| | MODE | LP24 LP18 LP12 BP HP Notch | Akai engine is 2-pole |
| | CUTOFF | 20 Hz – 20 kHz | |
| | RESO | 0 – 100 % | Moog self-oscillates from 97 % - on its own, even with no input |
| | DRIVE | 0 – 36 dB | |
| | BASS KEEP | 0 – 100 % | Moog LP: 100 % = no bass loss with resonance |
| | ANALOG | 0 – 100 % | Moog: stage mismatch (to ±5 %) + drift (to ±2.5 %), left and right apart |
| Effects | ORDER | 24 orders | Series order of FX1, FX2, RV1, RV2 |
| | FX1 FX2 RV1 RV2 | name, ON, WET | Name opens the unit's page; WET = its wet/dry |
| Freeze / Out | FREEZE, HOLD, MS/SYNC, SOURCE, SIZE | | See FREEZE below |
| | OUTPUT | −inf – +12 dB | Level of the frozen sound only |
| | MIX, LOCK | 0 – 100 % | Dry/wet of the whole plugin; LOCK keeps it through preset changes |

Sampler defaults loaded by the button:
**S950** 12-bit, 25 kHz (ROM default bandwidth 10 kHz), D-time 1000, Poly, Auto D off.
**S2000** 16-bit, 44.1 kHz, Cyclic, cycle 1000, Quality 10, XFD 0.

Double-click any knob to reset it. Hover for tooltips. Every number can be dragged: hold the
left button on a knob's value (or the NUDGE readout, or TEMPO) and push up / pull down to change
it - shift for fine steps. A click without dragging lets you type a value.

### Timing

Every stretch starts like the ROM does: its first cycle is played straight, so the attack on the
beat lands on the beat (the LIVE engine is bit-exact with the ROM model, start included). Retriggers
fire 1.5 ms early so their short crossfade is finished when the beat arrives.

* **TIGHT** (default): the plugin asks the host for 100 ms of look-ahead, which Ableton compensates.
  Raising the pitch (or a long cycle) needs to read the incoming audio a little in advance; with
  the look-ahead every stretch lands within 2 ms of its beat on both models, pitch -12..+12, with no
  slewing when the pitch changes. The only cost: anything monitored live through the plugin is
  100 ms late.
* **LIVE timing**: no added latency. Unpitched stretches are on the beat; raised pitch arrives late
  (by tens of ms, more with longer cycles), as it has to wait for the audio.
* Squeezes (TIME under 100 %) can't land on the beat in real time - they would need the rest of the
  step in advance; TIGHT says on the status line how late it is. **SAMPLE mode** plays squeezes (and
  everything else) exactly on the beat, because each step is fully recorded before it plays.
* At exactly TIME 100 % both models now pass the sound straight through (the 2000 used to sit one
  cycle - 22.7 ms - behind).

### Stretch SYNC

Two buttons, each on its own; the knob switches in the same place on the panel:

* **T SYNC** - **TIME** steps through ratios of the final length:
1/4 (25 %), 1/3, 1/2, 2/3 (quarter-note triplets), 3/4 (dotted), 1/1, 5/4 (5:4), 4/3 (half-note
triplet), 3/2 (dotted, 3:2), 5/3, 2/1, 5/2, 8/3, 3/1, 4/1, then 5x, 7.5x, 10x, 12.5x, 15x, 17.5x,
20x (2000 %).
* **C SYNC** - **CYCLE** steps through note values at the tempo: 1/256, 1/128T, 1/128, 1/64T, 1/64, 1/32T,
  1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2.
* With T SYNC on you can also type into TIME - `140`, `140%` or `4/3` - and it lands on the
  nearest ratio in the list. What locks to the note is the rhythm you hear -
  how often the sampler jumps back (the period) - and the cycle is worked out to match for the
  current TIME and PITCH. E.g. 1/32 at 120 BPM = sixteen jumps per beat at any TIME.
* With C SYNC on the hardware limits are lifted, so long note values work on both models (the
  S2000's 20 - 2000 cycle range and the S950's 16-bit period). INTELL and Auto D choose their own
  cycle, so C SYNC is greyed out for them.

Changes land at the next stretch cycle, so they switch cleanly; both are MIDI-mappable.

### Limits lifted beyond the hardware

* S2000 model: CYCLE uses the knob's full 5 - 9999 range (the real S2000 stops at 20 - 2000).
* C SYNC on (both models): cycles and periods go as long as the note values need.
* C SYNC off, the S950 stays ROM-exact (including its period-overflow rule).

### MASTER

The channel on the far right: a fader (-inf to +6 dB, double-click = 0 dB; grab the cap - clicking
the track doesn't make it jump) with the left / right output meters running behind it on the
fader's own scale (peak hold), red clip lights above it (click them to clear - they can't touch
the fader), **PAN** above it (a balance: centre leaves both sides at full level) and **LOCK** below it
(keeps fader and PAN through preset changes). NUDGE sits underneath.

### PRESETS

The **PRESET** bar at the top: **<** / **>** step through the presets, the list picks one
(factory presets first, then yours). **SAVE** turns the list into a name box - type a name and
press Enter (Esc cancels); saving with the name of one of yours replaces it, and saving with a
factory name makes a copy ("DUB ECHO 2"), so factory presets never change. **DELETE** removes one
of yours - click it, it says SURE?, click again within 3 seconds.

* Your presets are files in `<user app data>/CBA/Congo Filter/Presets/` (`.cfpreset`), so they are
  there in every project and every later version. Each session remembers which preset it was on.
* A preset stores every setting except the momentary buttons (TEST, KILL AUDIO, FREEZE) and the
  LOCK buttons. Settings added in later versions go to their defaults when an older preset loads.
* **LOCK** beside MIX keeps your MIX when you change preset; **LOCK** on the MASTER channel keeps
  the fader and PAN. RESET restores them anyway.
* Factory: INIT, S950 JUNGLE 3X, HIGHER STATES, S2000 INTELL PAD, MOOG SINGS, C-SYNC STUTTER,
  LOFI 8K 8BIT, DUB ECHO, ROTARY ORGAN, REVERSE VERB.

### NUDGE

Under the MASTER fader, top to bottom: **< EARLY  LATE >** (0.25 ms per click), the readout (two
decimals - drag it up / down, 0.5 ms per pixel, shift 0.05; or click to type, e.g. `-3.4`),
**coarse < EARLY  LATE >** (1 ms) and **fine < EARLY  LATE >** (0.05 ms), then a small **RESET**.
The range is +-250 ms.
Only the processed sound moves - the dry signal stays where it is. Moving later is a simple delay;
moving earlier works by reporting that much extra latency so the host plays everything else later
to match - so it applies to playback in the DAW (it can't make live monitoring earlier), and some
hosts glitch briefly while that amount changes. NUDGE is a parameter, so it can be automated or
MIDI-mapped.

### TEMPO

The red **TEMPO** display (next to TEST) shows the tempo everything syncs to (RETRIG, SYNC,
FREEZE sync sizes):

* **HOST on** (default): the host's tempo and transport. Shows `---.--` if the host sends none.
* **HOST off**: your own tempo - default **133.33 BPM** - with the plugin's own clock, so RETRIG and
  SYNC keep running with the transport stopped or in the standalone app. Drag up / down (shift =
  fine), scroll, or double-click to type a value; alt + double-click resets to 133.33.

### Themes

**THEME** opens the theme editor: pick a theme from the list (GREEN, ORANGE, WHITE and your own),
or make one from three colours - **BACKGROUND**, **MAIN** (text, knobs, lit buttons) and
**SECONDARY** (outlines, labels, unlit ticks); the phosphor glow is worked out from MAIN. Click a
colour box, then choose a colour in the picker or type its hex value (e.g. `#3DFF6E`); everything
updates live. **SAVE** adds it to your list under the NAME you give it, **DELETE** removes one of
yours. **SET DEFAULT** makes the current colours the theme every new copy of the plugin opens in
(saved sessions keep their own). **Disco** changes all three colours at random on every beat, in
time with the tempo - background kept dark and the main colour bright so it stays readable; switch
it off and the last colours stay. Your themes are saved to `<user app data>/CBA/Congo Filter/themes.xml`, so they're there in
every project; each session also remembers its own colours. KILL stays red in every theme.

### FREEZE

FREEZE loops the most recent SIZE of sound with a crossfaded seam. SOURCE picks what is frozen:
**Input** (the frozen loop is then stretched / filtered / effected), **Sampler** (the stretched
output, before the filter) or **Output** (the final sound, after the effects). SIZE is in ms
(1 ms - 2 s) or, with the MS/SYNC button, tempo divisions (1/64 - 1 bar, triplets included).
Turning SIZE while frozen changes the loop length and so the repeat rate - tiny sizes buzz into
a pitch.

* **OUTPUT** (in the FREEZE section) is the frozen sound's own level, -inf to +12 dB. It touches
  nothing else - the plugin's overall level is the MASTER fader.
* **SIZE** starts in SYNC (tempo divisions); MS/SYNC switches. **LOCK** beside SIZE keeps the same
  size when you switch: SYNC -> MS exactly, MS -> SYNC to the nearest note value. Without it each
  mode keeps its own setting.
* **HOLD** keeps the frozen loop going while the normal, unfrozen sound comes back underneath it;
  OUTPUT then fades the held loop in and out over the live sound. The moment HOLD goes on, one loop
  is captured exactly as you hear it and plays as its own layer at the very end of the plugin -
  after the MASTER fader - so nothing else can change it: not presets, RESET, SIZE, SOURCE, tempo,
  the sampler, filter, effects, MIX, MASTER or PAN. Only OUTPUT (fade), HOLD / FREEZE off (release)
  and KILL AUDIO (stop) touch it. HOLD is never stored in presets.

### Effects Board

The **EFFECTS** strip (middle) holds the **ORDER** menu and the four units **FX1, FX2, RV1, RV2**,
each with **ON** and its own **WET** (wet/dry) knob. The signal runs through the units that are on,
one after the other, in the ORDER chosen - default `FX1 - FX2 - RV1 - RV2`; all 24 orders are in
the menu, e.g. `FX2 - RV1 - FX1 - RV2`. Clicking a unit's name opens its controls on the panel on
the right: FX1 / FX2 have pages DIST/EQ, MOD, ECHO, MIX; RV1 / RV2 have one page each.
Every block has its own ON button (DIST, EQ, MOD, ECHO). On the hardware each multi-effect also
has its own reverb (placed by a PATH menu); here the reverbs are RV1 and RV2, placed with ORDER.
(On the hardware the reverbs are called RV3 / RV4 and are fed by sends; with one signal in a
plugin, the sends became the series ORDER.)

Every control, its units and value format come from the S2000 ROM's EB16 pages:

* DIST/EQ: ring mod (to 5 kHz) + distortion (depth, atten); 4-band EQ = four parallel bands -
  LOW 16-500 Hz, BP1 / BP2 40 Hz-6.3 kHz with width, HIGH 500 Hz-16 kHz, each -37 dB (off) to
  +12 dB, ISO 1/3-octave steps; F-MOD sweeps BP1 / BP2 (0.0-9.9 Hz).
* MOD: PHA/FLA/CHOR (rate, depth, feedback +-99), ROTARY SPKRS (speed 1 / 2, accel in seconds,
  depth, SPEED 2 switch - MIDI-mappable), FMOD/AUTOPAN (FM rate/depth/feedback + PAN, L>R, R>L,
  TREM), PITCH SHIFT and PITCH + FBK (tune, delay, feedback per side; **TUNE LINK** makes TUNE L
  and TUNE R move together).
* ECHO: MONO LEFT, MONO L+R, XOVER L&R (ping-pong), STEREO; delay to 999 ms, damping frequency,
  feedback, L/R offset. The page reads in rows: MIX / DELAY / DAMP / FBK / L/R OFFS (MIX is the echo
  level and starts down; DELAY..OFFS are the mono modes), then L DELAY / L DAMP / L FBK and
  R DELAY / R DAMP / R FBK (XOVER and STEREO).
* RV1 / RV2: LARGE / SMALL HALL, LARGE / SMALL ROOM, GATED 1 / 2, REVERSE; predelay,
  decay, LF / HF damping frequencies, diffuse, near (early reflections), gate / reverse time,
  level and pan. Tails have the board's measured stereo spread (left/right correlation ~+0.3).
* MIX: DIST/EQ level + pan / MOD level + pan + width / ECHO pan + width (ECHO's level is the MIX
  knob on the ECHO page).
* **SYNC** (one per page): the page's time controls switch to note values in the same places, at
  the tempo, and sweeps lock to the bar - DIST/EQ: the F-MOD sweeps (BP1 / BP2 RATE); MOD: PFC, PAN
  and FM rates and the pitch DEL L / R (ROTARY speeds have their own ROT SYNC); ECHO: DELAY, L / R
  DELAY (up to 2 s - past the EB16's 999 ms); RV1 / RV2: PREDELAY and the GATED / REVERSE TIME.
  FBK is an amount, not a time, so it doesn't sync.

Like the board, delay and reverb memory is 24-bit and saturates at full scale. What is exact
and what is reconstructed: the controls, ranges, units and routing are the EB16's; the DSP
chip's own microcode isn't readable in the S2000 ROM, so the algorithms are EB16-era designs
built to that parameter set, and can be tightened against recordings of a real board.

### SUSTAIN mode

* **Level** (default): a phrase starts when the input goes above THRESH and ends after 250 ms
  of quiet. **Retrig**: each RETRIG step while the transport plays. **MIDI**: a note starts the
  phrase and note-off ends it; the note transposes it (C3 = 60 = no transpose). In Ableton, set a
  MIDI track's "MIDI To" to the track holding Congo Filter.
* Each phrase plays to its end however long that takes (a 1 s phrase at 2000 % lasts about 20 s).
  Stretches start while the phrase is still coming in; squeezes (TIME < 100 %) start when it ends.
* Up to 8 voices; a 9th fades out the oldest. Phrase memory: 10 s per voice (at up to 48 kHz).
* Stretching and the start / end of each phrase follow the ROM routines; an S950 voice at pitch 0
  is bit-exact with the S950's own whole-sample stretch.

## Tests

`congo_eb16_tests` checks the Effects Board and FREEZE: transparency with the units off, EQ
flatness, echo timing and runaway feedback, reverb decay times for every type, gated and reverse
behaviour, pitch shift, stability of every modulation type at full depth, the series ORDER,
WET/DRY, click-free ON, FREEZE loop accuracy and CPU (all four units at full tilt: about 5 % of
one core).
`congo_sustain_tests` checks SUSTAIN: S950 phrases bit-exact with the ROM (incl. start/end
handling) at 50–2000 %, full-length playback after the input stops on every model, pitch,
overlay independence, the voice limit, KILL, squeeze timing and a 2-minute random stress run.
`congo_dsp_tests` (built automatically) checks: S950 bit-exactness against the ROM model,
pitch accuracy on every model, free-running stability, extreme rates (7.5 kHz sampler on a
192 kHz host), Moog self-oscillation onset and tuning, Bass Keep, converter rate/bit depth,
the test tone, and a 2-minute randomised stress run. `check_s2000.py` optionally verifies
the S2000 engine bit-for-bit against an independent Python model.

## Notes

* Latency 31 samples (2× oversampled filter), reported to the host; dry path is aligned.
* LIVE mode: with RETRIG on Free (or the transport stopped) a stretch runs continuously and
  jumps back to "now" every couple of seconds; use a RETRIG division, or SUSTAIN for phrases.
* SUSTAIN voices add up: several loud phrases overlaid can exceed 0 dBFS — use OUTPUT.
* The plugin reports a 300 s tail so hosts keep it running while long phrases play out.
* The S2000's filter runs in its voice chip, so the AKAI filter is a voicing rather than
  ROM-derived; the S950 has no resonant filter of its own.
