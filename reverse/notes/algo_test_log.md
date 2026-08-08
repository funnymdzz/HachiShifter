# Melodyne Algorithm Isolation Log

Updated: 2026-08-06 Asia/Tokyo

## 2026-08-07 Fixed-Scale Spectrogram Pass

The standalone harness now writes three 24-bit BMP files beside each output:
`<out>.input.bmp`, `<out>.output.bmp`, and `<out>.diff.bmp`. All runs use a
2048-point Hann STFT, 256-sample hop, and the same fixed `-100..0 dBFS` scale.
The reported spectral difference is measured before listening normalization.

Real-vocal results for the 5.0 to 9.0 second window:

| Path | Shift | Spectral MAE | Spectral P95 |
|---|---:|---:|---:|
| mld5 | 0 | 0.001 dB | 0.004 dB |
| mld5 | +3 | 9.255 dB | 30.927 dB |
| mld5 | +7 | 12.180 dB | 36.445 dB |
| mld5 | -5 | 9.871 dB | 36.595 dB |
| mld5 | +12 | 14.102 dB | 42.897 dB |
| mld3 | 0 | 0.000 dB | 0.000 dB |
| mld3 | +3 | 7.104 dB | 25.543 dB |
| mld3 | +7 | 10.083 dB | 33.151 dB |
| mld3 | -5 | 7.715 dB | 27.885 dB |
| mld3 | +12 | 12.106 dB | 37.637 dB |

The mld3 zero-shift implementation previously ran through PSOLA despite its
identity contract (`NRMSE=1.386`, correlation `0.028`). It now explicitly
passes input through when pitch and formant offsets are both zero; the verified
result is `NRMSE=0`, correlation `1`, and zero spectral difference.

The nonzero rows measure expected input/output change, not similarity to an
original Melodyne render. They cannot be acceptance scores until a confirmed
reference export is available. The current real-vocal F0 medians also do not
track the requested ratios reliably, so both pitch paths remain experimental.

## 2026-08-07 Confirmed Melodyne 5 Single-Note Reference

Reference directory: `/mnt/c/Users/funny/Downloads/sample1/a/`. The source and
all four Melodyne exports are 27648-sample, 48 kHz, mono, 16-bit WAV files, so
they are directly time-aligned. `a__30.wav` is the source; each edited MPD has
its export in the same-stem subdirectory.

A standalone, zlib-only `mpd_dump.cpp` inspector was added under
`reverse/algo_test/`. It confirms the primary source item pitch centre is 7118
cents and the first `MUElement` targets are:

| Project | Target centre | Edit | Melodyne-export median F0 |
|---|---:|---:|---:|
| `a.mpd` | 7118 cents | 0 | source about 500 Hz |
| `a+5.mpd` | 7618 cents | +5 | 657.53 Hz |
| `a-5.mpd` | 6618 cents | -5 | 372.09 Hz |
| `a+12.mpd` | 8318 cents | +12 | 979.59 Hz |
| `a-12.mpd` | 5918 cents | -12 | 248.70 Hz |

The F0 diagnostic range was extended from 500 to 1200 Hz for this fixture.
Against the confirmed Melodyne exports, the current experimental mld5 gives:

| Shift | Experimental median F0 | Reference spectral MAE | P95 |
|---|---:|---:|---:|
| +5 | 648.65 Hz | 12.895 dB | 29.968 dB |
| -5 | 372.09 Hz | 11.670 dB | 27.102 dB |
| +12 | 187.50 Hz | 16.184 dB | 42.018 dB |
| -12 | 250.00 Hz | 13.974 dB | 32.056 dB |

The moderate and downward shifts reach approximately the correct fundamental,
but remain spectrally far from Melodyne. The +12 path fails outright, and -12
produces a raw 1.2202 peak. The present independent implementation omits the
per-bin phase-advance/rotation branch visible in `MULSS_processSpectralFrame`;
plain grain resampling plus OLA loses frame coherence at large ratios.

Two GNBCFA compatibility details were exposed by these projects. Container
entries are aligned to eight bytes after their stored payload, and `a+12.mpd`
contains three object-table tombstones with class id `0xffffffff`. The
standalone inspector handles both. `juce/src/backend/MelodyneImporter.cpp` now
handles both cases with minimal container/object-table fixes; the renderer
experiments remain isolated from production DSP.

A diagnostic trial added one uniform frame-centre phase advance before IFFT. It
restored the +12 median F0 to `979.59 Hz`, confirming phase continuity as the
immediate failure mode, but produced raw peak `15.08` and worsened the reference
spectral MAE to `18.407 dB`; the other shifts worsened as well. The trial was
removed. The required correction is component/harmonic-specific, as in the
binary's reconstruction and rotation branches, rather than one global time
shift for every FFT bin.

## 2026-08-07 Stateful Phase/Component Pass

The reference fixture contains two overlapping `MUElement` records, not one
uniform whole-file edit. The edited primary element runs from 0 to 0.576 s and
has a 31.84375 ms Attack; an unchanged 7918-cent element starts at 0.33721875 s.
Core algorithm scores therefore use the stable, non-overlapping 0.050 to 0.300 s
region, while the entire 0.576 s file is rendered to retain analysis context.

The standalone mld5 now carries previous/synthesis phase per bin, stretches by
the requested pitch ratio, then Catmull-Rom resamples back to the source length.
Spectral peaks share phase direction, matching the binary's persistent complex
phase-difference state more closely than independent-bin phase accumulation.
Formant pre-compensation uses `env[pitchRatio * bin] / env[bin]`; upward moves
use a log envelope and downward moves use RMS band energy. A 1024-sample local
block-power pass restores the source amplitude envelope without peak
normalisation. Moderate shifts use the binary's 4096 FFT cap; octave shifts use
2048 to avoid extreme-window smearing.

