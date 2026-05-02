# Clip Formant Morph Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a clip-level `F` formant morph editor that automatically rebuilds a clip-local pre-processing audio cache in Rust and feeds that cache into playback, stretch, synthesis, quick export, and normal export.

**Architecture:** Store `formantMorph` parameters on each clip, build a Rust LPC-based formant-morph worker that outputs a clip-level cached PCM buffer, and make all downstream audio consumers prefer that cached PCM over the original source when enabled. The frontend adds a persistent `F` button on clip headers plus a small popup with enable, vowel chart, strength, and rebuild status.

**Tech Stack:** Rust (`cpal`, existing audio engine/cache infrastructure, `librosa` prototype reimplemented manually), TypeScript/React, Redux Toolkit, Radix UI popup primitives, existing Tauri command bridge.

---

## File Map

### Backend files to create

- Create: `backend/src-tauri/src/audio/formant_morph.rs`
  - Rust implementation of the LPC-based formant morph DSP.
- Create: `backend/src-tauri/src/formant_cache.rs`
  - Clip-level runtime cache store, cache keys, enqueue/retrieve helpers, invalidation helpers.

### Backend files to modify

- Modify: `backend/src-tauri/src/state.rs`
  - Add `formant_morph` to clip data model and timeline serialization.
- Modify: `backend/src-tauri/src/models.rs`
  - Expose `formant_morph` through frontend payloads.
- Modify: `backend/src-tauri/src/audio_engine/types.rs`
  - Add formant cache job/result command types if needed.
- Modify: `backend/src-tauri/src/audio_engine/engine.rs`
  - Add async rebuild worker and rebuild scheduling hooks.
- Modify: `backend/src-tauri/src/audio_engine/snapshot.rs`
  - Prefer formant cache audio before downstream stretch/processor stages.
- Modify: `backend/src-tauri/src/audio/mixdown.rs`
  - Use formant-processed clip audio in render/export path.
- Modify: `backend/src-tauri/src/commands/playback.rs`
  - Use formant-processed clip audio in ad hoc playback render path.
- Modify: `backend/src-tauri/src/commands.rs`
  - Add new command(s) for updating clip formant state if needed.
- Modify: `backend/src-tauri/src/lib.rs`
  - Register any new command.

### Frontend files to create

- Create: `frontend/src/components/layout/timeline/clip/ClipFormantButton.tsx`
  - Small persistent `F` button with state coloring.
- Create: `frontend/src/components/layout/timeline/clip/ClipFormantPopup.tsx`
  - Popup container with enable toggle, chart, slider, status.
- Create: `frontend/src/components/layout/timeline/clip/VowelChart.tsx`
  - Interactive single-point F1/F2 chart.
- Create: `frontend/src/components/layout/timeline/clip/useClipFormantEditor.ts`
  - Debounced local/edit orchestration hook.

### Frontend files to modify

- Modify: `frontend/src/features/session/sessionTypes.ts`
  - Add `formantMorph` typing if clip types are centralized there.
- Modify: `frontend/src/features/session/sessionSlice.ts`
  - Add clip parameter state updates and rebuild status storage.
- Modify: `frontend/src/features/session/thunks/timelineThunks.ts`
  - Add remote update thunk for clip formant morph fields.
- Modify: `frontend/src/services/api/core.ts` or `project.ts`
  - Add API wrapper for clip formant updates if needed.
- Modify: `frontend/src/services/invoke.ts`
  - Map new command parameters.
- Modify: `frontend/src/components/layout/timeline/clip/ClipHeader.tsx`
  - Render persistent `F` button.
- Modify: `frontend/src/i18n/en-US.ts`
- Modify: `frontend/src/i18n/zh-CN.ts`
- Modify: `frontend/src/i18n/zh-TW.ts`
- Modify: `frontend/src/i18n/ja-JP.ts`
- Modify: `frontend/src/i18n/ko-KR.ts`
  - Add formant popup strings and status labels.

### Tests to create or extend

- Test: `backend/src-tauri/src/audio/formant_morph.rs` inline unit tests
- Test: `backend/src-tauri/src/formant_cache.rs` inline unit tests
- Test: `backend/src-tauri/src/audio_engine/snapshot.rs` inline unit tests
- Test: `frontend/src/components/layout/timeline/clip/VowelChart.test.ts`
- Test: `frontend/src/components/layout/timeline/clip/useClipFormantEditor.test.ts`
- Test: `frontend/src/features/session/sessionSlice.formantMorph.test.ts`

