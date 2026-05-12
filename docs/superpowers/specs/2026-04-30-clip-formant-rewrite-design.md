# Clip Formant Rewrite Design

## Goal

Rewrite the clip-level `F` formant morph DSP so it follows the proven processing flow in `vowel_synth.py` and works reliably in timeline playback and export without changing the existing clip-level UI, command surface, or cache wiring.

## Problem Statement

The current clip-level formant feature already has:

- frontend editing UI
- clip state serialization
- backend rebuild scheduling
- timeline playback integration
- export integration

The unstable part is the DSP core in `backend/src-tauri/src/audio/formant_morph.rs`.

The current Rust implementation uses a custom LPC pole migration path with a handwritten complex QR root solver. That increases the chance of:

- analysis failure on ordinary voiced frames
- frequent silent fallback to passthrough
- weak or inconsistent audible change
- behavior that diverges from the `vowel_synth.py` reference

## Scope

### In Scope

- rewrite the DSP implementation in `backend/src-tauri/src/audio/formant_morph.rs`
- preserve the existing clip-level `formant_morph` state shape
- preserve current command, cache, snapshot, playback, and mixdown entry points
- add regression tests around audible change and output stability

### Out of Scope

- redesigning the clip-level formant UI
- changing the `formant_morph` payload shape
- changing cache ownership or rebuild scheduling semantics
- introducing a different spectral-warping algorithm

## Recommended Approach

Replace the current DSP core with a Rust implementation that mirrors the `vowel_synth.py` processing stages closely:

1. pre-emphasis
2. 25 ms frame analysis
3. quarter-hop overlap-add
4. LPC analysis on windowed frames
5. identify the first two valid formant candidates in the 150-3000 Hz band
6. move those candidates toward target `F1` and `F2` with strength scaling
7. synthesize a residual with the original LPC filter
8. resynthesize audio with the modified LPC filter
9. energy-match each processed frame
10. overlap-add, de-emphasize, and peak-protect the final signal

This keeps the acoustic behavior aligned with the working prototype while staying compatible with the existing product pipeline.

## Architecture

### Stable Surface

The following modules remain the public integration surface and should not change behaviorally:

- `backend/src-tauri/src/commands/timeline.rs`
- `backend/src-tauri/src/formant_cache.rs`
- `backend/src-tauri/src/audio_engine/snapshot.rs`
- `backend/src-tauri/src/audio/mixdown.rs`
- `backend/src-tauri/src/commands/playback.rs`

They continue to call the same `apply_formant_morph_mono()` and `apply_formant_morph_interleaved()` entry points.

### Rewritten Core

`backend/src-tauri/src/audio/formant_morph.rs` becomes a focused DSP module with these responsibilities:

- parameter clamping and early bypass
- frame preparation and windowing
- LPC coefficient estimation
- formant candidate extraction and migration
- residual extraction and all-pole reconstruction
- per-frame energy normalization
- full-buffer de-emphasis and peak protection

### Simplification Principle

The rewrite should reduce implementation novelty, not add more.

Specifically:

- prefer a direct LPC/formant migration path that matches the Python reference
- remove the current custom QR-based polynomial root solver from the main processing path
- keep any fallback behavior conservative and explicit

## Data Flow

### Mono Path

`apply_formant_morph_mono(input, sample_rate, params)` should:

1. return input unchanged when disabled, too short, too low-rate, or near-zero strength
2. clamp target formants to the chart bounds already used by the UI
3. run frame-based LPC formant morphing
4. return a same-length mono buffer with finite samples

### Interleaved Path

`apply_formant_morph_interleaved(input, sample_rate, channels, params)` should:

1. preserve current API behavior
2. derive a mono analysis signal from the channel average
3. process the mono signal through the rewritten mono path
4. apply the wet-minus-dry delta back to each channel
5. preserve interleaved frame count and channel layout

This keeps stereo behavior consistent with the existing clip cache and playback code.

## Error Handling

Frame-level failures should not abort the whole clip.

Rules:

- if a frame is too low-energy, pass it through
- if LPC analysis fails for a frame, pass that frame through
- if formant candidate extraction fails for a frame, pass that frame through
- only return the untouched full input buffer when no usable processing happened at all

Debug logging may stay, but it must describe why processing fell back rather than obscure the failure mode.

## Testing Strategy

### DSP Unit Tests

Add or update tests in `backend/src-tauri/src/audio/formant_morph.rs` to cover:

- disabled morph returns the original samples
- enabled morph preserves output length
- enabled morph keeps samples finite
- stronger morph causes greater waveform deviation than weaker morph on the same source
- stereo interleaved processing preserves shape and length
- ordinary voiced synthetic input produces a non-trivial audible difference from the dry signal

### Integration Confidence

Because playback and export already call the shared DSP entry points through existing cache helpers, no new interface layer is required.

The rewrite is successful when the same DSP output is consumed by:

- formant cache rebuilds
- timeline snapshot playback
- ad hoc playback render paths
- mixdown/export paths

## Risks and Mitigations

### Risk: Audible change is still too weak

Mitigation:

- bias test coverage toward minimum detectable change on voiced synthetic material
- align frame energy matching and pole movement behavior with the Python reference instead of the current Rust approximation

### Risk: Frame-level instability causes widespread passthrough

Mitigation:

- simplify the analysis path
- avoid the current custom root-solving approach in the main DSP flow
- keep failure accounting visible in debug logs

### Risk: Playback and export diverge

Mitigation:

- do not fork the algorithm by consumer
- keep all call sites on the same shared DSP module

## Implementation Notes

- preserve ASCII source edits unless a file already requires otherwise
- prefer targeted tests over broad refactors
- do not modify frontend behavior unless a backend contract bug forces a minimal compatibility fix

## Success Criteria

The rewrite is complete when:

- changing clip-level formant settings produces consistent audible change
- stronger settings clearly produce more change than weaker settings
- timeline playback and export both use the rewritten output
- the DSP no longer depends on the current custom QR root solver in its main processing path
- automated tests cover the new expected behavior
