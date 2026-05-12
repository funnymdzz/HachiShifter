# SoundTouch Replacement Design

Date: 2026-04-29

## Summary

Replace the current default time-stretch implementation with SoundTouch on Windows, including the paths that currently rely on Signalsmith Stretch and the NSF-HiFiGAN internal mel-stretch path.

The chosen design is a single external PCM-domain stretch pipeline:

- SoundTouch becomes the default stretch engine for realtime preview, offline mixdown, export, waveform-derived render paths, and prerender cache generation.
- NSF-HiFiGAN no longer performs internal mel-domain time stretch.
- All processors consume PCM that has already been stretched to the target timeline duration.

This is a deliberate behavior change. The goal is not to preserve the current mixed architecture. The goal is to make one stretch algorithm the default everywhere.

## Goals

- Replace the current default stretch engine with SoundTouch.
- Use SoundTouch through a Windows DLL integration.
- Remove the current split behavior where some paths use external stretch and NSF-HiFiGAN uses internal mel stretch.
- Keep one consistent stretch semantic across playback, export, prerender, and clip-level render flows.
- Preserve current timeline semantics:
  - `playback_rate != 1.0` changes clip duration on the timeline
  - pitch editing, formant shift, tension, fades, gain, and overlap mixing still apply

## Non-Goals

- Cross-platform SoundTouch packaging in this phase.
- A user-facing algorithm selector.
- Preserving byte-identical output relative to the old Signalsmith or mel-stretch outputs.
- Reworking the broader pitch-edit data model.

## Current State

The current project uses two different stretch models:

1. External PCM-domain stretch through `SignalsmithStretch`
   - Used in playback, mixdown, export, waveform-related rendering, and general fallback paths.

2. Internal NSF-HiFiGAN mel-domain stretch
   - Used when the processor reports `handles_time_stretch = true`
   - Triggered through `renderer/hifigan.rs::render_mel_stretch(...)`

This means the same timeline-level `playback_rate` can be realized by different algorithms depending on the active processor chain. That makes behavior, bug analysis, and cache semantics harder to reason about.

## Chosen Approach

Use one external stretch stage everywhere and make it SoundTouch-based.

Why this approach:

- It matches the explicit requirement to replace the default stretch algorithm, including the HiFiGAN-linked path.
- It removes the existing dual semantic model.
- It creates one clear boundary: time stretch happens before processor-specific rendering.

Rejected alternatives:

1. Replace only the existing Signalsmith paths and keep HiFiGAN mel stretch.
   - Rejected because it preserves split semantics.

2. Keep the HiFiGAN internal stretch entry point but secretly implement SoundTouch inside it.
   - Rejected because it keeps misleading architecture and special cases.

## Architecture

### 1. New SoundTouch FFI module

Add a new backend module:

- `backend/src-tauri/src/audio/soundtouch.rs`

Responsibilities:

- Load and call the SoundTouch DLL-backed API
- Expose a Rust-facing interface aligned with existing stretch callers
- Provide:
  - `is_available()`
  - `try_time_stretch_interleaved_offline(...)`
  - `try_time_stretch_interleaved_realtime(...)`

The API shape should intentionally resemble `sstretch.rs` so upper layers can switch with minimal churn.

### 2. Stretch algorithm enum and dispatcher

Update:

- `backend/src-tauri/src/audio/time_stretch.rs`

Changes:

- Add `StretchAlgorithm::SoundTouchDll`
- Make it the default algorithm used by command, playback, and export call sites
- Keep existing fallback behavior:
  - if SoundTouch is unavailable or fails, log and fall back to linear resample

Signalsmith may remain temporarily during migration, but SoundTouch becomes the default immediately.

### 3. DLL integration model

Use the same packaging style already used for `vslib_x64.dll`:

- commit the SoundTouch DLL and import library under a dedicated third-party directory
- link against the import library in `build.rs`
- copy the runtime DLL next to the built binary during build

Recommended layout:

- `backend/src-tauri/third_party/soundtouch/`
  - `SoundTouch_x64.dll` or repo-standard renamed DLL
  - matching import library
  - optional local README describing the pinned version

This keeps distribution behavior consistent with existing native DLL handling in the repo.

### 4. Remove HiFiGAN internal time-stretch ownership

Update:

- `backend/src-tauri/src/renderer/chain.rs`
- `backend/src-tauri/src/renderer/hifigan.rs`
- `backend/src-tauri/src/pitch_editing.rs`
- `backend/src-tauri/src/audio/mixdown.rs`
- `backend/src-tauri/src/audio_engine/snapshot.rs`
- `backend/src-tauri/src/commands/playback.rs`

Behavior change:

