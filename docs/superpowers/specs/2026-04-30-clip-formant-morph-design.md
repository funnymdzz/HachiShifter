# Clip Formant Morph Design

Date: 2026-04-30

## Summary

Add a clip-level `F` button that opens a small formant-morph editor for that clip.

This feature does **not** use the existing parameter panel or the existing track/root-track `formant_shift_cents` curve system.

Instead, it introduces a new **clip-local pre-processing cache**:

- user edits `enabled + target F1/F2 + strength`
- backend automatically rebuilds a processed audio cache for that clip
- the processed cache becomes the clip's input audio for downstream playback, stretch, synthesis, and export

The DSP is reimplemented in Rust based on the current `vowel_synth.py` prototype, but hardened for product use.

## Goals

- Add a direct clip-level vowel/formant editing entry point.
- Keep the feature independent from the existing parameter panel.
- Rebuild automatically after parameter edits, without requiring an explicit apply action.
- Use the processed audio as an upstream source before all later algorithms.
- Keep the first version project-local in parameters only, with cache rebuilt on demand after project open.

## Non-Goals

- No external Python process invocation.
- No multi-point vowel path editing.
- No preset library in the first version.
- No direct integration with existing `formant_shift_cents` automation.
- No persistent cache files saved into the project package in the first version.
- No real-time continuous "drag while always listening" recompute guarantee.

## User Experience

### Clip Entry

Each clip header shows a persistent `F` button.

- inactive state: plain
- enabled with valid cache: highlighted
- rebuilding: busy state
- failed: warning state

### Formant Popup

Clicking `F` opens a small clip-local popup.

Contents:

- title: `Formant`
- enabled switch
- vowel chart for choosing a single `(F1, F2)` target point
- strength slider from `0%` to `100%`
- status text:
  - `Ready`
  - `Rebuilding`
  - `Failed`

### Rebuild Behavior

When the user changes:

- enabled
- target F1
- target F2
- strength

the backend automatically rebuilds the clip formant cache after a short debounce.

Recommended debounce:

- `120-200ms`

The popup does not need an explicit `Apply` button.

### Playback Behavior

Spacebar keeps existing transport semantics.

It does **not** explicitly trigger a rebuild.

Instead:

- if the new formant cache is ready, timeline playback uses it
- if rebuild is still in progress, timeline playback uses the last valid formant cache
- if no valid formant cache exists yet, playback falls back to the original clip source

Playback always remains **whole-timeline playback**, not isolated clip preview.

### Copy / Duplicate Behavior

Duplicating or copying a clip preserves the formant morph parameters:

- `enabled`
- `targetF1Hz`
- `targetF2Hz`
- `strength`

But the processed audio cache is **not** copied.

The duplicated clip rebuilds its own cache from the new clip identity.

## Data Model

Add a new clip-local field to the project model.

Suggested shape:

```ts
formantMorph?: {
  enabled: boolean;
  targetF1Hz: number;
  targetF2Hz: number;
  strength: number; // 0.0 - 1.0
}
```

Suggested defaults when missing:

```ts
{
  enabled: false,
  targetF1Hz: 800,
  targetF2Hz: 1400,
  strength: 0.95
}
```

These values are stored with the project.

Only parameters are persisted.

Processed audio cache content is **not** persisted in the first version.

## Cache Model

Add a new clip-level runtime cache for formant-processed audio.

Suggested conceptual key:

- clip id
- source path identity
- trimmed source range
- reverse flag
- source sample rate identity
- formant morph enabled flag
- target F1
- target F2
- strength

The cache should be invalidated when any of the above changes.

### Cache Lifecycle

- parameter edit: enqueue rebuild
- clip duplicate: new clip gets same parameters, fresh cache
- clip delete: drop cache
- project open: no cache restored from disk; rebuild on demand
- source replacement: invalidate and rebuild

### Cache Position in Audio Pipeline

The formant cache is inserted **before** downstream algorithm processing.

Order:

1. decode source audio
2. trim / reverse / basic clip-local source preparation
3. formant morph pre-processing cache
4. external stretch or processor-native stretch
5. synthesis / vocoder / render chain
6. timeline playback / export / quick export

This means the `F` result becomes the input to:

- plain audio playback
- `linear / signalsmith / soundtouch`
- `WORLD`
- `HiFiGAN`
- quick export
- normal export

## DSP Design

Implement a new Rust module:

- `backend/src-tauri/src/audio/formant_morph.rs`

The implementation should be inspired by `vowel_synth.py` but not copied mechanically.

### Core Strategy

Use LPC-based formant manipulation with residual re-synthesis:

1. mono analysis signal
2. pre-emphasis
3. frame blocking
4. LPC analysis
5. detect candidate formants
6. move F1 and F2 toward the target
7. constrain pole radius / bandwidth
8. reconstruct from residual
9. overlap-add
10. de-emphasis
11. energy normalization and peak protection

### Engineering Improvements over `vowel_synth.py`