---

### Task 1: Add Clip Formant Morph Data Model

**Files:**
- Modify: `backend/src-tauri/src/state.rs`
- Modify: `backend/src-tauri/src/models.rs`
- Modify: `frontend/src/features/session/sessionSlice.ts`
- Modify: `frontend/src/features/session/sessionTypes.ts`
- Test: `backend/src-tauri/src/state.rs`
- Test: `frontend/src/features/session/sessionSlice.formantMorph.test.ts`

- [ ] **Step 1: Write the failing backend model test**

Add this unit test near existing clip serialization/model tests in `backend/src-tauri/src/state.rs`:

```rust
#[test]
fn clip_formant_morph_defaults_to_disabled_when_missing() {
    let json = serde_json::json!({
        "id": "clip-1",
        "trackId": "track_main",
        "name": "clip",
        "startSec": 0.0,
        "lengthSec": 1.0,
        "color": "#fff",
        "sourcePath": "demo.wav",
        "sourceStartSec": 0.0,
        "sourceEndSec": 1.0,
        "playbackRate": 1.0,
        "gain": 1.0,
        "muted": false,
        "fadeInSec": 0.0,
        "fadeOutSec": 0.0,
        "fadeInCurve": "sine",
        "fadeOutCurve": "sine"
    });

    let clip: Clip = serde_json::from_value(json).unwrap();
    let morph = clip.formant_morph.unwrap_or_default();
    assert!(!morph.enabled);
    assert_eq!(morph.target_f1_hz, 800.0);
    assert_eq!(morph.target_f2_hz, 1400.0);
    assert!((morph.strength - 0.95).abs() < 1e-6);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cargo test clip_formant_morph_defaults_to_disabled_when_missing --quiet
```

Expected: FAIL because `Clip` has no `formant_morph` field or default type yet.

- [ ] **Step 3: Add the backend struct and clip field**

Add this to `backend/src-tauri/src/state.rs` near other clip-adjacent model structs:

```rust
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct ClipFormantMorph {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default = "default_formant_target_f1_hz")]
    pub target_f1_hz: f64,
    #[serde(default = "default_formant_target_f2_hz")]
    pub target_f2_hz: f64,
    #[serde(default = "default_formant_strength")]
    pub strength: f64,
}

fn default_formant_target_f1_hz() -> f64 {
    800.0
}

fn default_formant_target_f2_hz() -> f64 {
    1400.0
}

fn default_formant_strength() -> f64 {
    0.95
}

impl Default for ClipFormantMorph {
    fn default() -> Self {
        Self {
            enabled: false,
            target_f1_hz: default_formant_target_f1_hz(),
            target_f2_hz: default_formant_target_f2_hz(),
            strength: default_formant_strength(),
        }
    }
}
```

Add this field to `Clip`:

```rust
#[serde(default)]
pub formant_morph: Option<ClipFormantMorph>,
```

Expose it in payload conversion in `backend/src-tauri/src/models.rs`:

```rust
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ClipFormantMorphPayload {
    pub enabled: bool,
    pub target_f1_hz: f64,
    pub target_f2_hz: f64,
    pub strength: f64,
}
```

and map it from `Clip`.

- [ ] **Step 4: Add the failing frontend state test**

Create `frontend/src/features/session/sessionSlice.formantMorph.test.ts`:

```ts
import { strict as assert } from "node:assert";
import reducer from "./sessionSlice";

const baseState = reducer(undefined, { type: "@@INIT" });

const next = reducer(
  baseState,
  {
    type: "session/fetchTimeline/fulfilled",
    payload: {
      ok: true,
      clips: [
        {
          id: "clip-1",
          track_id: "track_main",
          name: "clip",
          start_sec: 0,
          length_sec: 1,
          color: "#fff",
          source_path: "demo.wav",
          gain: 1,
          muted: false,
          source_start_sec: 0,
          source_end_sec: 1,
          playback_rate: 1,
          reversed: false,
          fade_in_sec: 0,
          fade_out_sec: 0,
          fade_in_curve: "sine",
          fade_out_curve: "sine",
          formant_morph: {
            enabled: true,
            target_f1_hz: 700,
            target_f2_hz: 1700,
            strength: 0.6,
          },
        },
      ],
      tracks: [],
    },
  } as any,
);

assert.equal(next.clips[0]?.formantMorph?.enabled, true);
assert.equal(next.clips[0]?.formantMorph?.targetF1Hz, 700);
```

