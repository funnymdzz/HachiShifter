# Clip Formant Follow-Up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the clip formant floating window interaction bugs and improve the DSP so it stays strong on voiced material while clipping and harsh artifacts are reduced.

**Architecture:** Keep the existing clip formant UI and backend call surfaces, but tighten the floating window's event and focus behavior and add selective protection inside `backend/src-tauri/src/audio/formant_morph.rs`. Frontend fixes should route through the current timeline/keybinding focus model, and backend fixes should remain internal to the shared DSP entry points already used by cache rebuild, playback, and export.

**Tech Stack:** React 19, TypeScript, Redux, lightweight `node --experimental-strip-types` frontend tests, Rust, Tauri backend unit tests

---

## File Map

- Modify: `frontend/src/components/layout/timeline/clip/ClipFormantToolWindow.tsx`
  - Stop event leakage, mark focus state, and harden local keyboard behavior.
- Modify: `frontend/src/components/layout/timeline/clip/VowelChart.tsx`
  - Prevent text selection and export a small helper for drag-guard tests if needed.
- Modify: `frontend/src/features/keybindings/useKeybindings.ts`
  - Suppress global `Space` playback handling while the clip formant tool is active.
- Create: `frontend/src/components/layout/timeline/clip/clipFormantInteractionGuards.test.ts`
  - Cover focus marker and keyboard gating helpers without a browser test framework.
- Modify: `backend/src-tauri/src/audio/formant_morph.rs`
  - Add suitability weighting, stronger frame protection, and soft limiting while preserving strong voiced-material morphing.

### Task 1: Frontend Interaction Isolation

**Files:**
- Modify: `frontend/src/components/layout/timeline/clip/ClipFormantToolWindow.tsx`
- Modify: `frontend/src/components/layout/timeline/clip/VowelChart.tsx`
- Modify: `frontend/src/features/keybindings/useKeybindings.ts`
- Create: `frontend/src/components/layout/timeline/clip/clipFormantInteractionGuards.test.ts`

- [ ] **Step 1: Write the failing test**

Create `frontend/src/components/layout/timeline/clip/clipFormantInteractionGuards.test.ts`:

```ts
import { shouldBlockPlaybackToggleForFormantWindow } from "../../../../features/keybindings/useKeybindings";
import { formantChartPointerDownShouldPreventDefault } from "./VowelChart";

function assert(condition: unknown, message: string): void {
    if (!condition) throw new Error(message);
}

assert(
    shouldBlockPlaybackToggleForFormantWindow({
        key: "space",
        formantToolActive: true,
        focusWindow: "clipFormant",
    }),
    "space should be blocked while the formant tool is active",
);

assert(
    !shouldBlockPlaybackToggleForFormantWindow({
        key: "enter",
        formantToolActive: true,
        focusWindow: "clipFormant",
    }),
    "non-space keys should not be blocked by the formant tool guard",
);

assert(
    formantChartPointerDownShouldPreventDefault({ disabled: false }),
    "enabled vowel chart drags should prevent default selection behavior",
);

assert(
    !formantChartPointerDownShouldPreventDefault({ disabled: true }),
    "disabled vowel chart should not claim drag prevention",
);

console.log("clip formant interaction guard checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
node --experimental-strip-types frontend/src/components/layout/timeline/clip/clipFormantInteractionGuards.test.ts
```

Expected: FAIL because the helper exports do not exist yet.

- [ ] **Step 3: Write minimal implementation**

Update `frontend/src/features/keybindings/useKeybindings.ts` to export a helper shaped like:

```ts
export function shouldBlockPlaybackToggleForFormantWindow(params: {
    key: string;
    formantToolActive: boolean;
    focusWindow: string | null;
}): boolean {
    return (
        params.formantToolActive &&
        params.focusWindow === "clipFormant" &&
        params.key === "space"
    );
}
```

Use it inside the global `onKeyDown` path before invoking `playback.toggle`.

Update `frontend/src/components/layout/timeline/clip/VowelChart.tsx` to export:

```ts
export function formantChartPointerDownShouldPreventDefault(params: {
    disabled: boolean;
}): boolean {
    return !params.disabled;
}
```

Then apply it in `onPointerDown`:

```ts
onPointerDown={(event) => {
    if (!formantChartPointerDownShouldPreventDefault({ disabled })) return;
    event.preventDefault();
    event.stopPropagation();
    draggingRef.current = true;
    updateFromPointer(event.clientX, event.clientY);
}}
```

Update `frontend/src/components/layout/timeline/clip/ClipFormantToolWindow.tsx` to:

- set `document.body.setAttribute("data-hs-focus-window", "clipFormant")` while active
- restore timeline focus marker on cleanup if this window owns the marker
- stop propagation for `pointerdown`, `click`, `doubleclick`, `contextmenu`, and `keydown`
- set `tabIndex={0}` and `style={{ userSelect: "none" }}` on the window root

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
node --experimental-strip-types frontend/src/components/layout/timeline/clip/clipFormantInteractionGuards.test.ts
node --experimental-strip-types frontend/src/components/layout/timeline/clip/VowelChart.test.ts
node --experimental-strip-types frontend/src/features/session/sessionSlice.formantToolWindow.test.ts
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/layout/timeline/clip/ClipFormantToolWindow.tsx frontend/src/components/layout/timeline/clip/VowelChart.tsx frontend/src/features/keybindings/useKeybindings.ts frontend/src/components/layout/timeline/clip/clipFormantInteractionGuards.test.ts
git commit -m "fix: harden clip formant window interactions"
```

### Task 2: DSP Protection Without Weakening Good Material

**Files:**
- Modify: `backend/src-tauri/src/audio/formant_morph.rs`

- [ ] **Step 1: Write the failing test**

Add these tests inside `backend/src-tauri/src/audio/formant_morph.rs`:

```rust
#[test]
fn voiced_frames_keep_strong_effect_without_runaway_peaks() {
    let input: Vec<f32> = (0..16_000)
        .map(|idx| {
            let t = idx as f32 / 16_000.0;
            let mut sample = 0.0f32;
            for harmonic in 1..=12 {
                sample +=
                    (2.0 * std::f32::consts::PI * 110.0 * harmonic as f32 * t).sin()
                        / (harmonic as f32).powf(1.15);
            }
            sample * 0.12
        })
        .collect();
    let params = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 900.0,
        target_f2_hz: 800.0,
        strength: 0.95,
    };

    let output = apply_formant_morph_mono(&input, 16_000, &params).unwrap();
    let diff = average_abs_diff(&input, &output);
    let in_peak = input.iter().fold(0.0f32, |peak, sample| peak.max(sample.abs()));
    let out_peak = output.iter().fold(0.0f32, |peak, sample| peak.max(sample.abs()));

    assert!(diff > 0.004, "expected strong voiced morph effect, got diff={diff}");
    assert!(out_peak <= 0.98, "expected protected output peak, got {out_peak}");
    assert!(out_peak <= in_peak * 2.2, "expected bounded peak growth, in={in_peak} out={out_peak}");
}

#[test]
fn noisy_low_energy_material_is_softened_relative_to_voiced_material() {
    let voiced: Vec<f32> = (0..8_000)
        .map(|idx| {
            let t = idx as f32 / 16_000.0;
            ((2.0 * std::f32::consts::PI * 180.0 * t).sin()
                + 0.45 * (2.0 * std::f32::consts::PI * 720.0 * t).sin())
                * 0.14
        })
        .collect();
    let noisy: Vec<f32> = (0..8_000)
        .map(|idx| (((idx as f32) * 12.9898).sin() * 43_758.547).fract() * 0.03)
        .collect();
    let params = ClipFormantMorph {
        enabled: true,
        target_f1_hz: 950.0,
        target_f2_hz: 760.0,
        strength: 0.95,
    };

    let voiced_out = apply_formant_morph_mono(&voiced, 16_000, &params).unwrap();
    let noisy_out = apply_formant_morph_mono(&noisy, 16_000, &params).unwrap();
    let voiced_diff = average_abs_diff(&voiced, &voiced_out);
    let noisy_diff = average_abs_diff(&noisy, &noisy_out);

    assert!(voiced_diff > noisy_diff * 1.2, "expected voiced material to keep stronger effect; voiced={voiced_diff} noisy={noisy_diff}");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cargo test voiced_frames_keep_strong_effect_without_runaway_peaks --quiet --no-run
cargo test noisy_low_energy_material_is_softened_relative_to_voiced_material --quiet --no-run
```

Then run the equivalent focused harness or crate test command available in the current environment and confirm at least one assertion fails with the current DSP behavior.

Expected: FAIL on peak protection or voiced-vs-noisy selectivity.

- [ ] **Step 3: Write minimal implementation**

Update `backend/src-tauri/src/audio/formant_morph.rs` to add:

- a frame suitability helper, for example:

```rust
fn frame_suitability(reference: &[f32], residual: &[f32]) -> f32 {
    let ref_energy = frame_energy(reference);
    let residual_energy = frame_energy(residual);
    if ref_energy <= EPSILON {
        return 0.0;
    }
    let residual_ratio = (residual_energy / ref_energy).clamp(0.0, 4.0);
    (1.15 - residual_ratio).clamp(0.0, 1.0)
}
```

- a bounded frame gain helper, for example:

```rust
fn limit_frame_peak(candidate: &mut [f32], reference: &[f32]) {
    let ref_peak = reference.iter().fold(0.0f32, |peak, sample| peak.max(sample.abs())).max(0.001);
    let cand_peak = candidate.iter().fold(0.0f32, |peak, sample| peak.max(sample.abs())).max(0.001);
    let gain = (ref_peak * 1.8 / cand_peak).clamp(0.0, 1.0);
    if gain < 0.999 {
        for sample in candidate {
            *sample *= gain;
        }
    }
}
```

- a soft limiter after de-emphasis, for example:

```rust
fn soft_limit(sample: f32) -> f32 {
    (sample * 1.4).tanh() / 1.4
}
```

Apply the new logic in this order:

1. compute residual
2. compute frame suitability
3. reduce effective frame strength on poor-suitability frames
4. blend the synthesized frame against the dry frame with that effective strength
5. limit frame peak before overlap-add
6. soft-limit the final de-emphasized output before hard clamp

- [ ] **Step 4: Run tests to verify they pass**

Run the focused backend verification that is available in the environment:

```bash
cargo test formant_morph --quiet --no-run
```

And run the focused executable test path or harness used for `formant_morph.rs` in this workspace until these new assertions pass.

Expected: PASS for the new DSP behavior checks and existing formant morph tests.

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/audio/formant_morph.rs
git commit -m "fix: improve clip formant dsp protection"
```