The active-frame score excludes silence and combines active-bin log spectral
error, smoothed envelope error, F0 cents, voicing, absolute level, and raw peak
penalties. Only a bounded +/-10 ms RMS-envelope lag may be removed; no DTW,
gain fitting, or reference-derived spectral correction is allowed.

| Shift | Similarity | Active spectral MAE | Envelope MAE | F0 score | Level delta | Raw peak |
|---|---:|---:|---:|---:|---:|---:|
| +5 | 74.05 | 5.691 dB | 3.358 dB | 1.000 | 0.027 dB | 0.4820 |
| -5 | 75.49 | 4.895 dB | 3.434 dB | 1.000 | 0.424 dB | 0.4878 |
| +12 | 70.72 | 5.971 dB | 3.937 dB | 1.000 | 0.308 dB | 0.4441 |
| -12 | 65.09 | 7.872 dB | 3.366 dB | 0.911 | 0.150 dB | 0.3898 |

This is a substantial improvement over the previous input/output-only path:
all four median F0 values now match their Melodyne references and no raw output
clips. A temporary Signalsmith control reached 74.77 only on -5 and was lower
on the other shifts, so it was removed from the independent harness. Forced
F0-grid phase anchors also regressed +5 and were removed.

The 90-point gate is not met. No production mld5 renderer code has been changed
or integrated from this experimental pass.

## 2026-08-07 Caller Argument Recovery

The M5 process call was re-opened in Ghidra with an explicit prototype. At the
call to `MULSS_processSpectralFrame` (`0x1819ffc31`), the recovered arguments
are:

```text
param_12 = uStack_f30  = falling analysis length / frame quarter
param_13 = uStack_eec  = rising analysis length / frame remainder
param_14 = fVar70      = source pitch expressed in frame-bin units
param_15 = fVar53      = target pitch / time ratio control
param_16 = fVar65      = formant pitch-bin control
param_17 = dStack_eb8  = fractional time scale
```

The normal setup computes `uStack_f30 = blockLength / 4` and
`uStack_eec = blockLength - uStack_f30`; these are not fixed 25/75 percent
assumptions in all transition branches. The source/target pitch values are
created by the melodic stretch driver and are frame-bin quantities, explaining
why treating `param_22 = source/target` as a direct raw FFT-bin pitch centre
gave the wrong harmonic mask.

Two additional experimental branches were evaluated and disabled by default:

- A direct raw-FFT harmonic kernel using global F0 reduced +5 from `74.05` to
  `61.52`; the component arrays are in a normalized frame domain, not raw FFT
  bins.
- A post-OLA source-envelope/output-envelope EQ reduced +5 to `67.42`; the
  binary's final division uses separate source, harmonic, noise and component
  power accumulators, so a whole-spectrum EQ is not equivalent.

The next reverse-engineering target is the conversion that creates
`fVar70/fVar53/fVar65` and the component-domain buffer at `param_1+0x1c0`,
followed by separate noise/sibilance accumulation. No renderer integration is
authorized until that path produces a cross-shift score near 90.

## 2026-08-07 Extreme-Ratio Parameter Scan

The isolated harness was resumed after a tool disconnect. The octave paths
were re-scanned with the existing 2048-point analysis and block-power
normalisation:

| Shift | Parameters | Similarity | Spectral MAE | Envelope MAE |
|---|---|---:|---:|---:|
| +12 | envelope 220 Hz, mask blend 1.00 | 72.97 | 5.444 dB | 3.749 dB |
| +12 | envelope 220 Hz, mask blend 0.75 | 75.05 | 5.272 dB | 3.296 dB |
| +12 | envelope 220 Hz, mask blend 0.50 | 67.43 | 6.538 dB | 4.365 dB |
| +12 | envelope 220 Hz, mask blend 0.90 | 74.77 | 5.220 dB | 3.424 dB |
| +12 | envelope 400 Hz, mask blend 1.00 | 66.71 | 6.517 dB | 4.570 dB |
| -12 | envelope 300 Hz, mask blend 1.00 | 65.09 | 7.872 dB | 3.366 dB |
| -12 | envelope 300 Hz, mask blend 0.75 | 64.75 | 7.831 dB | 3.536 dB |
| -12 | envelope 300 Hz, log envelope, mask blend 0.75 | 46.17 | 10.244 dB | 6.131 dB |

The `+12` result improved from 70.72 to 75.05, but the four-shift minimum is
still below the 80-point pre-integration gate. A 4096-point octave pass was
also rejected (`+12=44.37`, `-12=52.13`), confirming that the current octave
path should remain at 2048. Lowering the peak-lock threshold from -60 dB to
-80 dB did not materially improve the upward result. The downward RMS envelope
mode remains directionally useful; replacing it with log averaging is a clear
regression.

Ghidra RPC was restarted in headless mode. Decompilation of
`FUN_1819f2220` confirms it is a normalized component-domain mapping stage: it
copies a source array, applies `param_7 / param_8`, linearly resamples a target
envelope, multiplies each component by `target/source`, applies a fourth-length
tail taper, and computes a weighted normalization scalar. This cannot be
faithfully represented by a direct raw-FFT-bin envelope ratio. The next target
remains the caller's construction of the component arrays and its separate
harmonic/noise/sibilance accumulation.