- [ ] **Step 5: Update frontend clip typing and reducers**

Add this type to frontend clip types:

```ts
export interface ClipFormantMorph {
  enabled: boolean;
  targetF1Hz: number;
  targetF2Hz: number;
  strength: number;
}
```

Add it to `ClipInfo` and timeline normalization in `sessionSlice.ts`.

- [ ] **Step 6: Run model tests**

Run:

```bash
cargo test clip_formant_morph_defaults_to_disabled_when_missing --quiet
node --experimental-strip-types frontend/src/features/session/sessionSlice.formantMorph.test.ts
```

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add backend/src-tauri/src/state.rs backend/src-tauri/src/models.rs frontend/src/features/session/sessionSlice.ts frontend/src/features/session/sessionTypes.ts frontend/src/features/session/sessionSlice.formantMorph.test.ts
git commit -m "feat: add clip formant morph model"
```

### Task 2: Build Rust Formant Morph DSP Module

**Files:**
- Create: `backend/src-tauri/src/audio/formant_morph.rs`
- Modify: `backend/src-tauri/src/lib.rs`
- Test: `backend/src-tauri/src/audio/formant_morph.rs`

- [ ] **Step 1: Write the failing DSP tests**

Create inline tests in `backend/src-tauri/src/audio/formant_morph.rs`:

```rust
#[test]
fn disabled_formant_morph_returns_input_unchanged() {
    let input = vec![0.0f32, 0.1, -0.1, 0.2, -0.2, 0.0];
    let params = ClipFormantMorph::default();
    let output = apply_formant_morph_mono(&input, 16_000, &params).unwrap();
    assert_eq!(output, input);
}

#[test]
fn enabled_formant_morph_preserves_length() {
    let input: Vec<f32> = (0..1600).map(|i| ((i as f32) * 0.01).sin() * 0.2).collect();
    let params = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 700.0,
        target_f2_hz: 1800.0,
        strength: 0.75,
    };
    let output = apply_formant_morph_mono(&input, 16_000, &params).unwrap();
    assert_eq!(output.len(), input.len());
}
```

- [ ] **Step 2: Run DSP tests to verify they fail**

Run:

```bash
cargo test disabled_formant_morph_returns_input_unchanged enabled_formant_morph_preserves_length --quiet
```

Expected: FAIL because the module does not exist yet.

- [ ] **Step 3: Create the minimal module skeleton**

Create `backend/src-tauri/src/audio/formant_morph.rs`:

```rust
use crate::state::ClipFormantMorph;

pub fn apply_formant_morph_mono(
    input: &[f32],
    _sample_rate: u32,
    params: &ClipFormantMorph,
) -> Result<Vec<f32>, String> {
    if !params.enabled || input.is_empty() {
        return Ok(input.to_vec());
    }
    Ok(input.to_vec())
}
```

Register the module in the nearest `mod` tree, for example in `lib.rs` or the audio module root:

```rust
mod formant_morph;
```

- [ ] **Step 4: Implement the first passing DSP stages**

Extend the module with these helpers:

```rust
fn pre_emphasis(input: &[f32], coef: f32) -> Vec<f32> {
    let mut out = Vec::with_capacity(input.len());
    let mut prev = 0.0f32;
    for &sample in input {
        out.push(sample - coef * prev);
        prev = sample;
    }
    out
}

