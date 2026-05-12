# Clip Formant Follow-Up Design

## Goal

Fix the clip-level formant tool's interaction bugs and improve the DSP so it keeps a strong audible effect while reducing clipping, harsh artifacts, and poor results on unsuitable material.

## Problems To Solve

### Interaction Bugs

1. clicking the floating formant window can still affect the timeline behind it
2. dragging the vowel control point can select nearby text
3. pressing `Space` while the window is active can toggle the enable control instead of only behaving as a transport shortcut or doing nothing

### DSP Quality Problems

The current LPC-based formant morph can:

- overshoot level on some frames
- sound harsh or unstable on noisy or weakly voiced material
- apply too much processing to segments that are not good formant-morph candidates

The user prefers effect strength over a purely conservative rewrite, so the design should preserve clearly audible vowel movement while adding targeted protection.

## Scope

### In Scope

- clip formant floating window event isolation
- vowel chart drag behavior
- keyboard handling while the clip formant window is active
- backend formant DSP protection and effect-selection improvements

### Out of Scope

- redesigning the clip formant UI layout
- changing the clip `formant_morph` data model
- replacing the LPC-based algorithm with a different synthesis family

## Recommended Approach

Use a two-part fix:

1. make the floating tool window behave like a true foreground interaction surface
2. keep the current strong formant effect, but add selective DSP guards so the strongest processing only applies when the input frame is suitable

This keeps the tool feeling powerful without letting it misbehave on every kind of source.

## Frontend Design

### Foreground Event Isolation

The floating clip formant window should explicitly block interaction from leaking through to the timeline behind it.

Apply this at the top-level formant window container in `frontend/src/components/layout/timeline/clip/ClipFormantToolWindow.tsx`:

- stop propagation for `pointerdown`
- stop propagation for `click`
- stop propagation for `doubleclick`
- stop propagation for `contextmenu`
- stop propagation for `keydown`

This should make the formant window behave like other foreground editing surfaces in the app.

### Vowel Chart Drag Behavior

The vowel chart in `frontend/src/components/layout/timeline/clip/VowelChart.tsx` should suppress browser text selection during drag.

Required behavior:

- `onPointerDown` should call `preventDefault()`
- pointer move drag updates should continue to use the current pointer-capture style behavior
- the SVG surface should use `user-select: none`
- if needed, the formant window wrapper can also set `user-select: none` for the interactive region

This addresses the "dragging control point selects text" issue directly at the source.

### Space Key Behavior

The formant window should not let global `Space` shortcuts act as if the timeline is the active surface while the window is focused.

Recommended behavior:

- when the clip formant window is open or focused, global playback shortcut handling should ignore `Space`
- the enable checkbox should not toggle because of bubbled keyboard events from the global handler path

Implementation shape:

- add a lightweight body attribute or focused-window marker when the clip formant window is active
- update global keybinding routing so `playback.toggle` is suppressed when that marker is present
- also stop local keydown propagation from the formant window container as a defense-in-depth measure

This keeps the timeline shortcut system intact while preventing the formant tool from acting like an unfocused background overlay.

## DSP Design

### Principle

Keep the audible formant effect strong on voiced vowel-like material, but reduce how aggressively it acts on frames that are poor LPC/formant candidates.

### Processing Changes

The backend work remains in `backend/src-tauri/src/audio/formant_morph.rs`.

Add four protection layers.

#### 1. Frame Gain Protection

After LPC re-synthesis and frame energy matching:

- clamp maximum frame gain expansion more tightly than today
- reject or soften frames whose peak or RMS jumps too far relative to the dry frame

This prevents isolated explosive frames from dominating the final overlap-add result.

#### 2. Suitability Weighting

Before applying full-strength formant migration, compute a simple frame suitability score from signals already available in the current flow, such as:

- frame energy
- LPC stability
- residual-to-frame energy ratio or similar voicedness proxy

Use that score to reduce effective morph strength on:

- low-energy frames
- weakly voiced frames
- noisy or fricative-heavy frames

This keeps the stronger effect focused on vowel-like material.

#### 3. Soft Output Limiting

After full-buffer de-emphasis:

- keep peak normalization
- add a soft-limiter or soft-saturation stage before the final hard clamp

This should absorb overshoot more gracefully than relying on hard clipping alone.

#### 4. Split Strength Mapping

Do not use one linear `strength` value identically for every internal step.

Instead split it into:

- formant movement strength
- wet blend strength

This lets high user strength keep a strong vowel shift without forcing every frame to become fully wet at the same rate.

## Data Flow Impact

No caller contract changes are required.

The existing shared backend call path remains:

- clip update triggers cache rebuild
- cache rebuild uses shared formant DSP
- timeline playback consumes cache
- export consumes the same processed result

Only the internal DSP decision-making and frontend interaction behavior change.

## Testing Strategy

### Frontend

Add focused tests where practical for:

- formant window keyboard event isolation
- vowel chart helper or interaction behavior if existing test coverage supports it

Manual verification should confirm:

- clicking inside the window no longer selects or edits timeline content
- dragging the formant point does not select text
- pressing `Space` with the formant window active no longer toggles the enable checkbox

### Backend

Extend `backend/src-tauri/src/audio/formant_morph.rs` tests to cover:

- output remains finite and length-preserving
- strong voiced input still changes audibly
- stronger settings still produce more effect than weaker settings
- high-energy pathological cases do not produce runaway peaks
- low-energy or poor-suitability frames are softened rather than fully over-processed

## Risks and Mitigations

### Risk: Too Much Protection Weakens the Effect

Mitigation:

- reduce strength only on unsuitable frames
- keep full-strength behavior for voiced vowel-like segments
- separate morph strength from wet blend rather than globally weakening both

### Risk: Keyboard Fix Breaks Timeline Shortcuts Elsewhere

Mitigation:

- scope the suppression to the formant tool active/focused state only
- leave the rest of the keybinding system unchanged

### Risk: Pointer/Event Fixes Interfere with Window Dragging

Mitigation:

- block propagation broadly, but preserve the current explicit drag-handle logic
- verify title-bar dragging still works after event isolation changes

## Success Criteria

This follow-up is complete when:

- clicks inside the formant window no longer affect the timeline behind it
- dragging the vowel point does not select text
- `Space` no longer toggles the enable state while the formant window is active
- the DSP retains a strong audible formant effect on good material
- clipping and harsh artifacts are meaningfully reduced on difficult material