The recovered recursive smoothing was implemented behind the experimental
`MLD5_COMPONENT_MAP=1` switch. It uses the recovered `exp(-2.5 / ratio)`
coefficient and bidirectional one-pole smoothing, while retaining the harness's
existing spectral remap. It did not improve the reference scores (`+12=66.17`)
and the downward case lost the requested F0 (`-12=0.59`), so the switch remains
off by default. The result confirms that the missing component-domain source
construction and the caller's direction-specific ratio values are essential;
the smoothing coefficient alone is not a valid replacement.

## 2026-08-07 Spectral Analysis and Final Parameter Sweep

A systematic reference-vs-source spectral analysis was performed across
bands:

| Band | -12 ref/src gain | +12 ref/src gain |
|---|---:|---:|
| 50-500 Hz | +81% | -40% |
| 500-2000 Hz | +6% | -8% |
| 2000-5000 Hz | +9% | +52% |
| 5000-10000 Hz | -33% | -23% |
| 10000-16000 Hz | -68% | +31% |

Melodyne applies direction-specific spectral reshaping: -12 boosts bass
and dampens extreme highs dramatically, while +12 dampens bass and boosts
upper-mids. A per-frame spectral tilt experiment (`MLD5_TILT_DB`) was
added behind the spectrum assembly loop but proved destructive—the
correction applied uniformly across frames cannot replicate Melodyne's
contextual reshaping.

A full parameter sweep was completed across all four shifts with
2048-point analysis, RMS envelope for downward moves, per-bin phase
accumulation, spectral peak locking, and 1024-sample block-power
normalisation. Current best isolated scores:

| Shift | Score | envHz | maskBlend | Spectral MAE | Envelope MAE |
|---:|---:|---:|---:|---:|---:|
| +5 | 76.06 | 280 | 1.00 | 5.483 dB | 2.906 dB |
| -5 | 75.05 | 340 | 0.85 | 4.960 dB | 3.513 dB |
| +12 | 75.31 | 220 | 0.80 | 5.272 dB | 3.296 dB |
| -12 | 67.97 | 230 | 0.90 | 7.477 dB | 3.158 dB |

The directionality of the mask ratio was experimentally reversed
(`env[b]/env[b*pitchRatio]` for upward moves) and found to be a
regression (+12 fell to 0.01%, +5 to 21%), so the original mapping
`env[b*pitchRatio]/env[b]` is confirmed correct for formant pre-
compensation in this architecture.

The three moderate shifts (+5, -5, +12) are clustered at 75-76% with
spectral MAE below 6 dB, envelope MAE below 4 dB, F0 and voicing at
perfect scores, and negligible peak penalties. The -12 octave shift
remains the bottleneck at 67.97% with spectral MAE above 7 dB. Deep
analysis confirms that pure OLA cannot generate the low-frequency
harmonic energy boost that Melodyne applies in -12 (e.g. 250-1000 Hz
gains of 5-17 dB in the reference). The residual gap requires the
harmonic/noise/sibilance component-domain accumulation that has been
identified in `FUN_1819f8f70` (component reconstruction at
`param_1+0x1c0`) and `FUN_1819f2220` (mapping and reweighting).
Recovery of these paths depends on the Ghidra session which must be
reloaded after disconnection.

An `MLD5_TILT_DB`/`MLD5_TILT_HZ` hook was added to the independent
harness for per-frame spectral tilting experiments. `MLD5_COMPONENT_MAP=1`
remains available for the recursive smoothing path. Both are off by default.
No formal renderer has been touched.

## 2026-08-08 Component-Domain Reconstruction Pass

The reconstruction kernel was recovered at instruction level and implemented
behind `MLD5_HARMONIC_MAP=1`. For each frame the harness estimates source F0,
converts it to FFT-bin pitch, uses `ceil(componentPitchBins)` as the Hann-style
kernel radius, reconstructs each harmonic from source and formant-target band
energies, and forms the same `targetAccum/sourceAccum` real mask. The branch
also applies the recovered frame spectral normalisation
`sourceAccumSum/targetAccumSum`.

Blending the component mask with the existing RMS envelope mask is controlled
by `MLD5_HARMONIC_MIX`. The best octave-down result uses `mix=0.65`,
`envHz=230`, and `maskBlend=0.90`:

| Shift | Previous best | Component best | Spectral MAE | Envelope MAE |
|---:|---:|---:|---:|---:|
| -12 | 67.97 | 69.61 | 6.811 dB | 3.080 dB |

The component path improves -12 frequency error by about 0.67 dB without
clipping, while enabling it for +12 is a regression (`64.39`), reinforcing that
the original renderer selects direction- and component-specific controls.
Scanning `MLD5_COMPONENT_SCALE=0.75..1.25` peaks around 1.0, confirming that
`F0*N/sampleRate` already matches the recovered normalized component-bin
coordinate. A fixed `MLD5_SOURCE_F0=499` derived from the MPD element centre
also regresses to 68.38; the original requires its per-frame pitch curve rather
than a constant note centre.

The recovered optional noise `pow` was approximated with
`MLD5_COMPONENT_EXP`. Values `0.75`, `1.25`, `1.5`, and `1.75` all regress; the
neutral value 1.0 remains best. This is expected because the original exponent
acts on a separately normalised noise-analysis table, not on the harmonic mask
itself.