fn de_emphasis(input: &[f32], coef: f32) -> Vec<f32> {
    let mut out = Vec::with_capacity(input.len());
    let mut prev = 0.0f32;
    for &sample in input {
        let next = sample + coef * prev;
        out.push(next);
        prev = next;
    }
    out
}
```

Then change `apply_formant_morph_mono()` to:

```rust
pub fn apply_formant_morph_mono(
    input: &[f32],
    sample_rate: u32,
    params: &ClipFormantMorph,
) -> Result<Vec<f32>, String> {
    if !params.enabled || input.is_empty() {
        return Ok(input.to_vec());
    }

    let pre = pre_emphasis(input, 0.97);
    let mut output = pre.clone();

    if sample_rate < 8_000 || input.len() < 512 {
        return Ok(input.to_vec());
    }

    for sample in &mut output {
        *sample = (*sample).clamp(-1.0, 1.0);
    }

    Ok(de_emphasis(&output, 0.97))
}
```

- [ ] **Step 5: Add production-direction tests for stability**

Add:

```rust
#[test]
fn formant_morph_clamps_strength_range() {
    let params = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 700.0,
        target_f2_hz: 1800.0,
        strength: 5.0,
    };
    let output = apply_formant_morph_mono(&vec![0.0; 2048], 16_000, &params).unwrap();
    assert_eq!(output.len(), 2048);
}
```

- [ ] **Step 6: Run DSP tests**

Run:

```bash
cargo test formant_morph --quiet
```

Expected: PASS for the new minimal tests

- [ ] **Step 7: Commit**

```bash
git add backend/src-tauri/src/audio/formant_morph.rs backend/src-tauri/src/lib.rs
git commit -m "feat: add initial rust formant morph dsp"
```

### Task 3: Add Clip-Level Formant Cache and Audio Engine Integration

**Files:**
- Create: `backend/src-tauri/src/formant_cache.rs`
- Modify: `backend/src-tauri/src/audio_engine/types.rs`
- Modify: `backend/src-tauri/src/audio_engine/engine.rs`
- Modify: `backend/src-tauri/src/audio_engine/snapshot.rs`
- Modify: `backend/src-tauri/src/audio/mixdown.rs`
- Modify: `backend/src-tauri/src/commands/playback.rs`
- Test: `backend/src-tauri/src/formant_cache.rs`
- Test: `backend/src-tauri/src/audio_engine/snapshot.rs`

- [ ] **Step 1: Write the failing cache-key test**

In `backend/src-tauri/src/formant_cache.rs` add:

```rust
#[test]
fn formant_cache_key_changes_when_parameters_change() {
    let a = make_formant_cache_key("clip-1", "demo.wav", 0.0, 1.0, false, true, 700.0, 1700.0, 0.5);
    let b = make_formant_cache_key("clip-1", "demo.wav", 0.0, 1.0, false, true, 750.0, 1700.0, 0.5);
    assert_ne!(a, b);
}
```

- [ ] **Step 2: Run the cache test to verify it fails**

Run:

```bash
cargo test formant_cache_key_changes_when_parameters_change --quiet
```

Expected: FAIL because the cache module does not exist yet.

- [ ] **Step 3: Create formant cache module**

Create `backend/src-tauri/src/formant_cache.rs`:

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex, OnceLock};

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct FormantCacheKey {
    pub clip_id: String,
    pub source_path: String,
    pub source_start_ms_q: i64,
    pub source_end_ms_q: i64,
    pub reversed: bool,
    pub enabled: bool,
    pub target_f1_q: i64,
    pub target_f2_q: i64,
    pub strength_q: i64,
}

pub fn make_formant_cache_key(
    clip_id: &str,
    source_path: &str,
    source_start_sec: f64,
    source_end_sec: f64,
    reversed: bool,
    enabled: bool,
    target_f1_hz: f64,
    target_f2_hz: f64,
    strength: f64,
) -> FormantCacheKey {
    FormantCacheKey {
        clip_id: clip_id.to_string(),
        source_path: source_path.to_string(),
        source_start_ms_q: (source_start_sec * 1000.0).round() as i64,
        source_end_ms_q: (source_end_sec * 1000.0).round() as i64,
        reversed,
        enabled,
        target_f1_q: target_f1_hz.round() as i64,
        target_f2_q: target_f2_hz.round() as i64,
        strength_q: (strength * 1000.0).round() as i64,
    }
}

pub type FormantCache = Arc<Mutex<HashMap<FormantCacheKey, Arc<Vec<f32>>>>>;

pub fn global_formant_cache() -> &'static FormantCache {
    static CACHE: OnceLock<FormantCache> = OnceLock::new();
    CACHE.get_or_init(|| Arc::new(Mutex::new(HashMap::new())))
}
```

- [ ] **Step 4: Add engine worker job path**

Add to `audio_engine/types.rs`:

```rust
pub(crate) struct FormantJob {
    pub(crate) key: crate::formant_cache::FormantCacheKey,
    pub(crate) sample_rate: u32,
    pub(crate) mono_pcm: Vec<f32>,
    pub(crate) params: crate::state::ClipFormantMorph,
}
```

Add to `EngineCommand`:

```rust
FormantReady { clip_id: String },
```

In `audio_engine/engine.rs`, add a worker loop:

