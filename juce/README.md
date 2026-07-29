# HachiShifter Next — JUCE native rewrite

`next` is the native JUCE rewrite of HachiShifter. It is intentionally isolated
from the Tauri/Vue application while the DSP and importers are migrated and
validated against the existing regression fixtures.

## Current native foundation

- JUCE 8, C++20 and CMake;
- model-free Windows and Linux packages;
- native multi-track project model with BPM and beat-origin persistence;
- sample-accurate multi-track transport, clip gain/pan and overlap fades;
- sample-rate-independent reader/resampling path;
- Melodyne-style dark/orange piano roll and source-edit wrench mode;
- pitch contours remain relative to the note centre when a note is dragged;
- voiced gaps, consonant shading, sibilant markers and note joins;
- native project save/load (`.hspx`);
- complete strings for zh-CN, zh-TW, ja-JP, ko-KR and en-US.

## Migration boundary

The existing `backend` and `frontend` remain in the branch as executable
specifications and regression fixtures. New production code belongs under
`juce/`. MPD parsing, GAME/FCPE inference and the reconstructed mld5 renderer
will move behind native C++ service interfaces before the legacy directories
are removed.

## Cloud build

No local build is required. Pushes to `next` trigger
`.github/workflows/compile-next-juce.yml` and create model-free Linux and Windows
archives.

