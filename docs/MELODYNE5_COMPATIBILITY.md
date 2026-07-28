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
- The desktop importer scans the outer archive through a 64 KiB buffered
  reader and inflates only the object-graph entry. The compressed project and
  unrelated archive entries are never copied into memory. Legacy full-file
  scanning is limited to small archives.
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

## Large-project loading

- Import progress is emitted for container scanning, graph inflation/parsing,
  track and media discovery, sample grouping, clip creation, edit restoration,
  and finalization. The UI remains responsive and shows a localized progress
  overlay throughout the operation.
- Media locations are indexed once instead of rescanning the project directory
  for every source. Clip grouping uses quantized placement buckets rather than
  comparing every element with every existing group.
- Source pitch property points are decoded once per source item and frame
  lookup uses binary search. This removes the previous quadratic behaviour on
  long, densely analysed recordings referenced by many notes.
- Dense imported pitch/formant/volume curves share a 64 MiB aggregate budget;
  long or highly multitrack sessions automatically use a coarser frame period.
  Curves that contain no edits are omitted.
- Initial waveform decoding, pitch analysis, stretching, and background
  rendering are deferred. Only the 30-second playback window near the playhead
  is prefetched, and lightweight timeline polling no longer clones edit curves.

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

The component renderer additionally exposes a pulse timeline, original/current
periods, source-sample-position increments, per-bin current/previous/accumulated
phase, attack phase diffusion/continuation, and a dedicated `isSibilant` path.
Its element layer stores separate attack duration/slope and decay elongation,
while the registered stretch modes distinguish attack adjustment, body stretch,
decay stretch/crop, and linear stretch. These observations are why HachiShifter
evaluates the exact Bezier time function at 2-ms subdivisions through one
persistent stretcher state, rather than restarting a resampler at each handle.

The product UI also contains operations for moving an attack to the nearest
left/right signal peak and splitting/reseparating elements at attacks. These
observations support treating the note body, transient and sibilant residual as
independent components rather than applying one resampler to the whole signal.

The MPD note graph persists `followingElement`, `joinsPitches`,
`pitchTransitionDuration`, `attackDuration`, and
`sourceTimeForElementTimeFunctionAttackSlope` independently. HachiShifter keeps
those element identities across clip boundaries. Connect can therefore point
to a note in another media item on the same root track: only the note-centre
trajectory receives a zero-velocity/zero-acceleration transition, while the
dense drift/modulation residual remains attached to each pronunciation. An
unconnected boundary remains untouched, and Disconnect restores the
authoritative pre-join curve.

Moving the attack handle keeps its source coordinate fixed and
re-parameterizes both the pre-attack and post-attack halves of the sampled time
function. Moving only the consonant-side chunk would leave a time-slope step at
the vowel boundary and produce a click or tail detuning. Imported projects read
both the destination attack duration and source-side slope/anchor before their
first render.

## HachiShifter mapping

The model-free `mld5` path now performs:

1. WORLD analysis/synthesis for the edited periodic principal and continuous F0.
2. STFT real-cepstrum envelope estimation for both source and synthesis.
3. Low-quefrency envelope matching while retaining synthesized phase and F0,
   with bounded gain and RMS normalization to preserve vocal identity.
4. High-frequency/non-periodic residual restoration at detected attacks. The
   low-frequency synthesized principal is retained so an attack does not leak
   the original pitch back into a shifted note.
5. MPD `startSibilantEndSampleOffset` / `endSibilantStartSampleOffset` boundaries
   are restored as a soft source-synchronous mask. For ordinary audio, an
   adaptive high-band/zero-crossing detector supplies the equivalent component.
6. The edited curve drives WORLD as an absolute F0 trajectory. A new detector
   is used only for periodic/voicing analysis and no longer adds its pitch error
   to Melodyne's stored contour.
7. Short MPD note clips receive temporary reflected analysis context before
   periodic synthesis and are cropped back sample-exactly. This avoids resetting
   F0 analysis on an isolated 30--200 ms fragment while keeping project timing
   and source boundaries unchanged.
8. Variable-ratio Signalsmith rendering preloads `inputLatency` with `seek`,
   feeds every source-time chunk from the required look-ahead position, removes
   `outputLatency` once, and flushes only the remaining output tail. This keeps
   warped PCM sample-aligned with the MPD/FCPE F0 curve instead of shifting the
   waveform by the stretcher's analysis window.
9. Persisted `joinsPitches` transitions follow their actual `followingElement`
   reference and `pitchTransitionDuration`. Only the pitch-centre offset is
   interpolated; modulation/drift residuals on both samples remain intact.
10. Above roughly +5 semitones, the mld5-only periodicity guard progressively
    reduces WORLD's low/mid-band D4C aperiodicity. The separately restored
    sibilant/attack component remains intact, avoiding the doubled hoarse
    texture on high notes without blurring the requested F0.
11. The MPD's exact 1024-sample analysis clock is evaluated through a bounded
    cubic Hermite display/playback cache at the 5-ms edit period. Stored points
    remain exact, silent spans stay separated, and the piano roll receives the
    same destination-time per-clip contour instead of waiting for a second
    detector pass.
12. Short WORLD unvoiced runs at pronunciation endings are extended only when
    the waveform still correlates with the preceding period. This prevents the
    dry, pre-transposition F0 from leaking into voiced tails, while actual
    breaths and fricatives remain unvoiced.
13. MPD source and edited F0 are retained per element/clip. A single root-track
    array cannot represent two overlapping source elements; its former
    last-writer-wins behaviour made one sample follow another sample's attack
    contour. Rendering and the piano-roll display now select the clip-local
    target and apply later user edits only as a delta.
14. `isConsideredSilent` and the persisted leading/trailing sibilant component
    boundaries split the pitch path. Invalid attack/tail points are not
    interpolated and zero-valued gaps begin a new canvas subpath. The note body
    remains visible in a lighter colour and stored sibilant handles are drawn
    as vertical separators.
15. Moving an attack handle applies the identical two-sided destination-time
    remap to audio, source F0, edited F0 and sibilant handles. This prevents the
    audio and pitch line from acquiring different time coordinates after an
    attack edit.
16. `amplitudeFadeInEndSourceTime` and
    `amplitudeFadeOutStartSourceTime` use the audio-description clock, not the
    source item's local clock. The item `startSampleIndex` is removed before
    inverting the element time function, restoring short overlap fades on
    reused/trimmed samples instead of turning them into full-note fades or
    zero-length tails.
17. Pitch property points are valid only inside their 1024-sample analysis
    cells. The importer no longer extrapolates the nearest point beyond half a
    cell or replaces a missing value with the note centre. Unvoiced and
    uncertain attack/tail spans therefore remain zero-valued gaps, are omitted
    from the pitch line, and retain the lighter note-body display.

GAME remains authoritative for identity: one GAME note is exactly one syllable,
and its start is exactly the beat-alignment point. A separate backward pass from
that point uses adaptive energy, positive flux, high-frequency ratio and
zero-crossing density to find a preceding consonant/plosive/sibilant onset. The
prefix becomes the fixed non-stretched region; it never creates or merges notes.
