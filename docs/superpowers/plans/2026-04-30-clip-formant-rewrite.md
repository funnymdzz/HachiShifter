# Clip Formant Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the clip-level formant DSP to follow `vowel_synth.py` closely while preserving the existing clip-level playback, cache, and export integration.

**Architecture:** Keep the current clip-level `formant_morph` state model and all backend call sites unchanged, and replace only the DSP core in `backend/src-tauri/src/audio/formant_morph.rs`. Drive the rewrite with TDD so the new implementation proves audible change, stronger-parameter scaling, and stable mono/stereo output before the rest of the engine consumes it.

**Tech Stack:** Rust, Tauri backend, existing audio engine/mixdown pipeline, Rust unit tests

---

## File Map

- Modify: `backend/src-tauri/src/audio/formant_morph.rs`
  - Replace the unstable DSP core with a direct `vowel_synth.py`-style LPC formant morph path.
  - Keep the public entry points `apply_formant_morph_mono()` and `apply_formant_morph_interleaved()`.
- Verify only: `backend/src-tauri/src/formant_cache.rs`
  - Ensure existing cache callers continue to use the shared DSP entry points without interface changes.
- Verify only: `backend/src-tauri/src/audio_engine/snapshot.rs`
  - Ensure timeline playback continues to consume the shared DSP result.
- Verify only: `backend/src-tauri/src/audio/mixdown.rs`
  - Ensure export continues to consume the shared DSP result.

### Task 1: Strength-Based Audible Change Regression

**Files:**
- Modify: `backend/src-tauri/src/audio/formant_morph.rs`
- Test: `backend/src-tauri/src/audio/formant_morph.rs`

- [ ] **Step 1: Write the failing test**

Add this test inside `backend/src-tauri/src/audio/formant_morph.rs`:

```rust
#[test]
fn enabled_formant_morph_changes_voiced_signal_and_strength_scales_effect() {
    let input: Vec<f32> = (0..16_000)
        .map(|idx| {
            let t = idx as f32 / 16_000.0;
            ((2.0 * std::f32::consts::PI * 160.0 * t).sin()
                + 0.65 * (2.0 * std::f32::consts::PI * 730.0 * t).sin()
                + 0.30 * (2.0 * std::f32::consts::PI * 1_240.0 * t).sin())
                * 0.16
        })
        .collect();

    let weak = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 900.0,
        target_f2_hz: 800.0,
        strength: 0.20,
    };
    let strong = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 900.0,
        target_f2_hz: 800.0,
        strength: 0.95,
    };

    let weak_out = apply_formant_morph_mono(&input, 16_000, &weak).unwrap();
    let strong_out = apply_formant_morph_mono(&input, 16_000, &strong).unwrap();

    let weak_diff = average_abs_diff(&input, &weak_out);
    let strong_diff = average_abs_diff(&input, &strong_out);

    assert!(weak_diff > 0.003, "weak morph diff too small: {weak_diff}");
    assert!(
        strong_diff > weak_diff * 1.15,
        "expected stronger morph to differ more; weak={weak_diff} strong={strong_diff}"
    );
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cargo test enabled_formant_morph_changes_voiced_signal_and_strength_scales_effect --quiet
```

Expected: FAIL because the current DSP often falls back or produces too little change for the stronger-vs-weaker threshold.

- [ ] **Step 3: Write minimal implementation**

Replace the main DSP internals in `backend/src-tauri/src/audio/formant_morph.rs` with code shaped like this:

```rust
pub fn apply_formant_morph_mono(
    input: &[f32],
    sample_rate: u32,
    params: &ClipFormantMorph,
) -> Result<Vec<f32>, String> {
    if !params.enabled || input.is_empty() {
        return Ok(input.to_vec());
    }

    let strength = params.strength.clamp(0.0, 1.0) as f32;
    if sample_rate < 8_000 || input.len() < 512 || strength <= 1.0e-5 {
        return Ok(input.to_vec());
    }

    let target_f1 = params.target_f1_hz.clamp(250.0, 1000.0) as f32;
    let target_f2 = params.target_f2_hz.clamp(540.0, 2600.0) as f32;
    let frame_len = ((sample_rate as f32) * 0.025).round().max(128.0) as usize;
    let hop_len = (frame_len / 4).max(64);
    let order = ((sample_rate / 1_000) as usize + 6).clamp(8, 24);
    let window = hann_window(frame_len);
    let emphasized = pre_emphasis(input, 0.97);
    let padded = pad_for_overlap_add(&emphasized, frame_len, hop_len);

    let mut overlap = vec![0.0f32; padded.len()];
    let mut window_sum = vec![0.0f32; padded.len()];
    let mut processed_frames = 0usize;

    for start in (0..=padded.len() - frame_len).step_by(hop_len) {
        let frame = &padded[start..start + frame_len];
        let passthrough: Vec<f32> = frame
            .iter()
            .zip(window.iter())
            .map(|(sample, win)| sample * win * win)
            .collect();
        let windowed: Vec<f32> = frame
            .iter()
            .zip(window.iter())
            .map(|(sample, win)| sample * win)
            .collect();

        let mut processed = if frame_energy(&windowed) < 1.0e-6 {
            passthrough.clone()
        } else if let Some(a_orig) = lpc_coefficients(&windowed, order) {
            if let Some(a_target) =
                move_formants_like_reference(&a_orig, sample_rate, target_f1, target_f2, strength)
            {
                let residual = fir_filter(&windowed, &a_orig);
                let mut frame_synth = all_pole_filter(&residual, &a_target);
                for (sample, win) in frame_synth.iter_mut().zip(window.iter()) {
                    *sample *= *win;
                }
                match_energy(&windowed, &mut frame_synth);
                processed_frames += 1;
                frame_synth
            } else {
                passthrough.clone()
            }
        } else {
            passthrough.clone()
        };

        match_energy(&windowed, &mut processed);
        for idx in 0..frame_len {
            overlap[start + idx] += processed[idx];
            window_sum[start + idx] += window[idx] * window[idx];
        }
    }

    if processed_frames == 0 {
        return Ok(input.to_vec());
    }

    normalize_overlap_add(&mut overlap, &window_sum);
    let mut out = de_emphasis(&overlap[..input.len()], 0.97);
    peak_protect(&mut out, input);
    Ok(out)
}
```

Supporting helpers in the same file should:

- pad the emphasized buffer for frame processing
- compute LPC coefficients
- extract the first two valid positive-frequency roots
- move those two formants and tighten their bandwidths the same way as `vowel_synth.py`
- rebuild the all-pole filter coefficients from the modified roots

The root-handling helper should remove the current QR-based root solver from the main path and use a simpler, reference-aligned implementation instead of keeping the present solver architecture.

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cargo test enabled_formant_morph_changes_voiced_signal_and_strength_scales_effect --quiet
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/audio/formant_morph.rs
git commit -m "fix: rewrite clip formant dsp core"
```

### Task 2: Preserve Existing DSP Safety Guarantees

**Files:**
- Modify: `backend/src-tauri/src/audio/formant_morph.rs`
- Test: `backend/src-tauri/src/audio/formant_morph.rs`

- [ ] **Step 1: Write the failing test**

Keep or add these tests in `backend/src-tauri/src/audio/formant_morph.rs`:

```rust
#[test]
fn disabled_formant_morph_returns_input_unchanged() {
    let input = vec![0.0f32, 0.1, -0.1, 0.2, -0.2, 0.0];
    let params = ClipFormantMorph::default();
    let output = apply_formant_morph_mono(&input, 16_000, &params).unwrap();
    assert_eq!(output, input);
}

#[test]
fn enabled_formant_morph_preserves_length_and_finiteness() {
    let input: Vec<f32> = (0..4_096)
        .map(|idx| {
            let t = idx as f32 / 16_000.0;
            ((2.0 * std::f32::consts::PI * 220.0 * t).sin()
                + 0.4 * (2.0 * std::f32::consts::PI * 660.0 * t).sin())
                * 0.15
        })
        .collect();
    let params = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 700.0,
        target_f2_hz: 1_800.0,
        strength: 0.75,
    };
    let output = apply_formant_morph_mono(&input, 16_000, &params).unwrap();
    assert_eq!(output.len(), input.len());
    assert!(output.iter().all(|sample| sample.is_finite()));
}

