# HachiShifter Next — JUCE native rewrite

`next` is the native JUCE/C++20 rewrite of HachiShifter. The former Tauri/Vue
frontend and Rust backend have been removed from this branch; their history
remains available on `main` for regression reference.

## Current native foundation

- JUCE 8, C++20 and CMake;
- model-free Windows and Linux packages;
- native multi-track project model with BPM and beat-origin persistence;
- sample-accurate multi-track transport, clip gain/pan and overlap fades;
- sample-rate-independent reader/resampling path;
- legacy-compatible dark piano roll using `#7F69CA`, `#CBCBFA` and `#F4C000`,
  plus the source-edit wrench mode;
- pitch contours remain relative to the note centre when a note is dragged;
- freehand and line target-pitch editing while retaining the source F0 contour;
- Melodyne-compatible pitch-modulation and pitch-drift correction controls;
- editable Melodyne Attack boundary and Attack Speed with independent time mapping;
- voiced gaps, consonant shading, sibilant markers and note joins;
- native project save/load (`.hspx`);
- portable HSPX media paths with relative and recursive project-folder relinking;
- complete strings for zh-CN, zh-TW, ja-JP, ko-KR and en-US.

## Native backend

Production code lives under `juce/`. `src/backend` contains model-free DSP and
parallel offline render services; realtime transport and project scheduling are
implemented by `AudioEngine`. Additional analysis backends are added as C++
services rather than restoring the removed web/Rust layers.

## Cloud build

No local build is required. Pushes to `next` trigger
`.github/workflows/compile-next-juce.yml` and create model-free Linux and Windows
archives.