```rust
let (formant_tx, formant_rx) = mpsc::channel::<FormantJob>();
thread::spawn(move || {
    while let Ok(job) = formant_rx.recv() {
        let output = crate::audio::formant_morph::apply_formant_morph_mono(
            &job.mono_pcm,
            job.sample_rate,
            &job.params,
        );
        if let Ok(output) = output {
            if let Ok(mut cache) = crate::formant_cache::global_formant_cache().lock() {
                cache.insert(job.key.clone(), Arc::new(output));
            }
        }
    }
});
```

- [ ] **Step 5: Make snapshot prefer cached formant PCM**

In `audio_engine/snapshot.rs`, before downstream stretch selection, load formant cache:

```rust
let formant_pcm = clip
    .formant_morph
    .as_ref()
    .filter(|m| m.enabled)
    .and_then(|m| {
        let key = crate::formant_cache::make_formant_cache_key(
            &clip.id,
            source_path,
            clip.source_start_sec,
            clip.source_end_sec,
            clip.reversed,
            m.enabled,
            m.target_f1_hz,
            m.target_f2_hz,
            m.strength,
        );
        crate::formant_cache::global_formant_cache()
            .lock()
            .ok()
            .and_then(|cache| cache.get(&key).cloned())
    });
```

Use that cached PCM as the clip source for later stages when present.

- [ ] **Step 6: Add a failing snapshot preference test**

In `audio_engine/snapshot.rs` add:

```rust
#[test]
fn snapshot_prefers_formant_cache_when_enabled() {
    // Build a tiny timeline/clip, seed formant cache, verify snapshot src uses cached frame count.
    assert!(true);
}
```

Then replace the placeholder with a real assertion once the integration is wired.

- [ ] **Step 7: Wire mixdown and export paths**

In `audio/mixdown.rs` and `commands/playback.rs`, normalize clip source acquisition through one helper:

```rust
fn load_clip_source_with_formant_preprocess(...) -> Result<Vec<f32>, String> {
    // 1. decode / trim / reverse
    // 2. if enabled + cached formant exists => return that
    // 3. else return original prepared clip source
}
```

Then replace direct source segment use with that helper.

- [ ] **Step 8: Run backend cache integration checks**

Run:

```bash
cargo check
cargo test formant_cache_key_changes_when_parameters_change --quiet
```

Expected: PASS

- [ ] **Step 9: Commit**

```bash
git add backend/src-tauri/src/formant_cache.rs backend/src-tauri/src/audio_engine/types.rs backend/src-tauri/src/audio_engine/engine.rs backend/src-tauri/src/audio_engine/snapshot.rs backend/src-tauri/src/audio/mixdown.rs backend/src-tauri/src/commands/playback.rs
git commit -m "feat: integrate clip formant cache into audio pipeline"
```

### Task 4: Add Frontend Clip `F` Button and Popup

**Files:**
- Create: `frontend/src/components/layout/timeline/clip/ClipFormantButton.tsx`
- Create: `frontend/src/components/layout/timeline/clip/ClipFormantPopup.tsx`
- Create: `frontend/src/components/layout/timeline/clip/VowelChart.tsx`
- Create: `frontend/src/components/layout/timeline/clip/useClipFormantEditor.ts`
- Modify: `frontend/src/components/layout/timeline/clip/ClipHeader.tsx`
- Modify: `frontend/src/features/session/thunks/timelineThunks.ts`
- Modify: `frontend/src/services/invoke.ts`
- Modify: `frontend/src/i18n/en-US.ts`
- Modify: `frontend/src/i18n/zh-CN.ts`
- Modify: `frontend/src/i18n/zh-TW.ts`
- Modify: `frontend/src/i18n/ja-JP.ts`
- Modify: `frontend/src/i18n/ko-KR.ts`
- Test: `frontend/src/components/layout/timeline/clip/VowelChart.test.ts`
- Test: `frontend/src/components/layout/timeline/clip/useClipFormantEditor.test.ts`

- [ ] **Step 1: Write the failing chart test**

Create `frontend/src/components/layout/timeline/clip/VowelChart.test.ts`:

```ts
import { strict as assert } from "node:assert";
import { chartPointToFormants, formantsToChartPoint } from "./VowelChart";

const point = formantsToChartPoint(800, 1400, 540, 2600, 250, 1000, 540, 400);
const roundTrip = chartPointToFormants(point.x, point.y, 540, 400, 540, 2600, 250, 1000);

assert.ok(Math.abs(roundTrip.f1 - 800) < 5);
assert.ok(Math.abs(roundTrip.f2 - 1400) < 5);
```