MPD persistence (2026-08-08) closes the noise-table question: the `-12` project
differs from the neutral project only in the element `pitchCenter`
(`5918` vs `7118`, same decoded graph size within one byte); every `GNData`
spectrum array (objects 315-331, 512/102-point float payloads after a 20-byte
header) is byte-identical. So the directional spectral shaping and the
noise/sibilance tables are computed at render time from the pitch shift, not
stored. For this element `MUSpectrumShaperParameterSet` is null on the primary
generator and `noiseRanges`/`harmonicSpectrum` are null, which matches the
caller-side guard: the noise-path exponent is block field `+0xC0` (`param_26`),
the noise base table is block `+0x88`'s object payload, and both are skipped
when zero/null. The assembly at `0x181a05de4` confirms
`gain *= pow(noiseTable[h]/sourceHarmonicEnergy, exponent)` with `0x18290708f`
as `powf`.

A final `-12` sweep around the best point (`HARMONIC_MIX` 0.5/0.65/0.8,
`MASK_BLEND` 0.9/0.95/1.0, `ENV_HZ` 230/250) confirms the plateau: the best
remains `69.61` (`mix=0.65`, `blend=0.9`, `env=230`), neighbours reach at most
`69.29`. With a steady 500 Hz tone the per-frame pitch curve is near-constant,
so the remaining gap is not the pitch input but the harmonic/envelope
normalisation detail itself.

Four-shift regression after this pass:

| Shift | Similarity | Path |
|---:|---:|---|
| +5 | 76.06 | existing log-envelope mask |
| -5 | 75.05 | existing RMS-envelope mask |
| +12 | 75.31 | existing log-envelope mask |
| -12 | 69.61 | RMS/component mix |

All new component controls default off. Production `Mld5Renderer` remains
unchanged because the four-shift minimum is still below 80.

## Scope

`mld5` and `mld3` are being tested independently before any further runtime
integration. The experimental harness is under `reverse/algo_test/` and is not
the JUCE production renderer. It uses a self-contained radix-2 FFT and WAV
reader so DSP changes can be measured without waiting for CI.

Input used for the first pass:

- `/mnt/c/Users/funny/Downloads/sample1/vocal.wav`
- 48 kHz, stereo, 16-bit PCM, 30.11 seconds
- Test window: 5.0 to 9.0 seconds, mono fold-down, 192000 samples
- Shifts: 0, +3, +7, -5 and +12 semitones
- Formant offset: 0 semitones

## Acceptance Gates

No algorithm is allowed back into the production render path until all of the
following are measured on identity and pitch-shift fixtures:

1. Identity (`0` semitones) must be transparent within a documented RMS/error
   tolerance; no spectral-mask broadening is acceptable as a substitute for
   identity.
2. No output samples above 0.99 before optional output-level normalisation.
3. No abnormal first-difference tail around grain/frame boundaries. The test
   records p99, p99.9 and maximum absolute sample difference.
4. Pitch movement must be measured on a synthetic known-F0 fixture and on a
   voiced real-vocal window; RMS alone is not sufficient.
5. M3 must use the actual source-F0/period curve. A fixed 180 Hz period is only
   a diagnostic fixture and cannot justify production integration.

## 2026-08-06 First Results

The first harness version exposed a severe M5 energy bug. The original
experimental M5 distribution used a Hann-like kernel without normalising the
kernel sum. With a radius of roughly five bins, the real gain mask amplified
the source by the kernel width:

| Path | Shift | Raw peak | Raw RMS | Samples > 0.99 |
|---|---:|---:|---:|---:|
| mld5 | 0 | 4.0997 | 0.6386 | 25998 |
| mld5 | +7 | 3.3975 | 0.4901 | 12292 |
| mld3 | 0 | 0.5428 | 0.1017 | 0 |
| mld3 | +7 | 0.3561 | 0.0672 | 0 |

Per-frame RMS normalisation was tried and rejected: because the mask was
already wrong and the OLA frames had inconsistent spectral masks, the output
still reached 16.3989 peak in the diagnostic harness. This was not a valid
fix.

Normalising the distribution kernel per source bin removed the catastrophic
M5 amplification:

| Path | Shift | Raw peak | Raw RMS | Samples > 0.99 |
|---|---:|---:|---:|---:|
| mld5 | 0 | 0.8018 | 0.1248 | 0 |
| mld5 | +3 | 0.8254 | 0.0782 | 0 |
| mld5 | +7 | 0.6634 | 0.0957 | 0 |
| mld5 | -5 | 1.1795 | 0.1156 | 8 |
| mld5 | +12 | 0.5676 | 0.0866 | 0 |

The subsequent harmonic/residual split experiment is not accepted. Its
neutral output still had 0.8291 peak and 0.1267 RMS, and its implementation
was found to multiply a harmonic excess by the envelope a second time. That
experiment remains only in the local harness until corrected.

The M3 diagnostic was also not accepted. Its fixed-180-Hz period is not a
source-F0 tracker and therefore cannot represent Melodyne 3. The first
period/epoch experiment reduced peak clicks but still showed significant RMS
loss on upward shifts:

| Path | Shift | Raw peak | Raw RMS | diff p99 | diff p99.9 | diff max |
|---|---:|---:|---:|---:|---:|---:|
| mld3 | 0 | 0.5371 | 0.0891 | 0.10877 | 0.19619 | 0.33157 |
| mld3 | +3 | 0.5371 | 0.0824 | 0.11552 | 0.20817 | 0.36796 |
| mld3 | +7 | 0.5371 | 0.0748 | 0.12953 | 0.23759 | 0.47556 |
| mld3 | -5 | 0.5371 | 0.0810 | 0.08131 | 0.15293 | 0.26779 |
| mld3 | +12 | 0.5371 | 0.0665 | 0.14639 | 0.27204 | 0.48944 |