The current Python prototype provides the algorithm direction, but is too fragile for production.

The Rust implementation should improve it in these ways:

#### 1. Better Frame Gating

Avoid aggressively processing silent or near-unvoiced frames.

Use this first-version gating set:

- frame RMS threshold
- zero-crossing-based coarse voicing heuristic

Low-energy frames should fall back to the original frame.

#### 2. Safer Formant Selection

Do not simply pick the first two poles in a broad range.

Prefer a more stable candidate selection:

- search in a valid frequency band
- reject unstable poles
- enforce ordering `F1 < F2`
- skip frames with unreliable candidates

#### 3. Safer Bandwidth Control

The prototype forces aggressive target bandwidths.

The Rust version should use conservative constraints:

- clamp pole radii
- clamp bandwidth movement
- avoid excessive sharpening that creates hiss or metallic artifacts

#### 4. Energy Protection

Protect both:

- per-frame energy
- final output peak / RMS

Use bounded gain compensation rather than unrestricted normalization.

#### 5. Failure Fallback

If a frame fails:

- use the original frame for that frame

If the clip-level failure rate is too high:

- discard the rebuilt result
- keep the previous valid cache if available
- otherwise fall back to original audio

### Channel Strategy

First version:

- analyze in mono
- apply the same reconstructed spectral change to the clip cache output path

Acceptable first-version implementation choices:

- convert source to mono for analysis, but render stereo by applying the same transformation path to both channels
- or build the processed cache as stereo from a shared analysis result

The critical product requirement is consistency and stability, not channel-independent formant modeling.

## Backend Integration

### New Runtime Worker Path

Add a background job queue for clip formant-cache rebuilds, parallel to other async clip preparation work.

The worker should:

- receive clip rebuild requests
- load prepared source PCM
- build the formant-processed cache
- publish completion back to the audio engine / timeline snapshot

### Audio Engine

The audio engine snapshot builder should prefer:

1. valid formant cache, if enabled and available
2. previous valid formant cache, if rebuild still in progress
3. original source PCM, if no valid cache exists

This preserves playback continuity.

### Export Paths

All render/export paths that currently consume clip source audio must be updated to consume:

- formant cache when enabled and valid
- original clip source otherwise

This includes:

- timeline playback
- standard export
- quick export
- synth render paths that prepare clip input audio

## Frontend Integration

### State

Frontend state needs clip-local UI/edit support for:

- popup open/close
- pending rebuild state
- error state

The clip's actual `formantMorph` parameters remain part of the project/timeline model.

### Components

Suggested new frontend pieces:

- clip header `F` button
- `ClipFormantPopup.tsx`
- reusable `VowelChart` component

### Vowel Chart

The chart should be derived from the current prototype, but adapted to the app style.

Behavior:

- single draggable point
- emits `F1/F2`
- shows basic vowel landmarks
- no preset management in first version

### Rebuild Triggering

Frontend should debounce parameter updates before sending rebuild-triggering state updates to backend.

Recommended behavior:

- local UI updates immediately
- backend state update is debounced
- latest edit wins

## Project Open / Save Semantics

### Save

Project save writes:

- `formantMorph` parameters per clip

It does not write:

- generated formant cache audio

### Open

On project open:

- read clip parameters
- do not eagerly rebuild every cache immediately
- rebuild lazily when needed for playback/export or when the popup is edited again

This keeps first-open cost under control.

## Error Handling

### Rebuild Failure

If rebuild fails:

- keep prior valid cache if one exists
- otherwise fall back to original audio
- mark popup state as `Failed`

The failure should not break timeline playback or export.

### Invalid Parameters

Clamp parameters at both UI and backend boundaries:

- `F1` within chart-supported range
- `F2` within chart-supported range
- enforce `F1 < F2` via safe adjustment if needed
- `strength` clamped to `0.0..=1.0`

## Testing

### Backend

- parameter change invalidates formant cache key
- duplicate clip copies parameters but not cache
- disabled formant morph bypasses cache
- enabled formant morph uses cache when ready
- rebuild failure falls back cleanly
- export path consumes formant cache
- `HiFiGAN` / other processor paths receive formant-processed input

### Frontend

- `F` button appears on every clip header
- popup edits update clip-local parameters
- debounce avoids rebuild storm on drag
- status text transitions correctly

### Manual QA

- edit one clip and verify only that clip changes
- duplicate clip and verify parameters copy over
- toggle enabled on/off and verify cache switching
- playback whole timeline while a rebuild is pending
- export and quick export with `F` enabled
- test on voiced vowels, sibilant material, and breathy material

## Rollout Notes

This feature intentionally avoids the existing parameter panel.

That keeps first-version scope contained and avoids forcing a track/root-track model onto a strictly clip-local pre-processing tool.

If the feature proves useful, future expansion can add:

- saved vowel presets
- multi-point formant paths
- clip-local preview buttons
- stronger visual status in timeline
- persistent cache files
