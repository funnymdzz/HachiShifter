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

## Melodyne 5: MULSS component renderer (deep 2026-08-06)

The previously recorded evidence (robust-pitch-curve, `MULSS_prepareAndRenderSpectralBlock`
/`reconstructSpectralComponents`) was confirmed against fresh decompilations saved in
`reverse/notes/decomp/`.  The complete algorithm is now understood as a **bandlimited
harmonic resynthesis applied as a per-bin real gain mask to the source complex spectrum**
plus a **Catmull-Rom time-domain resampling** stage for the melodic/hybrid stretch:

- `MULSS_processSpectralFrame` (`0x1819f9970`, formerly `FUN_1819f9970`):
  - windowed STFT; frame size is the next power of two `>= (analysisLen+hopLen)`,
    capped at 0x1000 (4096);
  - period analysis window `param_12` samples and hop `param_13` samples apply a
    stored 8192-point sine window split into rising (`DAT_18368af08`) and falling
    (`DAT_18368af08+0x8000`) halves, sampled with `8192.0/len` per tap;
  - multi-channel magnitudes are folded into one mono magnitude buffer at
    `param_1+0x1c0` via `sqrt(re^2+im^2)` (overflow-safe);
  - the output spectrum accumulator at `param_1+0x1c8` is initialised to 1.0;
  - when the element has a non-trivial time warp (`param_4`), the reconstructed
    output buffer at `0x1c8` is the **per-bin real gain mask** applied to the source
    complex spectrum (`frame_re *= mask`, `frame_im *= mask`).  When the element
    carries a join/transition (`0x1c8` slot via `0x78` branch), the mask is replaced
    by a **rotation**: real kept, imaginary negated scaled by the differential — this
    is the per-bin phase advance for the melodic-stretch path;
  - after IFFT, the time-domain frame is **resampled** for stretching:
    `*(0x261)` selects linear or **Catmull-Rom cubic Hermite** interpolation over
    the source-bin axis (`0x16666667` / `0.5` coefficients are Catmull-Rom).  When
    the stretch ratio is `<1`, the rising half of the window re-confines the
    compressed frame.

- `MULSS_reconstructSpectralComponents` (`0x181a04830`):
  - source pitch ratio `param_22`, inverse `fVar33`; harmonic count
    `iVar7 = trunc(numBins / param_22)` clamped to 0x3ff;
  - attack/body crossfade `fStack_1e4 = kernel((param_16-param_13)/(param_14-param_13))`;
  - for each harmonic `h` the source bandlimited extraction kernel
    `kernel(1.0 - |bin - h*param_22| / param_22)` is applied with a window of
    `2*iVar18+1` source bins; the harmonic energy `fVar37` and central-band energy
    `fVar39` are accumulated;
  - the kernel table `m5_kernelWindow01` (`0x18298fda0`) is a runtime-initialised
    8192-point function over input [0,1] returning 0..1 — used both as the bandlimited
    extraction kernel and as the attack/body crossfade.  Endpoint x=0 -> 0,
    x=1 -> 1.0; consistent with a Hann-style form `0.5-0.5*cos(pi*x)` sampled 8192
    deep.  Implementation should use a Hann or Hermite smoothstep for that table.
  - the formant correction multiplies each harmonic by
    `target_envelope(h) / source_envelope(h)` where both envelopes are extracted with
    the same bandlimited kernel at `formantRatio*h*param_22` and at the harmonic's
    own position, so the vocal-tract spectral envelope is preserved independent of
    the pitch shift;
  - amplitude and noise functions (`pow`-shaped) scale `fVar30`;
  - reconstructed harmonic energy is accumulated into the output buffer
    `param_7` (between adjacent target integer bins with linear fractional split);
  - **block power normalisation** at the end: `output[bin] =
    accumulated_output[bin] / accumulated_source[bin] * blockNorm` with `FLT_MIN`
    floor and a `/1.5` factor when no formant amplitude is requested.

```
% This block intentionally displayed for grep; ratio mapping direction:
% source_bin_of_harmonic(h) = h * pitchRatio   (param_22 = pitchRatio).
% pitchRatio is the SOURCE/TARGET frequency ratio (target_freq = source_freq / pitchRatio).
```

Consequences for HachiShifter (`mld5`):

- The old path set `mld5` to Signalsmith Stretch + a coarse
  `applyMld5SpectralFinish` low-pass finish.  That does **not** implement the MULSS
  model.  The new `Mld5Renderer` performs windowed STFT, bandlimited harmonic
  extraction, formant-ratio-corrected remap, and applies the resulting real gain mask
  to the source spectrum so the source phase/transient direction stays intact; the
  melodic stretch is the Catmull-Rom time-domain resampling of the IFFT'd frame.
- High-band protection / anti-aliased upward shift falls out of the harmonic-count
  truncation (`iVar7 = numBins / pitchRatio` clamped to 0x3ff) and the block-power
  normalisation noise floor `1e-7`-scale, instead of a one-pole low-pass post-filter.
- The direction-preserving real-gain masking avoids the synthetic phase accumulator
  that produced the phase-vocoder echo/doubling reported on the previous `mld5`.

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

### Render core (deep 2026-08-06)

- `MDPlayAlgorithm_setPitchRatio` (`0x00420440`) stores `float` at `this+0x5c`
  (`param_1[0x17]`), then invokes `(*vtable[0x1d8/4])(this)` as the render
  invalidation/redo hook; a neutral reset writes exactly `1.0`.
- `MDPlayAlgorithm_setFormantRatio` (`0x004204b0`) keeps its own `float` at
  `+0x60`, separate from pitch.
- The note list is iterated by `FUN_00421790` after a neutral ratio reset: for
  every note it calls `FUN_0043eec0()` then
  `FUN_00440050(pitchFactor * 1731.234)`.  The constant `1731.234 == 1200/ln(2)`
  converts log-cents to a log-Hz scale, so `FUN_00440050` feeds a cent→source-Hz
  note parametre to the per-note pitch drive; the analogous formula
  `logf(h*param_16*0.1223122)*1731.234/100 - 35` is the analytic-bin→cents mapping
  shared with the M5 `reconstructSpectralComponents` "3" branch.
- The element stores per-note period markers (`MDWritePeriodsToMDD`, `_periodMultipleField`,
  `BMDTakePeriodMultipleAction`).  This confirms a **period/f0-locked overlap**
  engine rather than a phase vocoder.
- The `MDInspectorPlayAlgo` vftable at `0x007d8664` shares the broad base of
  `MDPlayAlgorithmParams` vftable at `0x007befc4` (slots 0..79 overlap), so the
  render/notify path is in this inheritance family.  Slot 118 (`0x1d8`) of the
  play algorithm vtable is the per-note recompute notification invoked by every
  ratio setter.

Therefore HachiShifter must implement `mld3` as a **distinct period-transition
overlap** renderer whose render clock is 72 ms analysis / 7.5 ms periodic step at
nominal rate (the configured clock in `RenderService::renderFormantPreserved` for
`PitchRenderBackend::mld3`), and whose DSP is **PSOLA-style time-domain pitch
synchronous overlap-add** with multiplicative pitch and formant ratios, keeping the
existing note connection / wants-transition adaptation.  It must not share the M5
MULSS spectral gain-mask path.

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
