# Melodyne 5 compatibility notes

This document records the clean-room observations used by HachiShifter's
`mld5` implementation. No Melodyne executable code or model data is bundled.

## MPD container

- Melodyne 5.4 projects use a `GNBCFA` outer archive.
- Each entry has a length-prefixed UTF-8 name, 8-byte alignment and a 64-bit
  payload length.
- Project graph entries begin with `GNBKVAi\0`; their data after the 20-byte
  header is zlib-compressed.
- The decoded graph is a `GNBinaryKeyValueArchive`. Audio media is referenced
  by `GNFilePath`, normally stored as UTF-16LE.
- HachiShifter bounds-checks the container, caps an inflated entry at 256 MiB,
  and reads the key/class/object tables of the graph. Object, class, field and
  allocation counts are capped before any graph data is used.
- `MUPerformance.rootTrack`, `MUTrack.subtracks` and `MUTrack.elements` restore
  Melodyne's track assignment instead of creating one track for every file.
  Track titles, mute/solo state and the original order are retained.
- `MUAudioComponent` references are followed through the source element and
  source description to `MUAudioFileSource`. Exact, relative, basename and
  one-level `Audio Files`-style locations are resolved; missing paths are sent
  to the existing relink dialog.
- Imported MPD sessions are deliberately unsaved HachiShifter projects. Save
  opens Save As, so the source `.mpd` is never overwritten. Referenced audio is
  present on the timeline immediately and the transport can play it before
  pitch analysis or model download.

Multiple source files on a Melodyne track remain clips on one HachiShifter
track. Repeated use of a file is separated by its inferred source-to-project
placement. Source/element time functions restore trim and overall stretch;
negative Melodyne preroll is shifted as a whole so all tracks keep their exact
relative timing while HachiShifter's transport begins at zero.

Tracks whose analyzer parameter set is `melodic` automatically select `mld5`
and enable Compose. The imported editable curves retain:

- note pitch center;
- pitch drift and modulation factors, reconstructed from source property
  points and the stored pitch-without-vibrato curve;
- formant offset, amplitude factor and muted notes;
- explicit successive-note pitch joins and their transition duration.

Touching source clips on one track receive a short smooth contour bridge. This
matches Melodyne's connected-note transport and avoids a discontinuity at a
multi-sample edit. Raw media remains available immediately, while Compose uses
the reconstructed curve without requiring a GAME model download.

## Observed note/audio model

Static inspection of MelodyneCore 5.4 and the decoded sample graph exposes
separate objects and controls for:

- `MUAudioSourcePrincipalItem`, `MUAudioSourceAttackItem`, and
  `MUAudioSourceSibilantItem`;
- harmonic spectra, attack quality, phase diffusion at attacks, continuing
  phase state and the `resetAllPhasesAtAttack` option;
- independent pitch center, drift, modulation and transition duration;
- independent formant offset/transition and sibilant balance;
- source-time and warp-time functions.

The product UI also contains operations for moving an attack to the nearest
left/right signal peak and splitting/reseparating elements at attacks. These
observations support treating the note body, transient and sibilant residual as
independent components rather than applying one resampler to the whole signal.

## HachiShifter mapping

The model-free `mld5` path now performs:

1. WORLD analysis/synthesis for the edited periodic principal and continuous F0.
2. STFT real-cepstrum envelope estimation for both source and synthesis.
3. Low-quefrency envelope matching while retaining synthesized phase and F0,
   with bounded gain and RMS normalization to preserve vocal identity.
4. High-frequency/non-periodic residual restoration at detected attacks. The
   low-frequency synthesized principal is retained so an attack does not leak
   the original pitch back into a shifted note.

GAME remains authoritative for identity: one GAME note is exactly one syllable,
and its start is exactly the beat-alignment point. A separate backward pass from
that point uses adaptive energy, positive flux, high-frequency ratio and
zero-crossing density to find a preceding consonant/plosive/sibilant onset. The
prefix becomes the fixed non-stretched region; it never creates or merges notes.