#[test]
fn stereo_formant_morph_preserves_interleaved_shape() {
    let mono: Vec<f32> = (0..1_024)
        .map(|idx| ((idx as f32) * 0.03).sin() * 0.1)
        .collect();
    let mut stereo = Vec::with_capacity(mono.len() * 2);
    for sample in mono {
        stereo.push(sample);
        stereo.push(sample * 0.8);
    }
    let params = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 900.0,
        target_f2_hz: 1_400.0,
        strength: 0.5,
    };
    let output = apply_formant_morph_interleaved(&stereo, 16_000, 2, &params).unwrap();
    assert_eq!(output.len(), stereo.len());
    assert!(output.iter().all(|sample| sample.is_finite()));
}
```

- [ ] **Step 2: Run tests to verify current failures or regressions**

Run:

```bash
cargo test disabled_formant_morph_returns_input_unchanged enabled_formant_morph_preserves_length_and_finiteness stereo_formant_morph_preserves_interleaved_shape --quiet
```

Expected: At least one test may fail during the rewrite until helper behavior is restored.

- [ ] **Step 3: Write minimal implementation**

Make the rewritten helpers preserve these invariants:

```rust
pub fn apply_formant_morph_interleaved(
    input: &[f32],
    sample_rate: u32,
    channels: usize,
    params: &ClipFormantMorph,
) -> Result<Vec<f32>, String> {
    if channels == 0 {
        return Err("channels == 0".to_string());
    }
    if channels == 1 {
        return apply_formant_morph_mono(input, sample_rate, params);
    }
    if input.is_empty() || !params.enabled {
        return Ok(input.to_vec());
    }

    let frames = input.len() / channels;
    if frames == 0 {
        return Ok(input.to_vec());
    }

    let mono = average_channels_to_mono(input, channels, frames);
    let processed_mono = apply_formant_morph_mono(&mono, sample_rate, params)?;
    Ok(apply_mono_delta_to_interleaved(
        input,
        channels,
        &mono,
        &processed_mono,
    ))
}
```

All new helpers must keep sample counts unchanged and clamp output samples back into `[-1.0, 1.0]`.

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
cargo test disabled_formant_morph_returns_input_unchanged enabled_formant_morph_preserves_length_and_finiteness stereo_formant_morph_preserves_interleaved_shape --quiet
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/audio/formant_morph.rs
git commit -m "test: preserve clip formant dsp safety guarantees"
```

### Task 3: Verify Shared Playback and Export Integration

**Files:**
- Verify only: `backend/src-tauri/src/formant_cache.rs`
- Verify only: `backend/src-tauri/src/audio_engine/snapshot.rs`
- Verify only: `backend/src-tauri/src/audio/mixdown.rs`

- [ ] **Step 1: Inspect shared DSP call sites**

Confirm these call sites still route through the same shared entry points:

```rust
crate::formant_morph::apply_formant_morph_interleaved(&segment, out_rate, 2, params)?;
```

and:

```rust
match crate::formant_cache::get_or_compute_formant_audio(key, &segment, out_rate, params)
```

The relevant files are:

- `backend/src-tauri/src/formant_cache.rs`
- `backend/src-tauri/src/audio_engine/snapshot.rs`
- `backend/src-tauri/src/audio/mixdown.rs`

- [ ] **Step 2: Run focused DSP test suite**

Run:

```bash
cargo test formant_morph --quiet
```

Expected: PASS for the formant DSP unit tests.

- [ ] **Step 3: Run backend compile-oriented verification**

Run:

```bash
cargo test clip_formant_morph_defaults_to_disabled_when_missing --quiet
```

Expected: PASS, proving the backend crate still compiles with the rewritten DSP module linked into the existing state model.

- [ ] **Step 4: Commit**

```bash
git add backend/src-tauri/src/audio/formant_morph.rs
git commit -m "test: verify clip formant rewrite integration"
```