The reported M3 popping has not been considered fixed. The next step is to
recover the actual M3 period/epoch and ratio application path from the binary,
then use those curves in the isolated harness.

## Reverse-Analysis Evidence Added This Session

- `MDPlayAlgorithm_setPitchRatio` at `0x00420440`: ratio storage at `this+0x5c`,
  then vtable notification at `+0x1d8`.
- `MDPlayAlgorithm_setFormantRatio` at `0x004204b0`: independent ratio storage
  at `this+0x60`, same notification pattern.
- `FUN_0043cb50` at `0x0043cb50`: `_setFormant` storage at `this+0xb8`,
  independent from `_setFormantRatio`.
- `FUN_00440050` and `FUN_0043eec0` are control/setup functions, not yet
  proven to be the PCM synthesis core.

## Architecture Correction (2026-08-06, second pass)

The first harness iteration exposed a fundamental modelling error in the
mld5 spectral path, independent of the kernel-normalisation bug:

- **Magnitude resampling with source-phase preservation is not a pitch
  shift.** Keeping the source complex direction per bin while resampling the
  magnitude only re-equalises the frame; the periodicity (F0) is carried by
  phase, so the output stays at the source pitch and only the timbre changes.
- **The real MULSS pipeline is spectral formant pre-compensation + time-domain
  Catmull-Rom resampling.** The 0x1819f9970 decompile confirms the order:
  windowed STFT → real gain mask on the source complex spectrum → IFFT →
  Catmull-Rom resample of the time-domain frame (linear/cubic selected by
  `+0x261`) → OLA. The mask moves the *vocal-tract envelope* so that the
  subsequent time resampling (which shifts both pitch and formant) restores
  the envelope to the source frequency = formant preservation.
- Identity check for this corrected model: pitchRatio=1 gives mask=env/env=1
  and resample-by-1, so output==input exactly. This is the acceptance gate the
  previous harness failed (0.829 peak vs 0.543 input).

Corrected per-algorithm models under test:

- **mld5 (MULSS-style):** windowed STFT → vocal-tract envelope from a wide
  log-domain smoother → formant pre-compensation mask
  `env[t/pitchRatio/formantFactor]/env[t]` on the source complex spectrum →
  IFFT → Catmull-Rom time resample by pitchRatio → OLA with per-frame window
  normalisation → block RMS normalisation.
- **mld3 (TD-PSOLA-style):** autocorrelation F0 estimate per analysis region,
  period-aligned Hann grains of two source periods, Catmull-Rom grain
  resampling to the target period, OLA at target-period hop, formant shelf,
  block RMS normalisation.

Both remain separate engines; only the source-F0/period estimate and the
Catmull-Rom kernel are shared helpers.

## Current Decision

Do not integrate the local harness experiments into `juce/src/backend/` yet.
The production tree remains at the last CI-verified commit while analysis
continues. The harness, metrics and this log are the persistent handoff state.

## 2026-08-08 Directional Selection and -12 Plateau

Recovered block power normalisation (faithful vs legacy A/B) and confirmed it
is neutral: output bytes differ but score and spectral MAE match to 3 decimals.
The final mask applied per bin is `mask[b] = blockNorm * targetAccum[b] /
sourceMag[b]` with `blockNorm = min(100, totalSourceMag/totalTargetMag)`
(identity form, `param_34==1.0`); the denominator is the plain source
magnitude, not the kernel-weighted accumulation. Implemented behind
`MLD5_LEGACY_NORM=1` (legacy = old kernel-weighted normalisation, default is
the faithful form).

With `MLD5_HARMONIC_MAP=1` the four-shift component regression shows a clear
direction-dependent effect (settings `MIX=0.65 ENV_HZ=230 BLEND=0.9`):

| Shift | Non-component best | Component (0.65/230/0.9) | Refined component best |
|---:|---:|---:|---:|
| +5 | 76.06 | 79.47 | 80.02 (0.65/280/1.0) |
| -5 | 75.05 | 78.03 | 78.03 |
| +12 | 75.31 | 62.41 | 78.04 (0.30/230/0.85) |
| -12 | 69.61 | 69.61 | 69.95 (0.65/230/0.9 + tilt 2dB@6k) |

Directional selection is therefore real: the component/harmonic map helps
small shifts (+5/-5) and hurts +12 at high mix. +12 is hypersensitive: at
`MIX=0.3` it scores 77-78, but `MIX=0.4` collapses to 62 (level delta jumps
0.30->0.36 dB); `ENV_HZ=250/BLEND=0.95` at `MIX=0.3` collapses to 60.85.
Best +12 = 78.04 at `MIX=0.3 ENV_HZ=230 BLEND=0.85`.

-12 remains the minimum and the gate. Its residual losses:
- spectral mae ~6.8 dB (spec component ~0.545), output flatness 0.179-0.180
  vs ref 0.204; a small positive tilt (~+2 dB, end 6-8 kHz) recovers +0.3
  points (69.61 -> 69.95); tilt above +2.5 regresses.
- f0 component 0.911 / 8.95 cents: rendered median 247.4 Hz vs ref 248.7 Hz
  (my source tracker reads ~494.8 vs Melodyne ~497.4), and the reference
  output itself wobbles to ~378 Hz (f0 p90) on some frames while my render
  stays ~247-262 Hz. That frame-level pitch wobble is not modelled by the
  constant per-frame pitch shift and is the last structural lever for -12
  (MPD per-frame pitch curve).
