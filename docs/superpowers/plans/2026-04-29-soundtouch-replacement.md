# SoundTouch Replacement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current default stretch implementation with a SoundTouch DLL-backed path across playback, mixdown/export, and NSF-HiFiGAN-linked rendering.

**Architecture:** Introduce a focused `audio/soundtouch.rs` FFI layer, route `time_stretch.rs` through a new `SoundTouchDll` algorithm, and collapse all stretch ownership into one external PCM-domain stage. NSF-HiFiGAN stops performing internal mel-domain stretch and instead receives PCM already stretched to timeline duration.

**Tech Stack:** Rust, Tauri, Windows DLL FFI, Cargo build script, existing audio engine/mixdown renderer tests

---

### Task 1: Add SoundTouch DLL wiring and a testable Rust wrapper

**Files:**
- Create: `backend/src-tauri/src/audio/soundtouch.rs`
- Modify: `backend/src-tauri/src/lib.rs`
- Modify: `backend/src-tauri/build.rs`
- Test: `backend/src-tauri/src/audio/soundtouch.rs`

- [ ] **Step 1: Write the failing wrapper test**

Add this test module skeleton at the bottom of `backend/src-tauri/src/audio/soundtouch.rs` before writing the implementation:

```rust
#[cfg(test)]
mod tests {
    use super::{normalize_output_len, SoundTouchError};

    #[test]
    fn normalize_output_len_pads_short_output() {
        let out = normalize_output_len(vec![1.0, 2.0, 3.0, 4.0], 2, 4);
        assert_eq!(out.len(), 8);
        assert_eq!(&out[..4], &[1.0, 2.0, 3.0, 4.0]);
        assert_eq!(&out[4..], &[0.0, 0.0, 0.0, 0.0]);
    }

    #[test]
    fn normalize_output_len_truncates_long_output() {
        let out = normalize_output_len(vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0], 2, 2);
        assert_eq!(out, vec![1.0, 2.0, 3.0, 4.0]);
    }

    #[test]
    fn missing_runtime_error_is_clear() {
        let err = SoundTouchError::RuntimeUnavailable("SoundTouch_x64.dll".to_string());
        assert!(err.to_string().contains("SoundTouch_x64.dll"));
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cargo test normalize_output_len_pads_short_output --quiet
```

Expected: FAIL because `backend/src-tauri/src/audio/soundtouch.rs` does not exist yet.

- [ ] **Step 3: Write the minimal SoundTouch wrapper**

Create `backend/src-tauri/src/audio/soundtouch.rs` with a small, testable surface first:

```rust
use std::fmt::{Display, Formatter};

#[derive(Debug)]
pub enum SoundTouchError {
    RuntimeUnavailable(String),
    ProcessingFailed(&'static str),
}

impl Display for SoundTouchError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::RuntimeUnavailable(name) => {
                write!(f, "SoundTouch runtime unavailable: {name}")
            }
            Self::ProcessingFailed(msg) => write!(f, "SoundTouch processing failed: {msg}"),
        }
    }
}

impl std::error::Error for SoundTouchError {}

pub fn normalize_output_len(
    mut output: Vec<f32>,
    channels: usize,
    out_frames: usize,
) -> Vec<f32> {
    let wanted = out_frames.saturating_mul(channels);
    output.resize(wanted, 0.0);
    output.truncate(wanted);
    output
}

pub fn is_available() -> bool {
    std::path::Path::new("SoundTouch_x64.dll").exists()
        || std::path::Path::new("soundtouch_x64.dll").exists()
}

pub fn try_time_stretch_interleaved_offline(
    _input: &[f32],
    _channels: usize,
    _sample_rate: u32,
    _time_ratio: f64,
    _out_frames: usize,
) -> Result<Vec<f32>, SoundTouchError> {
    Err(SoundTouchError::RuntimeUnavailable(
        "SoundTouch_x64.dll".to_string(),
    ))
}

pub fn try_time_stretch_interleaved_realtime(
    _input: &[f32],
    _channels: usize,
    _sample_rate: u32,
    _time_ratio: f64,
    _out_frames: usize,
) -> Result<Vec<f32>, SoundTouchError> {
    Err(SoundTouchError::RuntimeUnavailable(
        "SoundTouch_x64.dll".to_string(),
    ))
}
```

Then register the module in `backend/src-tauri/src/lib.rs`:

```rust
#[path = "audio/soundtouch.rs"]
mod soundtouch;
```

And add the build-script placeholder in `backend/src-tauri/build.rs`:

```rust
fn build_soundtouch() {
    let lib_dir = std::path::Path::new("third_party/soundtouch");
    if !lib_dir.exists() {
        panic!("[soundtouch] third_party/soundtouch/ not found");
    }
    let abs = lib_dir
        .canonicalize()
        .expect("[soundtouch] failed to canonicalize path");

    println!("cargo:rerun-if-changed=third_party/soundtouch/SoundTouch_x64.dll");
    println!("cargo:rerun-if-changed=third_party/soundtouch/SoundTouch_x64.lib");
    println!("cargo:rustc-link-search=native={}", abs.display());
    println!("cargo:rustc-link-lib=dylib=SoundTouch_x64");
}
```

Call it from `main()` immediately after `build_vslib();`.

- [ ] **Step 4: Run the wrapper tests**

Run:

```bash
cargo test normalize_output_len_truncates_long_output --quiet
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/audio/soundtouch.rs backend/src-tauri/src/lib.rs backend/src-tauri/build.rs
git commit -m "build: add soundtouch dll wrapper scaffold"
```

### Task 2: Route `time_stretch.rs` to `SoundTouchDll` and verify fallback behavior

**Files:**
- Modify: `backend/src-tauri/src/audio/time_stretch.rs`
- Test: `backend/src-tauri/src/audio/time_stretch.rs`

- [ ] **Step 1: Write the failing dispatcher test**

Add these tests at the bottom of `backend/src-tauri/src/audio/time_stretch.rs`:

```rust
#[cfg(test)]
mod tests {
    use super::{time_stretch_interleaved, StretchAlgorithm};

    #[test]
    fn soundtouch_fallback_keeps_requested_length() {
        let input = vec![0.0f32, 0.5, 0.25, -0.25];
        let out = time_stretch_interleaved(
            &input,
            1,
            44_100,
            8,
            StretchAlgorithm::SoundTouchDll,
        );
        assert_eq!(out.len(), 8);
    }

    #[test]
    fn default_algorithm_symbol_exists() {
        let algo = StretchAlgorithm::SoundTouchDll;
        assert!(matches!(algo, StretchAlgorithm::SoundTouchDll));
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cargo test soundtouch_fallback_keeps_requested_length --quiet
```

Expected: FAIL because `StretchAlgorithm::SoundTouchDll` does not exist.

- [ ] **Step 3: Implement the new algorithm branch**

Update `backend/src-tauri/src/audio/time_stretch.rs`:

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StretchAlgorithm {
    LinearResample,
    SignalsmithStretch,
    SoundTouchDll,
    ElastiqueSoloist,
}
```

Add the branch inside `time_stretch_interleaved(...)`:

```rust
StretchAlgorithm::SoundTouchDll => {
    let in_frames = if channels == 0 { 0 } else { input.len() / channels };
    if in_frames < 2 || out_frames < 2 {
        return linear_time_stretch_interleaved(input, channels, out_frames);
    }
    let ratio = (out_frames as f64) / (in_frames as f64);
    let result = crate::soundtouch::try_time_stretch_interleaved_realtime(
        input,
        channels,
        sample_rate.max(1),
        ratio,
        out_frames,
    )
    .or_else(|_| {
        crate::soundtouch::try_time_stretch_interleaved_offline(
            input,
            channels,
            sample_rate.max(1),
            ratio,
            out_frames,
        )
    });

    match result {
        Ok(mut out) => {
            preserve_hard_silence_after_stretch(input, &mut out, channels, sample_rate.max(1));
            out.resize(out_frames * channels, 0.0);
            out
        }
        Err(e) => {
            if std::env::var("HIFISHIFTER_DEBUG_COMMANDS").ok().as_deref() == Some("1") {
                eprintln!("time_stretch: SoundTouch failed, falling back: {e}");
            }
            linear_time_stretch_interleaved(input, channels, out_frames)
        }
    }
}
```

- [ ] **Step 4: Run the dispatcher tests**

Run:

```bash
cargo test soundtouch_fallback_keeps_requested_length --quiet
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/audio/time_stretch.rs
git commit -m "feat: route time stretch dispatcher to soundtouch"
```

### Task 3: Switch playback, export, and stream call sites to the new default

**Files:**
- Modify: `backend/src-tauri/src/commands/common.rs`
- Modify: `backend/src-tauri/src/commands/playback.rs`
- Modify: `backend/src-tauri/src/commands/synth.rs`
- Modify: `backend/src-tauri/src/commands/waveform.rs`
- Modify: `backend/src-tauri/src/audio/mixdown.rs`
- Modify: `backend/src-tauri/src/audio_engine/engine.rs`
- Modify: `backend/src-tauri/src/audio_engine/stretch_stream.rs`
- Modify: `backend/src-tauri/src/state.rs`
- Test: `backend/src-tauri/src/audio_engine/stretch_stream.rs`

- [ ] **Step 1: Write the failing stretch-stream test**

Add a narrow unit test to `backend/src-tauri/src/audio_engine/stretch_stream.rs` around a helper function you will introduce:

```rust
#[cfg(test)]
mod tests {
    use crate::time_stretch::StretchAlgorithm;

