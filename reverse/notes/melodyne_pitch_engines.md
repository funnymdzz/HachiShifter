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
ordinary pitch-correction tool.  This places the switch before note rendering:
it chooses a consolidated detector/source curve.  Slope, deflection, vibrato
quality and portamento quality remain separate features, so a robust curve must
reject isolated harmonic/octave choices without flattening genuine transitions.

HachiShifter adapts the source-level setting to a note-level flag.  The `mld5`
render request carries a frame mask and applies a zero-lag robust estimator to
the source-to-target correction: local median/MAD clipping followed by symmetric
slope bounds.  Unvoiced frames and disabled notes are untouched.  MPD import
checks the setting on element, principal item, source description and analyser
parameter set; HSPX stores it per note.

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