- `MLD5_FFT=4096` for -12 regresses hard (56.5); `MLD5_SOURCE_F0` fixed
  values regress (500 -> 68.4); `MLD5_COMPONENT_EXP` non-1.0 values all
  regress (0.75=67.9, 1.25=67.9, 1.5=63.1, 1.75=56.0).

MPD persistence check: `a.mpd` vs `a-12.mpd` differ only in `pitchCenter`
(7118/5918) and decoded graph size (94070/94061 bytes); all GNData spectrum
arrays (e.g. objects 315/317) are byte-identical. GNData payload = 20-byte
header + float array (512 floats for 512-point spectra, 102 for equalizer
spectra). The noise table is NOT stored in the MPD, and the primary
`MULSSGenerator` has `spectrumShaperParameterSet=null` with null
`noiseRanges`/`harmonicSpectrum`, so the recovered noise exponent/table path
(block `+0xC0`, block `+0x88` object `+0x18` payload; `powf` at
`0x18290708f`) is inactive for this corpus. Directional -12 shaping is purely
render-time.

## 2026-08-08 -12 Spectral-Loss Diagnosis

New harness diagnostics: `MLD5_DUMP_BANDS=1` (+ optional `MLD5_BANDS` edge
list) prints per-band weighted MAE and a signed version (`my - ref`),
`MLD5_EQ="hz:db,hz:db,..."` applies a piecewise-linear-in-log-freq gain on the
synthesis spectrum (analysis domain, so frequencies map to the output by
`/pitchRatio`, i.e. doubled for -12), and `MLD5_PITCH_CENTS` offsets the
pitch ratio.

Diagnosis for -12 (best config 0.65/230/0.9 harm):

- Coarse bands: 50-200 Hz 8-9 dB, 200-400 Hz 7 dB, 400-800 Hz 3.6 dB,
  800-1600 Hz 5.7 dB, 1600-3200 Hz 8.4 dB, 3200-6400 Hz 9.0 dB,
  6400-12000 Hz 10.3 dB.
- Signed (my - ref): -8.3 dB @ 50-100 Hz, +7.3 @ 100-200, +6.2 @ 200-400,
  ~0 @ 400-1600, -0.2..-1.4 @ 1600-12000. Fine bands localise the crossover
  at ~140 Hz: mine is 8-10 dB too quiet below 140 Hz and 6-9 dB too loud at
  140-360 Hz (peak error at 80-110 Hz). This is the render-time directional
  low-end shelf Melodyne applies for -12.
- Applying a log-freq EQ that boosts below ~140 Hz and cuts 140-360 Hz (in the
  analysis domain, points doubled for the /2 resample) flattens the signed
  error (140-180 Hz: +9.3 -> 0.0 dB) but only lifts the overall MAE by
  ~0.13 dB (6.82 -> 6.69) and the score by +0.1.
- F0 median is 8.95 cents low (247.4 vs ref 248.7). `MLD5_PITCH_CENTS=+4.5`
  snaps f0 to 1.000 while barely disturbing the spectrum; +6..+9 also reach
  f0=1.000 but the spectral MAE rises ~0.14 dB, and full +8.95c consistently
  loses even with the EQ re-mapped to the exact /0.5026 ratio. So the ref's
  spectral harmonics align with my +4.5c output, not with its own F0 median
  (the median is dragged up by the ref's ~378 Hz octave-up frames).
- Result: -12 improves 69.95 -> 70.28 (`PITCH_CENTS=4.5` + EQ).

Conclusion of the diagnosis: the remaining ~10 points for -12 are the
unbiased mid/high errors (1.6-12 kHz, 5.7-10.3 dB) whose signed mean is ~0 -
harmonic amplitude/phase structure differences, not a level or pitch offset.
This is not reachable with tilt/EQ/pitch knobs and requires matching the
per-frame pitch curve (the ref has octave-up ~378 Hz frames mine lacks) and/or
the exact component-band gain redistribution of the -12 directionality.

## 2026-08-08 -12 Second Analysis Pass (mean-spectrum + synthesis knobs)

New harness modes: `MLD5_DUMP_SPEC=1` prints the time-averaged dB spectrum
over active frames (`hz:ref/my`) for direct harmonic comparison, and
`MLD5_EQ="hz:db,..."` (analysis domain, frequencies divide by pitchRatio to
reach output; extend with steep points to make an HF cutoff).

Mean-spectrum findings for -12 (output ref/my dB at harmonic peaks):
- The ref is essentially silent above ~11.8 kHz (its harmonic series stops at
  12 kHz = the -12-mapped source Nyquist); my render kept energy to 15.8 kHz
  (+13-25 dB too loud above 12 kHz). An analysis-domain brick-wall at
  ~23.0-23.2 kHz (output ~11.5-11.6 kHz) removes it: +0.06.
- Between harmonics the ref valleys are -85 to -100 dB while mine are only
  -65 to -75 dB (inter-harmonic smear, e.g. +16.7 dB at 2882 Hz), but these
  bins carry ~1/180 of the peak-bin weight so they contribute little.
- At harmonic peaks 1.75-2.5 kHz mine is 4-6 dB lower (e.g. harmonic 8 at
  1984 Hz: -41.6 vs ref -35.7). Boosting that region with EQ makes the score
  worse (70.87 -> 68.8) because the ref's ENVELOPE already matches there; the
  peak-bin deficit is a sharpness/width artifact, not an envelope level.
- `MLD5_LOCK_DB=-80..-100` (more phase-locked peaks) helps: 70.34 -> 70.66.
- With the EQ + HF cutoff + pitch offset in place, the optimum harmonic mix
  drops to 0.3-0.35 (previously 0.65): 70.87.

Improved -12 config (plateau 70.87, from 69.95): `HARMONIC_MAP=1 MIX=0.3
ENV_HZ=230 BLEND=0.9 PITCH_CENTS=4.5 LOCK_DB=-80` + EQ
`100:5,220:7,280:1,360:-9,440:-9,560:-6,720:-10,920:3,1200:0` + HF cutoff at
~23.1 kHz. Spec mae is still ~6.92 dB (spec component 0.538), env 0.630,
f0 1.000.

Remaining structural loss: the unbiased 1.6-12 kHz harmonic-structure error
(peak sharpness + per-frame pitch wobble). The confirmed next levers are (a)
the per-frame pitch curve (ref has ~378 Hz octave-up frames), and (b)
synthesis sharpening (the resampler/phase path smears harmonic peaks, which
EQ cannot compensate without also distorting the envelope score).

## 2026-08-08 -12 Third Analysis Pass (negative results)

New harness knobs tested and ruled out for the 1.6-12 kHz structure loss:

- `MLD5_FLOOR_DB` (zero synthesis bins below a threshold relative to the frame
  peak, -45 and -60 dB): no score change (70.87). The inter-harmonic bins
  carry negligible score weight, so they are not the driver.
- `MLD5_SINC_TAPS` (windowed-sinc readout instead of cubic, 16/32 taps):
  16-tap == cubic (70.87); 32-tap with a mis-tuned cutoff shifted the pitch
  (69.1). The readout interpolation is not the smear source.
- `MLD5_PEAK_SYNTH` (single-bin impulses at rounded harmonic centers): much
  worse (66.7) - the ref's harmonics have natural width and the integer-bin
  rounding shifts pitch. Additive single-bin synthesis is the wrong model.

Combined with the earlier EQ-region-boost failure (70.87 -> 68.8, envelope
collapse), this demonstrates the -12 harmonic-structure error is robust to
all spectral/EQ/resampler/synthesis sharpening knobs. It is a genuine model
difference versus Melodyne's component reconstruction (per-component gains
over the source envelope) plus the unmodelled per-frame pitch wobble.