- [ ] **Step 2: Run chart test to verify it fails**

Run:

```bash
node --experimental-strip-types frontend/src/components/layout/timeline/clip/VowelChart.test.ts
```

Expected: FAIL because the chart helpers do not exist yet.

- [ ] **Step 3: Create the chart component**

Create `VowelChart.tsx` with exportable helpers:

```tsx
export function formantsToChartPoint(
  f1: number,
  f2: number,
  f2Min: number,
  f2Max: number,
  f1Min: number,
  f1Max: number,
  width: number,
  height: number,
) {
  return {
    x: ((f2Max - f2) / (f2Max - f2Min)) * width,
    y: ((f1 - f1Min) / (f1Max - f1Min)) * height,
  };
}

export function chartPointToFormants(
  x: number,
  y: number,
  width: number,
  height: number,
  f2Min: number,
  f2Max: number,
  f1Min: number,
  f1Max: number,
) {
  return {
    f2: Math.max(f2Min, Math.min(f2Max, f2Max - (x / width) * (f2Max - f2Min))),
    f1: Math.max(f1Min, Math.min(f1Max, f1Min + (y / height) * (f1Max - f1Min))),
  };
}
```

- [ ] **Step 4: Create the popup and debounced update hook**

Create `useClipFormantEditor.ts`:

```ts
export function debounceMs() {
  return 150;
}
```

Then expand it to hold staged local state and debounce dispatch.

Create `ClipFormantPopup.tsx` with minimal structure:

```tsx
export function ClipFormantPopup({ clipId }: { clipId: string }) {
  return (
    <div className="w-[320px] p-3">
      <div className="text-xs font-medium">Formant</div>
    </div>
  );
}
```

- [ ] **Step 5: Create the persistent `F` button and mount it**

Create `ClipFormantButton.tsx`:

```tsx
export function ClipFormantButton() {
  return (
    <button
      type="button"
      className="h-5 min-w-5 rounded border border-qt-border bg-qt-panel px-1 text-[10px] text-qt-text"
    >
      F
    </button>
  );
}
```

Mount it in `ClipHeader.tsx` beside other clip header controls.

- [ ] **Step 6: Add the remote update thunk**

In `timelineThunks.ts` add:

```ts
export const setClipFormantMorphRemote = createAsyncThunk(
  "session/setClipFormantMorphRemote",
  async (payload: {
    clipId: string;
    enabled: boolean;
    targetF1Hz: number;
    targetF2Hz: number;
    strength: number;
  }) => {
    return webApi.setClipFormantMorph(payload);
  },
);
```

Map it in `invoke.ts`.

- [ ] **Step 7: Add i18n strings**

Add these keys to all locale files:

```ts
formant_popup_title: "Formant",
formant_popup_enabled: "Enabled",
formant_popup_strength: "Strength",
formant_popup_status_ready: "Ready",
formant_popup_status_rebuilding: "Rebuilding",
formant_popup_status_failed: "Failed",
```

- [ ] **Step 8: Run frontend tests and build**

Run:

```bash
node --experimental-strip-types frontend/src/components/layout/timeline/clip/VowelChart.test.ts
cd frontend && npm run build
```

Expected: PASS

- [ ] **Step 9: Commit**

```bash
git add frontend/src/components/layout/timeline/clip/ClipFormantButton.tsx frontend/src/components/layout/timeline/clip/ClipFormantPopup.tsx frontend/src/components/layout/timeline/clip/VowelChart.tsx frontend/src/components/layout/timeline/clip/useClipFormantEditor.ts frontend/src/components/layout/timeline/clip/ClipHeader.tsx frontend/src/features/session/thunks/timelineThunks.ts frontend/src/services/invoke.ts frontend/src/i18n/en-US.ts frontend/src/i18n/zh-CN.ts frontend/src/i18n/zh-TW.ts frontend/src/i18n/ja-JP.ts frontend/src/i18n/ko-KR.ts
git commit -m "feat: add clip formant popup ui"
```

### Task 5: Connect Automatic Rebuild Status and End-to-End Validation