- `hifigan_chain()` no longer reports `handles_time_stretch = true`
- `render_mel_stretch(...)` is retired
- HiFiGAN always receives PCM already stretched to timeline duration

New rule:

- timeline stretch is always resolved outside the processor
- processor-specific rendering then applies pitch/formant/tension/breath-related work to the stretched PCM

This is the key architectural simplification in the design.

## Data Flow

### Realtime preview

1. Decode/resample source PCM
2. Apply SoundTouch realtime stretch to target timeline duration when `playback_rate != 1.0`
3. Run pitch-edit or processor rendering on the stretched PCM
4. Mix into output stream

### Offline mixdown/export

1. Decode/resample source PCM
2. Apply SoundTouch offline stretch to target timeline duration when `playback_rate != 1.0`
3. Run pitch-edit or processor rendering on the stretched PCM
4. Apply fades, gain, automation, overlap mix
5. Write output file

### NSF-HiFiGAN path after change

1. Decode/resample source PCM
2. Stretch PCM with SoundTouch
3. Feed stretched PCM into the HiFiGAN rendering path
4. Apply formant/tension/breath processing as supported by the renderer

There is no separate mel-stretch branch after this change.

## File-Level Impact

Expected primary change set:

- `backend/src-tauri/src/audio/soundtouch.rs`
- `backend/src-tauri/src/audio/time_stretch.rs`
- `backend/src-tauri/src/audio/mixdown.rs`
- `backend/src-tauri/src/audio_engine/engine.rs`
- `backend/src-tauri/src/audio_engine/stretch_stream.rs`
- `backend/src-tauri/src/audio_engine/snapshot.rs`
- `backend/src-tauri/src/commands/common.rs`
- `backend/src-tauri/src/commands/playback.rs`
- `backend/src-tauri/src/commands/synth.rs`
- `backend/src-tauri/src/commands/waveform.rs`
- `backend/src-tauri/src/renderer/chain.rs`
- `backend/src-tauri/src/renderer/hifigan.rs`
- `backend/src-tauri/src/pitch_editing.rs`
- `backend/src-tauri/build.rs`

Possible supporting test files will be added next to the affected modules.

## Versioning and Source Pinning

SoundTouch documentation reviewed:

- Main site: <https://www.surina.net/soundtouch/>
- README: <https://www.surina.net/soundtouch/README.html>
- Downloads: <https://www.surina.net/soundtouch/download.html>

Important note:

- The documentation pages visibly reference different version labels in different places.
- Implementation must pin one concrete DLL/import-library version and document it locally in the repo.
- The build must not silently mix headers, libs, or DLLs from different releases.

## Error Handling

- If the SoundTouch DLL is missing at runtime, the backend should surface a clear diagnostic and fall back to linear resample rather than panic.
- If a SoundTouch processing call fails, the failing clip render should log enough context to identify:
  - path type
  - clip id
  - sample rate
  - channel count
  - requested ratio
- Build-time messages should clearly report when the SoundTouch third-party assets are missing.

## Testing

### Unit and integration tests

- `time_stretch.rs`
  - output length matches requested target frames
  - identity ratio keeps stable output shape
  - failure path falls back cleanly

- playback and mixdown paths
  - stretch call sites now route to SoundTouch default
  - no caller still hardcodes `SignalsmithStretch`

- HiFiGAN path
  - no remaining internal mel-stretch branch is exercised
  - `handles_time_stretch` expectations are updated
  - stretched-PCM flow still reaches pitch/formant processing correctly

### Manual validation

- Realtime playback of stretched clips
- Export of stretched clips
- NSF-HiFiGAN clips with:
  - stretch only
  - stretch + pitch edit
  - stretch + formant shift
  - stretch + breath/tension

## Risks

### 1. Audible output changes

This change is expected to alter sound relative to both:

- current Signalsmith-based PCM stretch
- current HiFiGAN mel-stretch path

That is acceptable within scope, but it must be validated intentionally.

### 2. Realtime latency

SoundTouch documentation explicitly notes non-trivial processing latency for time-stretch use. Realtime preview quality and responsiveness must be re-evaluated after integration.

### 3. Cache behavior

Any cache keyed on stretch assumptions, processor capabilities, or rendered output semantics must be reviewed. A stale cache that still assumes internal HiFiGAN stretch would create confusing mismatches.

## Migration Notes

- First introduce SoundTouch behind a new backend module and wire it as the default dispatcher target.
- Then remove the HiFiGAN internal stretch ownership and collapse callers to the new external model.
- Only after verification should old Signalsmith-specific assumptions be deleted aggressively.

This sequencing reduces the chance of breaking all render paths at once while still converging on the final single-algorithm architecture.