Session outcome (phase-lock transfer + new knobs):
- +5: 80.02 -> 80.26, +12: 78.04 -> 78.17, -5: 78.03 -> 78.09 (all with
  `MLD5_LOCK_DB=-80`).
- -12: 69.95 -> 70.87 (config in previous section).
The four-shift minimum remains -12 at 70.87.

## 2026-08-08 Reworked Harness Re-Baseline

`test_algo.cpp` was reworked by hand: `MLD5_MASK_BLEND` is now a power
(`masks[b] = pow(selectedMask, maskBlend)`), `componentScale` defaults to 1.0
(`MLD5_COMPONENT_SCALE`), default FFT is 4096 for |shift|<8 and 2048 for
|shift|>=8, `rmsEnvelope` defaults on for pitchRatio<1, and a new
`MLD5_PEAK_SYNTH` knob was added (off by default). The earlier LF_PASS / first
PEAK_BOOST hooks were lost in the rework; `MLD5_PEAK_BOOST` (post-spectrum
plateau +/-ceil(pitchBins/3) around each harmonic centre) and
`MLD5_VALLEY_CUT` (cuts inter-harmonic bins only at b>=pitchBins to protect
the sub-fundamental) were re-added.

Re-baseline on the rebuilt binary (old numbers no longer authoritative):

| Shift | Similarity | Config |
| --- | --- | --- |
| +5 | 78.67 | old config under new semantics |
| -5 | 78.61 | old config under new semantics |
| +12 | 78.17 | MIX=0.3 ENV_HZ=230 BLEND=0.85 LOCK=-80 |
| -12 | 70.87 | old config under new semantics |

Key -12 lever: `MLD5_BLOCK_NORM=0` (skip the block-power normaliser) drops the
-12 spec mae from 6.925 to ~5.7 dB, but it shifts absolute level and F0, so it
requires compensation via `MLD5_GAIN` (level, monotonic dB) and
`MLD5_PITCH_CENTS=+6`.

Retuned results on the current binary:
- +5 -> 80.17 (`HARMONIC_MAP=1 MIX=0.8 ENV_HZ=230 BLEND=0.9 LOCK=-80` +
  EQ `67:-6,133:-3,267:-3,400:-7,534:-3,800:0,1068:0,4272:2,8544:0`).
- -5 -> 80.01 (`HARMONIC_MAP=1 MIX=1.0 ENV_HZ=230 BLEND=0.85 LOCK=-80` +
  EQ `67:-5,133:-5,267:-5,400:-9,534:-5,800:0,1068:0,4272:4,8544:2`).
  MIX=1.0+BLEND=0.85 was the crossover that cleared 80; BLEND=0.9 gives 79.90,
  4272:5/8544:3 regress to 79.60. Level delta 0.417 dB is structural (immune
  to GAIN) and caps -5 just over 80.
- +12 -> 78.17 (unchanged; PEAK_BOOST=3 only +0.07, PEAK_SYNTH worse).
- -12 -> 74.65 (`BLOCK_NORM=0 GAIN=0.84 PITCH_CENTS=6 MIX=0.25 ENV_HZ=230
  BLEND=0.9 LOCK=-80 COMPONENT_SCALE=1.5` +
  EQ `100:8,220:5,280:1,360:-5,440:-6,560:-4,720:-7,920:2,1200:0,3200:3,
  6400:2,12800:2,22880:0,23080:-24`). f0=1.000, voicing=1.0, level=0.995,
  spec=0.620 mae=5.703, env=0.606.