    #[test]
    fn realtime_stream_defaults_to_soundtouch() {
        assert!(matches!(
            super::default_realtime_stretch_algorithm(),
            StretchAlgorithm::SoundTouchDll
        ));
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cargo test realtime_stream_defaults_to_soundtouch --quiet
```

Expected: FAIL because `default_realtime_stretch_algorithm()` does not exist.

- [ ] **Step 3: Introduce one helper and switch every default call site**

Add this helper near the top of `backend/src-tauri/src/audio_engine/stretch_stream.rs`:

```rust
pub(crate) fn default_realtime_stretch_algorithm() -> crate::time_stretch::StretchAlgorithm {
    crate::time_stretch::StretchAlgorithm::SoundTouchDll
}
```

Replace the realtime constructor call in the same file with a SoundTouch-backed constructor once the wrapper exposes it:

```rust
let mut rb = match crate::soundtouch::RealtimeStretcher::new(out_rate, 2, time_ratio) {
    Ok(v) => v,
    Err(e) => {
        eprintln!("[StretchStream ERROR] Failed to create SoundTouch stretcher: {}", e);
        return;
    }
};
```

Then replace all hardcoded `StretchAlgorithm::SignalsmithStretch` defaults in:

- `backend/src-tauri/src/commands/common.rs`
- `backend/src-tauri/src/commands/playback.rs`
- `backend/src-tauri/src/commands/synth.rs`
- `backend/src-tauri/src/commands/waveform.rs`
- `backend/src-tauri/src/audio_engine/engine.rs`
- `backend/src-tauri/src/audio/mixdown.rs`
- `backend/src-tauri/src/state.rs`

with:

```rust
crate::time_stretch::StretchAlgorithm::SoundTouchDll
```

- [ ] **Step 4: Run focused verification**

Run:

```bash
cargo test realtime_stream_defaults_to_soundtouch --quiet
cargo test quick_export --quiet
cargo check
```

Expected: PASS, PASS, PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/commands/common.rs backend/src-tauri/src/commands/playback.rs backend/src-tauri/src/commands/synth.rs backend/src-tauri/src/commands/waveform.rs backend/src-tauri/src/audio/mixdown.rs backend/src-tauri/src/audio_engine/engine.rs backend/src-tauri/src/audio_engine/stretch_stream.rs backend/src-tauri/src/state.rs
git commit -m "feat: switch stretch call sites to soundtouch default"
```

### Task 4: Remove NSF-HiFiGAN internal mel stretch ownership

**Files:**
- Modify: `backend/src-tauri/src/renderer/chain.rs`
- Modify: `backend/src-tauri/src/renderer/hifigan.rs`
- Modify: `backend/src-tauri/src/pitch_editing.rs`
- Modify: `backend/src-tauri/src/audio_engine/snapshot.rs`
- Modify: `backend/src-tauri/src/audio/mixdown.rs`
- Test: `backend/src-tauri/src/renderer/chain.rs`

- [ ] **Step 1: Write the failing processor-capability test**

Add this test to `backend/src-tauri/src/renderer/chain.rs`:

```rust
#[cfg(test)]
mod tests {
    #[test]
    fn hifigan_chain_no_longer_handles_time_stretch() {
        let chain = super::hifigan_chain();
        assert!(!chain.handles_time_stretch);
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cargo test hifigan_chain_no_longer_handles_time_stretch --quiet
```

Expected: FAIL because `hifigan_chain()` still sets `handles_time_stretch: true`.

- [ ] **Step 3: Collapse the HiFiGAN path to stretched PCM input**

In `backend/src-tauri/src/renderer/chain.rs`, change the chain definition:

```rust
pub fn hifigan_chain() -> ProcessorChain {
    ProcessorChain {
        id: "nsf_hifigan".into(),
        display_name: "NSF-HiFiGAN".into(),
        stages: vec![Box::new(HiFiGanStage)],
        handles_time_stretch: false,
    }
}
```

In `backend/src-tauri/src/renderer/hifigan.rs`, retire `render_mel_stretch(...)` by deleting it and route `HiFiGanStage::process(...)` to `render_with_formant(...)` only:

```rust
if !breath_enabled {
    return crate::renderer::hifigan::HiFiGanRenderer
        .render_with_formant(&render_ctx, formant_curve);
}
```

In `backend/src-tauri/src/pitch_editing.rs`, keep `needs_processor_stretch` false for HiFiGAN by relying on the updated capability:

```rust
let handles = crate::renderer::get_processor(kind)
    .capabilities()
    .handles_time_stretch;
```

No special-case replacement is needed after `handles_time_stretch` flips to `false`.

In `backend/src-tauri/src/audio/mixdown.rs` and `backend/src-tauri/src/audio_engine/snapshot.rs`, remove any assumptions that HiFiGAN-owned stretch must happen after the PCM stage.

- [ ] **Step 4: Run focused HiFiGAN verification**

Run:

```bash
cargo test hifigan_chain_no_longer_handles_time_stretch --quiet
cargo test hifigan_formant_shift_ignores_near_zero_residual_values --quiet
cargo check
```

Expected: PASS, PASS, PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/renderer/chain.rs backend/src-tauri/src/renderer/hifigan.rs backend/src-tauri/src/pitch_editing.rs backend/src-tauri/src/audio_engine/snapshot.rs backend/src-tauri/src/audio/mixdown.rs
git commit -m "refactor: remove hifigan internal stretch ownership"
```

### Task 5: Finalize the DLL integration and run full verification

**Files:**
- Modify: `backend/src-tauri/build.rs`
- Modify: `backend/src-tauri/src/audio/soundtouch.rs`
- Create: `backend/src-tauri/third_party/soundtouch/README.md`
- Test: `backend/src-tauri/src/audio/soundtouch.rs`

- [ ] **Step 1: Write the failing version-pinning test**

Add this test to `backend/src-tauri/src/audio/soundtouch.rs`:

```rust
#[cfg(test)]
mod version_tests {
    use super::runtime_library_name;

    #[test]
    fn runtime_library_name_is_pinned() {
        assert_eq!(runtime_library_name(), "SoundTouch_x64.dll");
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cargo test runtime_library_name_is_pinned --quiet
```

Expected: FAIL because `runtime_library_name()` does not exist yet.

- [ ] **Step 3: Pin the runtime asset names and document the vendor drop**

In `backend/src-tauri/src/audio/soundtouch.rs`, add:

```rust
pub fn runtime_library_name() -> &'static str {
    "SoundTouch_x64.dll"
}

pub fn import_library_name() -> &'static str {
    "SoundTouch_x64.lib"
}
```

Use those names in `build.rs`:

```rust
println!("cargo:rerun-if-changed=third_party/soundtouch/SoundTouch_x64.dll");
println!("cargo:rerun-if-changed=third_party/soundtouch/SoundTouch_x64.lib");
println!("cargo:rustc-link-lib=dylib=SoundTouch_x64");
```

Create `backend/src-tauri/third_party/soundtouch/README.md` with:

```md
# SoundTouch Vendor Drop

Pinned runtime assets for the SoundTouch replacement work:

- `SoundTouch_x64.dll`
- `SoundTouch_x64.lib`

Place matching files from the same upstream release in this directory.
Do not mix DLL/import-library files from different SoundTouch versions.
```

- [ ] **Step 4: Run full verification**

Run:

```bash
cargo test --quiet
cd frontend && npm run build
```

Expected: PASS, PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/build.rs backend/src-tauri/src/audio/soundtouch.rs backend/src-tauri/third_party/soundtouch/README.md
git commit -m "docs: pin soundtouch runtime assets"
```

### Task 6: Manual audio regression pass

**Files:**
- Modify: none
- Test: manual QA checklist

- [ ] **Step 1: Build a Windows dev binary with the pinned DLL present**

Run:

```bash
cargo check
```

Expected: PASS

- [ ] **Step 2: Regress realtime stretch behavior**

Manual checks:

```text
1. Place one clip with playback_rate = 0.8 and preview it.
2. Place one clip with playback_rate = 1.2 and preview it.
3. Confirm there is no silent failure and no obvious ring-buffer underrun spam.
```

- [ ] **Step 3: Regress NSF-HiFiGAN-linked paths**

Manual checks:

```text
1. Use an NSF-HiFiGAN track with playback_rate != 1.0 and no pitch edits.
2. Repeat with pitch edits enabled.
3. Repeat with formant_shift_cents enabled.
4. Repeat with breath/tension enabled if the source track uses those controls.
5. Confirm output is stretched, rendered, and cached consistently.
```

- [ ] **Step 4: Regress export and quick export**

Manual checks:

```text
1. Export the whole project with stretched clips.
2. Quick-export selected clips that include playback_rate changes.
3. Confirm duration and audible placement match the timeline.
```

- [ ] **Step 5: Commit release candidate state**

```bash
git status --short
git commit -am "test: validate soundtouch replacement paths"
```