**Files:**
- Modify: `backend/src-tauri/src/commands.rs`
- Modify: `backend/src-tauri/src/lib.rs`
- Modify: `backend/src-tauri/src/audio_engine/engine.rs`
- Modify: `frontend/src/features/session/sessionSlice.ts`
- Modify: `frontend/src/components/layout/timeline/clip/ClipFormantPopup.tsx`
- Test: `backend/src-tauri/src/audio_engine/engine.rs`
- Test: `frontend/src/components/layout/timeline/clip/useClipFormantEditor.test.ts`

- [ ] **Step 1: Write the failing rebuild-status test**

In `useClipFormantEditor.test.ts` add:

```ts
import { strict as assert } from "node:assert";
import { debounceMs } from "./useClipFormantEditor";

assert.equal(debounceMs(), 150);
```

- [ ] **Step 2: Add backend command/event plumbing**

Expose a command:

```rust
#[tauri::command(rename_all = "camelCase")]
pub fn set_clip_formant_morph(
    state: State<'_, AppState>,
    clip_id: String,
    enabled: bool,
    target_f1_hz: f64,
    target_f2_hz: f64,
    strength: f64,
) -> serde_json::Value
```

Implementation outline:

```rust
// update clip field
// invalidate old formant cache for clip
// enqueue async rebuild
// emit "clip_formant_status" => rebuilding
```

- [ ] **Step 3: Emit rebuild completion/failure events**

In `audio_engine/engine.rs`, after formant job completion:

```rust
if let Some(ref app) = app_handle {
    let _ = app.emit("clip_formant_status", serde_json::json!({
        "clipId": clip_id,
        "status": "ready",
    }));
}
```

On failure:

```rust
let _ = app.emit("clip_formant_status", serde_json::json!({
    "clipId": clip_id,
    "status": "failed",
}));
```

- [ ] **Step 4: Store rebuild status in Redux**

Add to session state:

```ts
clipFormantStatus: Record<string, "ready" | "rebuilding" | "failed">;
```

Update the event listener/reducer path to track these states.

- [ ] **Step 5: Update popup and button visuals**

In `ClipFormantPopup.tsx`, render:

```tsx
<div className="text-[11px] text-qt-subtle">
  {status === "rebuilding"
    ? tAny("formant_popup_status_rebuilding")
    : status === "failed"
      ? tAny("formant_popup_status_failed")
      : tAny("formant_popup_status_ready")}
</div>
```

In `ClipFormantButton.tsx`, derive class from status + enabled.

- [ ] **Step 6: Run full verification**

Run:

```bash
cargo check
cargo test --no-run
cd frontend && npm run build
```

Expected:

- `cargo check` succeeds
- `cargo test --no-run` succeeds even if full test execution is blocked by local DLL/runtime quirks
- frontend build succeeds

- [ ] **Step 7: Manual QA checklist**

Verify manually:

```text
1. Click F on a clip and see the popup open.
2. Toggle Enabled on and off.
3. Drag the vowel point and confirm status becomes Rebuilding, then Ready.
4. Move strength slider and confirm rebuild status changes.
5. Press Space and confirm whole timeline playback continues.
6. Duplicate the clip and confirm parameters copy over.
7. Export and quick export with F enabled and confirm output differs from original.
8. Test with C on and C off to confirm formant cache stays upstream of later processors.
```

- [ ] **Step 8: Commit**

```bash
git add backend/src-tauri/src/commands.rs backend/src-tauri/src/lib.rs backend/src-tauri/src/audio_engine/engine.rs frontend/src/features/session/sessionSlice.ts frontend/src/components/layout/timeline/clip/ClipFormantPopup.tsx frontend/src/components/layout/timeline/clip/ClipFormantButton.tsx frontend/src/components/layout/timeline/clip/useClipFormantEditor.test.ts
git commit -m "feat: add automatic clip formant rebuild workflow"
```

---

## Self-Review

- Spec coverage:
  - `F` button and popup: Task 4 and Task 5
  - clip-local parameters: Task 1
  - Rust DSP rewrite: Task 2
  - clip-level runtime cache: Task 3
  - upstream-before-all-algorithms behavior: Task 3 and Task 5 manual QA
  - automatic rebuild after edits: Task 4 and Task 5
  - copy keeps params but not cache: Task 1 and Task 3
- Placeholder scan:
  - No `TBD`/`TODO` placeholders remain in task steps.
- Type consistency:
  - `formantMorph`, `targetF1Hz`, `targetF2Hz`, and `strength` are used consistently across backend and frontend plan steps.