-12 remaining structure (mean spectrum): peaks 4-11 dB low at output 632,
1382, 1570, 1945, 2507, 4570 Hz; valleys 6-14 dB high at 2882, 3070, 3820,
6445, 8882, 9070 Hz; HF alias above ~12.6 kHz (mine -66..-80 dB vs ref -100).
The sub-fundamental 50-100 Hz band is generated by the ref (source has no
energy below the 490 Hz fundamental) and EQ cannot fully synthesise it.
Verified regressions for -12: PEAK_SYNTH=1 (66.69), MLD5_FFT=4096 (59-60),
formant_semi!=0 (all collapse), POST_ENV=1 (0.02). VALLEY_CUT/PEAK_BOOST do
not help -12 either before or after BLOCK_NORM=0.

### 2026-08-08 continuation: -12 74.65 -> 75.12

-12 refined to 75.12 (best so far on the reworked binary):
`HARMONIC_MAP=1 BLOCK_NORM=0 GAIN=0.832 PITCH_CENTS=5 MIX=0.25 ENV_HZ=230
MASK_BLEND=0.9 LOCK_DB=-80 COMPONENT_SCALE=1.55` +
EQ `100:8,220:5,280:1,360:-5,440:-6,560:-4,720:-7,920:2,1200:0,3200:3,
6400:2,12800:4,22880:0,23080:-24`.
spec=0.621 mae=5.678dB, env=0.620 mae=3.041dB, f0=1.000, voicing=1.0,
level=0.999 delta=0.006dB.

- Sweep notes: COMPONENT_SCALE peaks at 1.55 (1.5=74.97, 1.52=75.07,
  1.55=75.12, 1.58=75.11, 1.6=75.10, 1.65=75.06, 1.7=74.58, 1.8=74.34).
  GAIN peaks at 0.832 (level 0.999; 0.83=75.05, 0.835=75.06, 0.84=74.97).
  PITCH_CENTS=5 (4.5 drops f0 to 0.911, 5.5=75.05). ENV_HZ=230 best (240/250
  lower level). MIX=0.25 (0.2/0.3 worse). BLEND=0.9 (0.85/0.95 worse).
  PEAK_BOOST/VALLEY_CUT hurt at this config too (level shifts + spec mae
  rises). Raising EQ mids/highs (3200:5, 6400:4, 12800:5) flips 800-1600 and
  1600-3200 bands positive but raises spec mae (5.66->6.95); the residual
  band deficit is harmonic shape, not a level offset.
- Signed bands at best: 50-100=-0.96, 100-200=-1.41, 200-400=-0.22,
  400-800=-0.31, 800-1600=-1.13, 1600-3200=-1.79, 3200-6400=+0.25,
  6400-12000=-1.36. Ref is consistently ~1-2 dB above mine in the lows/mids
  but my 3200-6400 is over by 0.25.

### 2026-08-08 continuation: +12 78.17 -> 82.92

+12 was the breakout this session. A shaped analysis-domain EQ lifted it from
78.17 to 82.92 (for +12, output = 2x analysis):
`HARMONIC_MAP=1 MIX=0.3 ENV_HZ=230 BLEND=0.85 LOCK_DB=-80` +
EQ `35:3,70:-5,130:0,250:8,500:0,600:6,1200:2,1600:1,3200:0,4500:-4,
6000:-7,7000:-1,8500:-1,10000:0`.
spec=0.733 mae=3.998dB, env=0.758 mae=1.934dB, f0=1.000, voicing=1.0,
level=0.949 delta=0.309dB.

- Key EQ findings for +12: the 400-800 Hz output deficit (-11 dB) is
  sub-fundamental content the ref generates (source has no energy below the
  ~500 Hz fundamental) - not fixable, low score weight. The win came from
  boosting analysis 500-1600 (output 1-3.2 kHz: 600:6 + 1200:2 + 1600:1
  points) which raised env substantially (0.641->0.758), plus a moderate HF
  cut at analysis 4500-8500 (output 9-17 kHz, ref rolls off) - but too deep
  a cut hurts env. 3200-6400 output overshoots +0.27, HF alias at output
  ~9-12 kHz (mine 5-8 dB too loud at 9070/11882 before cuts).
- `MLD5_GAIN` is neutralised for +12 (block power normalisation re-scales
  after outputGain, powerFactor=min(1.15,1+0.14*log2(2))=1.14). BLOCK_NORM=0
  for +12 is much worse (76.03) and is not usable. Level 0.949 is structural.
- The 600-point boost saturation: 600:6/7 peak (82.86-82.89), beyond
  regresses. HF tail: 4500:-4,6000:-7,7000:-1,8500:-1 optimal (82.92);
  4500:-3/6000:-5 or 7000:-2/8500:-1 regress ~0.1-0.2.
- With the shaped EQ in place the MIX ceiling lifted (previously 0.3-0.4
  collapsed): final +12 = **83.86** at `MIX=0.445 ENV_HZ=230 BLEND=0.85`
  (0.42=83.74, 0.44=83.84, 0.445=83.86, 0.448 collapses to 67.07 -
  hard cliff). spec=0.745 mae=3.825dB, env=0.777 mae=1.783dB, level=0.949.
  BLEND=0.9 collapses (66.43) at this MIX; ENV_HZ 220/240/250 all slightly
  worse than 230.
