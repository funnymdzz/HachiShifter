# Melodyne pitch-engine reconstruction notes

This file records evidence used by the independent `mld5` and `mld3` paths.  The
large Ghidra database is deliberately local and persistent at
`reverse/ghidra/Melodyne.gpr`; it is ignored by Git, while these conclusions are
versioned with the implementation.

## Analysed samples

| Product | File | SHA-256 |
|---|---|---|
| Melodyne 5.4.0.036 | `MelodyneCore-5.4.0.036.dll` (89,387,008 bytes) | `b0e0a58b8351a89e4d18eb85f995fd0cb8f13ba74c64361dc4815203fc470159` |
| Melodyne 3.2 | `Melodyne.exe` (6,881,280 bytes, PE32/x86) | `0f459defcda85c3f88dc77c1da9b5f28eec17ee63ba2cfbf87ff00ecb938364f` |

## Melodyne 5: Robust Pitch Curve

Confirmed binary identifiers include:

- `handleSwitchRobustPitchCurve`
- `_robustPitchCurveSwitch`
- `robustPitchCurveSwitch`
- `MDDetectionAudioSourceInsp2.Window.SwitchButton.handleSwitchRobustPitchCurve.title`
- `_pitchCurveSlope`, `_pitchCurveDeflection`
- `_vibratoQuality`, `_portamentoQuality`, `_pitchQualityWeight`

The control belongs to the Detection Audio Source Inspector rather than the
ordinary pitch-correction tool.  Ghidra confirms the complete call chain:

- `0x180eb4160` — `MDDetectionAudioSourceInspector_handleSwitchRobustPitchCurve`;
- `0x181aafb50` — `MUAudioSource_setRobustPitchCurve`;
- the setter is accepted only for analyser kind `1`;
- it change-notifies a boolean stored at detection source offset `+0x1ac`.

These names, a plate comment and bookmark category `MLD5_ROBUST_PITCH` are
persisted in `/home/ubuntu/hjs/reverse/melodyne5/melodyne-re.gpr`.  This places
the switch before note rendering: it chooses a consolidated detector/source
curve.  Slope, deflection, vibrato quality and portamento quality remain
separate features, so a robust curve must reject isolated harmonic/octave
choices without flattening genuine transitions.

HachiShifter adapts the source-level setting to a note-level flag.  The `mld5`
render request carries a note-region mask and applies a zero-lag robust
estimator to source F0: local median/MAD context folds convincing harmonic
octave errors, while only isolated non-harmonic spikes are interpolated.  The
original target-minus-source edit is then reapplied exactly.  Note boundaries,
ordinary vibrato/portamento, unvoiced frames
and disabled notes are untouched.  MPD import checks `robustPitchCurveSwitch`
and aliases on element, principal item, detection audio source, source
description and analyser parameter set; HSPX stores it per note.

## Melodyne 3.2: pitch, formant and time model

The x86 binary retains unusually descriptive class/action names:

- `MDMelody`, `MDMelodyNote`, `MDMelodyNote::findNoteAverages`,
  `MDMelodyNote::findFinalPitchPosition`
- `_setPitchRatio`, `_setFormantRatio`, `_setOrigPitchCenter`
- `_setPitchTransition`, `_setWantsPitchTransition`,
  `_setNoteCurveAlignRate`
- `setShouldAdaptPitchTransition`, `setMaxSlopeForPitchTransition`
- `MDSetEditTimeStretchToolAction`, `MDSetEditTimeHandleToolAction`
- `MDSetEditPitchTransitionToolAction`,
  `MDSetEditFormantTransitionToolAction`, `MDSetEditAmpTransitionToolAction`

This is an older note-centre/ratio engine with explicit transition adaptation,
formant-ratio control and a coarser periodic time clock, rather than the later
component renderer represented by Melodyne 5's `MULSSGenerator` family.  The
initial `mld3` route therefore has its own enum, cache identity and renderer
clock (72 ms analysis / 7.5 ms periodic step at nominal rate), while retaining
HachiShifter's single-pass latency correction, source time map and unvoiced
protection.  Further named Ghidra findings should be appended here instead of
being left only in a transient terminal log.

### Persisted function evidence

The following names, comments and bookmarks are now saved in
`reverse/ghidra/Melodyne.gpr`:

- `0x00420440` — `MDPlayAlgorithm_setPitchRatio`: stores a float at object
  offset `+0x5c`; neutral reset writes exactly `1.0`;
- `0x004204b0` — `MDPlayAlgorithm_setFormantRatio`: separate float at `+0x60`;
- `0x00441400` — `MDMelodyNote_findNoteAverages`: quality/energy-weighted
  source-note statistics and original pitch centre;
- `0x00441e70` — `MDMelodyNote_findFinalPitchPosition`: iterative weighted
  centre refinement, up to 21 iterations;
- `0x0043cbc0`, `0x0043cc30`, `0x0043cca0`: separate pitch, amplitude and
  formant transition values;
- `0x0043cd10`, `0x0043cd80`: separate local and wide curve-alignment rates;
- `0x0043cdf0` — per-note `wantsPitchTransition`;
- `0x00442490` — transition decision checks approximately 20 ms around the
  boundary and rejects a transition when that region contains an unvoiced
  frame;
- `0x0049f8d0` — analyser settings store `shouldAdaptPitchTransition` at
  `+0x36`, maximum transition slope at `+0x50`, and forward/backward handle
  ratios/limits at `+0x74..+0x80`.

Accordingly, the independent `mld3` route now feeds multiplicative pitch and
formant ratios to the renderer rather than reusing the `mld5` component-cent
control.  Pitch-induced envelope motion is compensated first, then the saved
formant ratio is applied independently; this is what keeps a neutral formant
ratio from acquiring a child/elder timbre after transposition.  It keeps the
M3-specific periodic clock and the existing explicit
note connection mask: transitions are synthesized only for connected notes,
while unvoiced intervals remain disconnected.

### Melodyne 3 editor representation evidence (2026-08-03)

The melodic editor does not route every gesture through one note rectangle.
RTTI and vtables identify separate representations for note body, note
separator, time handle, pitch transition, formant, amplitude transition,
assignment, back handle and quantization.  The common visual/event slots are
shared, while construction and retained state remain type-specific.

- `0x004a8210` is persisted as `MDEditorNoteRep_initializeState`;
- it initializes six independent retained child-representation slots at
  `+0x98..+0xa8` and `+0xd4`, plus auxiliary state at `+0xcc`;
- `0x004a82b0` is persisted as
  `MDEditorNoteRep_releaseChildRepresentations` and releases those children
  independently before base cleanup;
- bookmark category `MLD3_EDITOR` marks this object layout.

The HachiShifter melodic editor follows this separation: body dragging,
left/right time handles, consonant boundary alignment, pitch drawing,
connection/split gestures and selection marquee have independent hit regions
and state.  This also prevents a time-handle drag from being interpreted as a
pitch edit when compact or overlapping blobs are displayed.

## Direct LLSM2 integration

The selectable `llsm2` route integrates the libllsm2 speech-model library,
not the moresampler command-line frontend.  The renderer analyses source PCM
into harmonic/noise layer 0, derives the layer-1 glottal/vocal-tract
representation, interpolates source frames through the clip source-time map,
sets target F0 on the native 5 ms curve, applies formant/tension edits to the
vocal-tract envelope, propagates phase and synthesizes once.  Unvoiced frames
remain unvoiced and the LLSM harmonic/noise decomposition is retained across
time stretching and pitch shifting.
