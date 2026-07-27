//! Per-sample timing annotations backed by authoritative GAME syllable notes.
//!
//! The sidecar format intentionally stays small and human-editable:
//! `name,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec,relative_pitch_cents`.
//! The non-stretched part is `[region_start, region_start + fixed_duration]` and
//! the stretchable part is the remainder of the region.  Times are relative to
//! the source audio file, not to a timeline clip.

use encoding_rs::SHIFT_JIS;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashMap};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};

const SIDECAR_SUFFIX: &str = ".hachi.csv";
const CSV_HEADER: &str = "name,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec,relative_pitch_cents,melodyne_project_data,melodyne_pitch_center_cents,melodyne_original_pitch_center_cents,melodyne_pitch_drift_factor,melodyne_pitch_modulation_factor,melodyne_transition_sec,melodyne_formant_offset_cents,melodyne_amplitude_factor,melodyne_sibilant_balance,melodyne_attack_duration_sec,melodyne_decay_elongation\n";

fn one_f64() -> f64 { 1.0 }

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SampleRegionAnnotation {
    pub name: String,
    pub region_start_sec: f64,
    pub region_end_sec: f64,
    pub note_alignment_sec: f64,
    pub fixed_duration_sec: f64,
    /// Piano-key relation of this syllable in the original-source editor.
    /// Zero preserves the detected GAME pitch; edits are applied to the final
    /// stretched/processed contour as a delta, never by flattening the line.
    #[serde(default)]
    pub relative_pitch_cents: f64,
    /// Controls restored directly from a Melodyne element. They remain in
    /// source time so the wrench editor can show the same note-object values.
    #[serde(default)]
    pub melodyne_project_data: bool,
    #[serde(default)]
    pub melodyne_pitch_center_cents: f64,
    #[serde(default)]
    pub melodyne_original_pitch_center_cents: f64,
    #[serde(default = "one_f64")]
    pub melodyne_pitch_drift_factor: f64,
    #[serde(default = "one_f64")]
    pub melodyne_pitch_modulation_factor: f64,
    #[serde(default)]
    pub melodyne_transition_sec: f64,
    #[serde(default)]
    pub melodyne_formant_offset_cents: f64,
    #[serde(default = "one_f64")]
    pub melodyne_amplitude_factor: f64,
    #[serde(default)]
    pub melodyne_sibilant_balance: f64,
    #[serde(default)]
    pub melodyne_attack_duration_sec: f64,
    #[serde(default)]
    pub melodyne_decay_elongation: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SamplePitchNote {
    pub start_sec: f64,
    pub end_sec: f64,
    pub midi_note: f32,
    #[serde(default)]
    pub confidence: f32,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SampleAudioEventKind {
    Silence,
    Breath,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SampleAudioEvent {
    pub start_sec: f64,
    pub end_sec: f64,
    pub kind: SampleAudioEventKind,
    pub confidence: f32,
}

#[derive(Debug, Clone, Serialize)]
pub struct SampleAnalysis {
    pub annotations: Vec<SampleRegionAnnotation>,
    pub pitch_notes: Vec<SamplePitchNote>,
    pub audio_events: Vec<SampleAudioEvent>,
    pub note_detector: NoteDetectorKind,
    pub detector_message: Option<String>,
}

#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum NoteDetectorKind {
    Game,
    Melodyne,
}

#[derive(Debug, Clone, Serialize)]
pub struct OtoConversionItem {
    pub audio_path: String,
    pub sidecar_path: String,
    pub annotation_count: usize,
}

#[derive(Debug, Clone, Serialize)]
pub struct OtoConversionResult {
    pub oto_files: usize,
    pub converted_samples: Vec<OtoConversionItem>,
    pub warnings: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ClipAnnotationTiming {
    /// Source frames before this point stay at 1:1 speed.  This value is
    /// relative to the clip's non-negative source start.
    pub fixed_prefix_sec: f64,
    /// Alignment point relative to the clip's non-negative source start.
    pub alignment_sec: f64,
}

#[derive(Debug, Clone)]
struct OtoEntry {
    wav: String,
    alias: String,
    offset_ms: f64,
    consonant_ms: f64,
    cutoff_ms: f64,
    preutter_ms: f64,
}

#[derive(Clone)]
struct CachedSampleAnalysis {
    file_len: u64,
    modified_ns: u128,
    analysis: SampleAnalysis,
}

static ANALYSIS_CACHE: OnceLock<Mutex<HashMap<PathBuf, CachedSampleAnalysis>>> = OnceLock::new();
static MELODYNE_PROJECT_CACHE: OnceLock<Mutex<HashMap<PathBuf, CachedSampleAnalysis>>> = OnceLock::new();

fn analysis_fingerprint(path: &Path) -> Option<(u64, u128)> {
    let metadata = fs::metadata(path).ok()?;
    let modified_ns = metadata
        .modified()
        .ok()?
        .duration_since(std::time::UNIX_EPOCH)
        .ok()?
        .as_nanos();
    Some((metadata.len(), modified_ns))
}

fn cache_analysis(path: &Path, analysis: &SampleAnalysis) {
    let Some((file_len, modified_ns)) = analysis_fingerprint(path) else {
        return;
    };
    if let Ok(mut cache) = ANALYSIS_CACHE
        .get_or_init(|| Mutex::new(HashMap::new()))
        .lock()
    {
        if cache.len() >= 32 && !cache.contains_key(path) {
            if let Some(oldest) = cache.keys().next().cloned() {
                cache.remove(&oldest);
            }
        }
        cache.insert(
            path.to_path_buf(),
            CachedSampleAnalysis {
                file_len,
                modified_ns,
                analysis: analysis.clone(),
            },
        );
    }
}

fn cached_analysis(path: &Path) -> Option<SampleAnalysis> {
    let (file_len, modified_ns) = analysis_fingerprint(path)?;
    let cache = ANALYSIS_CACHE
        .get_or_init(|| Mutex::new(HashMap::new()))
        .lock()
        .ok()?;
    let cached = cache.get(path)?;
    (cached.file_len == file_len && cached.modified_ns == modified_ns)
        .then(|| cached.analysis.clone())
}

/// Register boundaries and note controls read from an MPD object graph. This
/// cache deliberately precedes GAME/sidecars while the imported project is
/// open, so opening its wrench view shows Melodyne's stored segmentation.
pub fn register_melodyne_project_analysis(path: &Path, analysis: SampleAnalysis) {
    let Some((file_len, modified_ns)) = analysis_fingerprint(path) else { return; };
    if let Ok(mut cache) = MELODYNE_PROJECT_CACHE
        .get_or_init(|| Mutex::new(HashMap::new()))
        .lock()
    {
        if cache.len() >= 512 && !cache.contains_key(path) {
            if let Some(key) = cache.keys().next().cloned() { cache.remove(&key); }
        }
        cache.insert(path.to_path_buf(), CachedSampleAnalysis { file_len, modified_ns, analysis });
    }
}

fn melodyne_project_analysis(path: &Path) -> Option<SampleAnalysis> {
    let (file_len, modified_ns) = analysis_fingerprint(path)?;
    let cache = MELODYNE_PROJECT_CACHE
        .get_or_init(|| Mutex::new(HashMap::new())).lock().ok()?;
    let item = cache.get(path)?;
    (item.file_len == file_len && item.modified_ns == modified_ns)
        .then(|| item.analysis.clone())
}

pub fn update_melodyne_project_annotations(path: &Path, annotations: &[SampleRegionAnnotation]) {
    if let Ok(mut cache) = MELODYNE_PROJECT_CACHE
        .get_or_init(|| Mutex::new(HashMap::new())).lock()
    {
        if let Some(item) = cache.get_mut(path) {
            item.analysis.annotations = annotations.to_vec();
        }
    }
}

pub fn sidecar_path(audio_path: &Path) -> PathBuf {
    let mut os = audio_path.as_os_str().to_os_string();
    os.push(SIDECAR_SUFFIX);
    PathBuf::from(os)
}

pub fn validate_annotations(
    annotations: &[SampleRegionAnnotation],
    duration_sec: Option<f64>,
) -> Result<Vec<SampleRegionAnnotation>, String> {
    let duration = duration_sec.filter(|value| value.is_finite() && *value > 0.0);
    let mut out = Vec::with_capacity(annotations.len());
    for (index, annotation) in annotations.iter().enumerate() {
        let values = [
            annotation.region_start_sec,
            annotation.region_end_sec,
            annotation.note_alignment_sec,
            annotation.fixed_duration_sec,
            annotation.relative_pitch_cents,
            annotation.melodyne_pitch_center_cents,
            annotation.melodyne_original_pitch_center_cents,
            annotation.melodyne_pitch_drift_factor,
            annotation.melodyne_pitch_modulation_factor,
            annotation.melodyne_transition_sec,
            annotation.melodyne_formant_offset_cents,
            annotation.melodyne_amplitude_factor,
            annotation.melodyne_sibilant_balance,
            annotation.melodyne_attack_duration_sec,
            annotation.melodyne_decay_elongation,
        ];
        if values.iter().any(|value| !value.is_finite()) {
            return Err(format!(
                "annotation row {} contains a non-finite time",
                index + 1
            ));
        }
        let start = annotation.region_start_sec.max(0.0);
        let mut end = annotation.region_end_sec.max(0.0);
        if let Some(duration) = duration {
            end = end.min(duration);
        }
        if end <= start {
            return Err(format!(
                "annotation row {} must end after it starts",
                index + 1
            ));
        }
        let alignment = annotation.note_alignment_sec.clamp(start, end);
        let fixed = annotation.fixed_duration_sec.clamp(0.0, end - start);
        let clean_name = annotation
            .name
            .trim()
            .replace(['\r', '\n'], " ");
        out.push(SampleRegionAnnotation {
            name: if clean_name.is_empty() {
                format!("region {}", index + 1)
            } else {
                clean_name
            },
            region_start_sec: start,
            region_end_sec: end,
            note_alignment_sec: alignment,
            fixed_duration_sec: fixed,
            relative_pitch_cents: annotation.relative_pitch_cents.clamp(-4800.0, 4800.0),
            melodyne_project_data: annotation.melodyne_project_data,
            melodyne_pitch_center_cents: annotation.melodyne_pitch_center_cents.clamp(0.0, 12700.0),
            melodyne_original_pitch_center_cents: annotation.melodyne_original_pitch_center_cents.clamp(0.0, 12700.0),
            melodyne_pitch_drift_factor: annotation.melodyne_pitch_drift_factor.clamp(0.0, 2.0),
            melodyne_pitch_modulation_factor: annotation.melodyne_pitch_modulation_factor.clamp(0.0, 2.0),
            melodyne_transition_sec: annotation.melodyne_transition_sec.clamp(0.0, 2.0),
            melodyne_formant_offset_cents: annotation.melodyne_formant_offset_cents.clamp(-2400.0, 2400.0),
            melodyne_amplitude_factor: annotation.melodyne_amplitude_factor.clamp(0.0, 4.0),
            melodyne_sibilant_balance: annotation.melodyne_sibilant_balance.clamp(-1.0, 1.0),
            melodyne_attack_duration_sec: annotation.melodyne_attack_duration_sec.clamp(0.0, 2.0),
            melodyne_decay_elongation: annotation.melodyne_decay_elongation.clamp(-2.0, 4.0),
        });
    }
    out.sort_by(|a, b| {
        a.region_start_sec
            .total_cmp(&b.region_start_sec)
            .then_with(|| a.region_end_sec.total_cmp(&b.region_end_sec))
    });
    Ok(out)
}

pub fn read_sidecar(audio_path: &Path) -> Result<Vec<SampleRegionAnnotation>, String> {
    let path = sidecar_path(audio_path);
    let text = fs::read_to_string(&path)
        .map_err(|error| format!("failed to read {}: {error}", path.display()))?;
    parse_csv(&text)
}

pub fn write_sidecar(
    audio_path: &Path,
    annotations: &[SampleRegionAnnotation],
) -> Result<PathBuf, String> {
    let annotations = validate_annotations(annotations, None)?;
    let path = sidecar_path(audio_path);
    let mut csv = String::from(CSV_HEADER);
    for row in annotations {
        csv.push_str(&escape_csv(&row.name));
        csv.push(',');
        csv.push_str(&format_time(row.region_start_sec));
        csv.push(',');
        csv.push_str(&format_time(row.region_end_sec));
        csv.push(',');
        csv.push_str(&format_time(row.note_alignment_sec));
        csv.push(',');
        csv.push_str(&format_time(row.fixed_duration_sec));
        csv.push(',');
        csv.push_str(&format_time(row.relative_pitch_cents));
        csv.push(','); csv.push_str(if row.melodyne_project_data { "1" } else { "0" });
        csv.push(','); csv.push_str(&format_time(row.melodyne_pitch_center_cents));
        csv.push(','); csv.push_str(&format_time(row.melodyne_original_pitch_center_cents));
        csv.push(','); csv.push_str(&format_time(row.melodyne_pitch_drift_factor));
        csv.push(','); csv.push_str(&format_time(row.melodyne_pitch_modulation_factor));
        csv.push(','); csv.push_str(&format_time(row.melodyne_transition_sec));
        csv.push(','); csv.push_str(&format_time(row.melodyne_formant_offset_cents));
        csv.push(','); csv.push_str(&format_time(row.melodyne_amplitude_factor));
        csv.push(','); csv.push_str(&format_time(row.melodyne_sibilant_balance));
        csv.push(','); csv.push_str(&format_time(row.melodyne_attack_duration_sec));
        csv.push(','); csv.push_str(&format_time(row.melodyne_decay_elongation));
        csv.push('\n');
    }
    fs::write(&path, csv.as_bytes())
        .map_err(|error| format!("failed to write {}: {error}", path.display()))?;
    Ok(path)
}

pub fn load_or_detect(audio_path: &Path) -> Result<SampleAnalysis, String> {
    load_or_detect_with_game_mode(audio_path, false)
}

/// Analyze a sample with GAME large by default, or GAME small when the editor
/// explicitly enables performance mode. Performance-mode results are kept out
/// of the default cache so switching modes never reuses the wrong model.
pub fn load_or_detect_with_game_mode(
    audio_path: &Path,
    performance_mode: bool,
) -> Result<SampleAnalysis, String> {
    if !performance_mode {
        if let Some(analysis) = melodyne_project_analysis(audio_path) {
            return Ok(analysis);
        }
    }
    let detected = if performance_mode {
        analyze_audio_uncached(audio_path, true)?
    } else {
        analyze_audio(audio_path)?
    };
    let annotations = if sidecar_path(audio_path).is_file() {
        match read_sidecar(audio_path) {
            Ok(rows) if !rows.is_empty() => {
                // Migrate sidecars written by the first GAME integration,
                // where the GAME beat point was also used as the region start.
                // Auto-generated GAME rows now include the consonant/attack
                // prefix found by the Melodyne-style backward analysis below.
                let legacy_game_rows = rows.iter().all(|row| {
                    row.name.starts_with("GAME ")
                        && row.fixed_duration_sec <= 1e-6
                        && (row.region_start_sec - row.note_alignment_sec).abs() <= 1e-6
                });
                if legacy_game_rows
                    && detected
                        .annotations
                        .iter()
                        .any(|row| row.fixed_duration_sec > 0.005)
                {
                    let _ = write_sidecar(audio_path, &detected.annotations);
                    detected.annotations.clone()
                } else {
                    rows
                }
            }
            _ => {
                // Analysis should still be usable for read-only sample
                // locations. Explicit Save will report write failures.
                let _ = write_sidecar(audio_path, &detected.annotations);
                detected.annotations.clone()
            }
        }
    } else {
        let _ = write_sidecar(audio_path, &detected.annotations);
        detected.annotations.clone()
    };
    Ok(SampleAnalysis {
        annotations,
        pitch_notes: detected.pitch_notes,
        audio_events: detected.audio_events,
        note_detector: detected.note_detector,
        detector_message: detected.detector_message,
    })
}

/// Return the sidecar location while deferring creation until GAME has run.
/// Import must stay lightweight, but writing an acoustic placeholder here
/// would incorrectly turn non-GAME regions into syllables.
pub fn ensure_sidecar(audio_path: &Path) -> Result<PathBuf, String> {
    let path = sidecar_path(audio_path);
    if path.is_file() {
        if let Ok(rows) = read_sidecar(audio_path) {
            if !rows.is_empty() {
                return Ok(path);
            }
        }
    }
    Ok(path)
}

pub fn timing_for_clip(clip: &crate::state::Clip) -> Option<ClipAnnotationTiming> {
    if clip.reversed {
        return None;
    }
    let source = clip.source_path.as_deref()?;
    let annotations = read_sidecar(Path::new(source)).ok()?;
    if annotations.is_empty() {
        return None;
    }
    let index = clip
        .extra_params
        .as_ref()
        .and_then(|params| params.get("hachi_active_annotation"))
        .copied()
        .unwrap_or(0.0)
        .round()
        .max(0.0) as usize;
    let annotation = annotations.get(index.min(annotations.len() - 1))?;
    let source_start = clip.source_start_sec.max(0.0);
    let source_end = clip.source_end_sec.max(source_start);
    let source_span = source_end - source_start;
    let fixed_end = annotation.region_start_sec + annotation.fixed_duration_sec;
    let fixed_prefix_sec = (fixed_end - source_start).clamp(0.0, source_span);
    if fixed_prefix_sec <= 1e-6 || fixed_prefix_sec >= source_span - 1e-6 {
        return None;
    }
    Some(ClipAnnotationTiming {
        fixed_prefix_sec,
        alignment_sec: (annotation.note_alignment_sec - source_start).clamp(0.0, source_span),
    })
}

fn analyze_audio_uncached(
    audio_path: &Path,
    game_performance_mode: bool,
) -> Result<SampleAnalysis, String> {
    let (sample_rate, channels, interleaved) =
        crate::audio_utils::decode_audio_f32_interleaved(audio_path)?;
    if sample_rate == 0 || channels == 0 || interleaved.is_empty() {
        return Err("audio has no decodable samples".to_string());
    }
    let mono = interleaved_to_mono(&interleaved, channels as usize);
    let duration_sec = mono.len() as f64 / sample_rate as f64;
    let audio_events = detect_silence_and_breath_events(&mono, sample_rate);

    let (pitch_notes, note_detector, detector_message) =
        match crate::game_detector::detect_notes(
            &mono,
            sample_rate,
            crate::game_detector::GameOptions {
                performance_mode: game_performance_mode,
                ..crate::game_detector::GameOptions::default()
            },
        ) {
            Ok(notes) => {
                let notes: Vec<SamplePitchNote> = notes
                    .into_iter()
                    .filter(|note| {
                        !note.is_rest
                            && note.midi_note.is_finite()
                            && (0.0..=127.0).contains(&note.midi_note)
                            && note.end_sec - note.start_sec >= 0.01
                    })
                    .map(|note| SamplePitchNote {
                        start_sec: note.start_sec,
                        end_sec: note.end_sec.min(duration_sec),
                        midi_note: note.midi_note,
                        confidence: note.confidence,
                    })
                    .filter(|note| note.end_sec > note.start_sec)
                    .collect();
                if notes.is_empty() {
                    (
                        Vec::new(),
                        NoteDetectorKind::Game,
                        Some("GAME returned no syllable notes".to_string()),
                    )
                } else {
                    (notes, NoteDetectorKind::Game, None)
                }
            }
            Err(error) => (
                Vec::new(),
                NoteDetectorKind::Game,
                Some(error),
            ),
        };

    // GAME segmentation is authoritative: one GAME note is one syllable and
    // the GAME note start is always the beat-alignment point. Melodyne's
    // analysed principal/attack/sibilant split is used only to look backward
    // from that vowel anchor for the consonant onset; it never creates another
    // note. This supplies a fixed, non-stretched consonant prefix.
    let syllable_starts = detect_consonant_onsets_before_game_notes(
        &mono,
        sample_rate,
        &pitch_notes,
    );
    let annotations: Vec<SampleRegionAnnotation> = pitch_notes
        .iter()
        .enumerate()
        .map(|(index, note)| {
            let start = syllable_starts
                .get(index)
                .copied()
                .unwrap_or(note.start_sec)
                .clamp(0.0, note.start_sec);
            // When GAME notes touch, the consonant of the next syllable may
            // lie before its GAME vowel point. Assign that prefix to the next
            // syllable instead of overlapping both sample regions.
            let next_start = syllable_starts.get(index + 1).copied();
            let end = next_start
                .filter(|next| *next > note.start_sec + 0.005 && *next < note.end_sec)
                .unwrap_or(note.end_sec)
                .max(note.start_sec + 0.005);
            SampleRegionAnnotation {
                name: format!("GAME {}", index + 1),
                region_start_sec: start,
                region_end_sec: end,
                note_alignment_sec: note.start_sec,
                fixed_duration_sec: (note.start_sec - start).max(0.0),
                relative_pitch_cents: 0.0,
                melodyne_project_data: false,
                melodyne_pitch_center_cents: 0.0,
                melodyne_original_pitch_center_cents: 0.0,
                melodyne_pitch_drift_factor: 1.0,
                melodyne_pitch_modulation_factor: 1.0,
                melodyne_transition_sec: 0.0,
                melodyne_formant_offset_cents: 0.0,
                melodyne_amplitude_factor: 1.0,
                melodyne_sibilant_balance: 0.0,
                melodyne_attack_duration_sec: 0.0,
                melodyne_decay_elongation: 0.0,
            }
        })
        .collect();

    Ok(SampleAnalysis {
        annotations,
        pitch_notes,
        audio_events,
        note_detector,
        detector_message,
    })
}

#[derive(Debug, Clone, Copy)]
struct SyllableFrameFeature {
    rms: f32,
    high_frequency_ratio: f32,
    zero_crossing_ratio: f32,
    positive_flux: f32,
}

/// Locate a consonant/attack onset before every GAME vowel anchor.
///
/// Melodyne's internal representation separates principal (stable/periodic),
/// attack and sibilant items.  This model-free pass mirrors those observable
/// features with adaptive energy, pre-emphasized high-frequency energy,
/// zero-crossing density and positive flux. It intentionally adjusts only the
/// sample boundary; GAME remains the sole owner of note/syllable identity.
fn detect_consonant_onsets_before_game_notes(
    samples: &[f32],
    sample_rate: u32,
    notes: &[SamplePitchNote],
) -> Vec<f64> {
    if samples.is_empty() || sample_rate == 0 || notes.is_empty() {
        return notes.iter().map(|note| note.start_sec).collect();
    }
    let window = ((sample_rate as f64 * 0.020).round() as usize).max(32);
    let hop = ((sample_rate as f64 * 0.005).round() as usize).max(8);
    let frame_count = samples.len().saturating_sub(1).div_ceil(hop).max(1);
    let mut features = Vec::with_capacity(frame_count);
    let mut previous_rms = 0.0f32;
    for frame in 0..frame_count {
        let start = frame.saturating_mul(hop).min(samples.len().saturating_sub(1));
        let end = (start + window).min(samples.len());
        let slice = &samples[start..end];
        let count = slice.len().max(1) as f32;
        let rms = (slice.iter().map(|value| value * value).sum::<f32>() / count).sqrt();
        let mut difference_energy = 0.0f32;
        let mut crossings = 0usize;
        for pair in slice.windows(2) {
            let difference = pair[1] - pair[0];
            difference_energy += difference * difference;
            crossings += ((pair[0] >= 0.0) != (pair[1] >= 0.0)) as usize;
        }
        let difference_rms =
            (difference_energy / slice.len().saturating_sub(1).max(1) as f32).sqrt();
        features.push(SyllableFrameFeature {
            rms,
            high_frequency_ratio: difference_rms / (rms * 2.0).max(1e-7),
            zero_crossing_ratio: crossings as f32 / slice.len().saturating_sub(1).max(1) as f32,
            positive_flux: (rms - previous_rms).max(0.0),
        });
        previous_rms = rms;
    }

    let peak = features.iter().map(|feature| feature.rms).fold(0.0f32, f32::max);
    let mut levels: Vec<f32> = features.iter().map(|feature| feature.rms).collect();
    levels.sort_by(f32::total_cmp);
    let floor = levels[levels.len() / 6];
    let active_threshold = (floor * 2.25).max(peak * 0.009).max(0.0006);
    let frame_sec = hop as f64 / sample_rate as f64;
    let inactive_stop_frames = ((0.020 / frame_sec).ceil() as usize).max(3);
    let max_lookback_frames = ((0.320 / frame_sec).ceil() as usize).max(1);

    let mut result = Vec::with_capacity(notes.len());
    for (note_index, note) in notes.iter().enumerate() {
        let alignment_frame = ((note.start_sec * sample_rate as f64) / hop as f64)
            .round()
            .clamp(0.0, features.len().saturating_sub(1) as f64) as usize;
        let time_lower_bound = if note_index == 0 {
            (note.start_sec - 0.320).max(0.0)
        } else {
            let previous = &notes[note_index - 1];
            // A consonant may be labelled as the tail of the preceding GAME
            // note, but never search before that preceding vowel anchor.
            (note.start_sec - 0.320).max(previous.start_sec + 0.010)
        };
        let time_lower_frame = ((time_lower_bound * sample_rate as f64) / hop as f64)
            .floor()
            .clamp(0.0, alignment_frame as f64) as usize;
        let lower_frame = alignment_frame
            .saturating_sub(max_lookback_frames)
            .max(time_lower_frame);

        let mut earliest_active = alignment_frame;
        let mut inactive_run = 0usize;
        let mut found_silence_edge = false;
        for frame in (lower_frame..=alignment_frame).rev() {
            let feature = features[frame];
            // Sibilants and plosives can sit below the vowel RMS, so their
            // high-frequency/zero-crossing evidence lowers the activity gate.
            let noisy_consonant = feature.high_frequency_ratio > 0.42
                || feature.zero_crossing_ratio > 0.075;
            let threshold = if noisy_consonant {
                active_threshold * 0.68
            } else {
                active_threshold
            };
            if feature.rms >= threshold {
                earliest_active = frame;
                inactive_run = 0;
            } else {
                inactive_run += 1;
                if inactive_run >= inactive_stop_frames {
                    found_silence_edge = true;
                    break;
                }
            }
        }

        // In legato material there may be no silence. Select the strongest
        // attack/sibilant transition before the GAME principal/vowel anchor.
        if !found_silence_edge && alignment_frame > lower_frame + 2 {
            let mut best_frame = earliest_active;
            let mut best_score = 0.0f32;
            for frame in lower_frame + 1..alignment_frame.saturating_sub(1) {
                let feature = features[frame];
                let energy_gate = (feature.rms / active_threshold.max(1e-7)).clamp(0.0, 3.0);
                let score = feature.positive_flux / active_threshold.max(1e-7)
                    + feature.high_frequency_ratio * 0.42
                    + feature.zero_crossing_ratio * 1.8
                    + energy_gate * 0.08;
                if feature.rms >= active_threshold * 0.62 && score > best_score {
                    best_score = score;
                    best_frame = frame;
                }
            }
            if best_score > 0.42 {
                earliest_active = best_frame;
            }
        }

        let onset = (earliest_active * hop) as f64 / sample_rate as f64;
        result.push(onset.clamp(time_lower_bound, note.start_sec));
    }
    result
}

/// Add explicit silence and breath/noise events around GAME's note output.
/// GAME is a note segmenter, so its rest probability alone is not a reliable
/// silence detector. This lightweight acoustic post-pass combines adaptive RMS,
/// periodicity and zero-crossing density and is deterministic on every target.
fn detect_silence_and_breath_events(samples: &[f32], sample_rate: u32) -> Vec<SampleAudioEvent> {
    if samples.is_empty() || sample_rate == 0 {
        return Vec::new();
    }
    let window = ((sample_rate as f64 * 0.02).round() as usize).max(32);
    let hop = ((sample_rate as f64 * 0.01).round() as usize).max(16);
    if samples.len() < window {
        let rms = (samples.iter().map(|value| value * value).sum::<f32>()
            / samples.len().max(1) as f32)
            .sqrt();
        return (rms < 0.001).then(|| SampleAudioEvent {
            start_sec: 0.0,
            end_sec: samples.len() as f64 / sample_rate as f64,
            kind: SampleAudioEventKind::Silence,
            confidence: 1.0,
        }).into_iter().collect();
    }
    let mut rms_values = Vec::new();
    let mut starts = Vec::new();
    for start in (0..samples.len().saturating_sub(window).saturating_add(1)).step_by(hop) {
        let slice = &samples[start..start + window];
        let rms = (slice.iter().map(|value| value * value).sum::<f32>() / window as f32).sqrt();
        starts.push(start);
        rms_values.push(rms);
    }
    if rms_values.is_empty() {
        return Vec::new();
    }
    let peak = rms_values.iter().copied().fold(0.0f32, f32::max);
    let mut sorted = rms_values.clone();
    sorted.sort_by(f32::total_cmp);
    let floor = sorted[sorted.len() / 5];
    let silence_threshold = (floor * 2.5).max(peak * 0.012).max(0.0008);

    let labels: Vec<Option<SampleAudioEventKind>> = starts
        .iter()
        .zip(rms_values.iter())
        .map(|(&start, &rms)| {
            if rms < silence_threshold {
                return Some(SampleAudioEventKind::Silence);
            }
            let slice = &samples[start..start + window];
            let crossings = slice
                .windows(2)
                .filter(|pair| (pair[0] >= 0.0) != (pair[1] >= 0.0))
                .count() as f32
                / (window - 1).max(1) as f32;
            let time = start as f64 / sample_rate as f64;
            let (_, periodicity) = estimate_pitch(samples, sample_rate, time, 0.04);
            (periodicity < 0.35 && crossings > 0.055 && rms < (peak * 0.7).max(0.01))
                .then_some(SampleAudioEventKind::Breath)
        })
        .collect();

    let mut events = Vec::new();
    let mut index = 0usize;
    while index < labels.len() {
        let Some(kind) = labels[index] else {
            index += 1;
            continue;
        };
        let begin = index;
        while index < labels.len() && labels[index] == Some(kind) {
            index += 1;
        }
        let min_frames = match kind {
            SampleAudioEventKind::Silence => 4,
            SampleAudioEventKind::Breath => 5,
        };
        if index - begin < min_frames {
            continue;
        }
        let start_sec = starts[begin] as f64 / sample_rate as f64;
        let end_sample = (starts[index - 1] + window).min(samples.len());
        let end_sec = end_sample as f64 / sample_rate as f64;
        let confidence = match kind {
            SampleAudioEventKind::Silence => 1.0 - (rms_values[begin] / silence_threshold).min(1.0),
            SampleAudioEventKind::Breath => 0.75,
        };
        events.push(SampleAudioEvent { start_sec, end_sec, kind, confidence });
    }
    events
}

pub fn analyze_audio(audio_path: &Path) -> Result<SampleAnalysis, String> {
    if let Some(analysis) = cached_analysis(audio_path) {
        return Ok(analysis);
    }
    let analysis = analyze_audio_uncached(audio_path, false)?;
    cache_analysis(audio_path, &analysis);
    Ok(analysis)
}

pub fn reanalyze_audio(audio_path: &Path) -> Result<SampleAnalysis, String> {
    let analysis = analyze_audio_uncached(audio_path, false)?;
    cache_analysis(audio_path, &analysis);
    Ok(analysis)
}

pub fn convert_oto_path(path: &Path) -> Result<OtoConversionResult, String> {
    let oto_files = collect_oto_files(path)?;
    if oto_files.is_empty() {
        return Err(format!("no oto.ini found under {}", path.display()));
    }

    let mut by_audio: BTreeMap<PathBuf, Vec<SampleRegionAnnotation>> = BTreeMap::new();
    let mut warnings = Vec::new();
    for oto_path in &oto_files {
        let entries = match parse_oto_file(oto_path) {
            Ok(entries) => entries,
            Err(error) => {
                warnings.push(error);
                continue;
            }
        };
        let base = oto_path.parent().unwrap_or_else(|| Path::new("."));
        for entry in entries {
            let audio_path = base.join(&entry.wav);
            if !audio_path.is_file() {
                warnings.push(format!("missing UTAU sample: {}", audio_path.display()));
                continue;
            }
            let duration_sec = crate::audio_utils::try_read_wav_info(&audio_path, 16)
                .map(|info| info.duration_sec)
                .or_else(|| {
                    crate::audio_utils::decode_audio_f32_interleaved(&audio_path)
                        .ok()
                        .map(|(rate, channels, samples)| {
                            samples.len() as f64 / channels.max(1) as f64 / rate.max(1) as f64
                        })
                })
                .unwrap_or(0.0);
            if duration_sec <= 0.0 || !duration_sec.is_finite() {
                warnings.push(format!(
                    "could not determine UTAU sample duration: {}",
                    audio_path.display()
                ));
                continue;
            }
            let start = (entry.offset_ms / 1000.0).max(0.0);
            let end = if entry.cutoff_ms < 0.0 {
                start + (-entry.cutoff_ms / 1000.0)
            } else {
                duration_sec - entry.cutoff_ms / 1000.0
            }
            .clamp(start + 0.001, duration_sec.max(start + 0.001));
            by_audio
                .entry(audio_path)
                .or_default()
                .push(SampleRegionAnnotation {
                    name: entry.alias,
                    region_start_sec: start,
                    region_end_sec: end,
                    note_alignment_sec: (start + entry.preutter_ms / 1000.0).clamp(start, end),
                    fixed_duration_sec: (entry.consonant_ms / 1000.0).clamp(0.0, end - start),
                    relative_pitch_cents: 0.0,
                    melodyne_project_data: false,
                    melodyne_pitch_center_cents: 0.0,
                    melodyne_original_pitch_center_cents: 0.0,
                    melodyne_pitch_drift_factor: 1.0,
                    melodyne_pitch_modulation_factor: 1.0,
                    melodyne_transition_sec: 0.0,
                    melodyne_formant_offset_cents: 0.0,
                    melodyne_amplitude_factor: 1.0,
                    melodyne_sibilant_balance: 0.0,
                    melodyne_attack_duration_sec: 0.0,
                    melodyne_decay_elongation: 0.0,
                });
        }
    }

    let mut converted_samples = Vec::new();
    for (audio_path, rows) in by_audio {
        let rows = validate_annotations(&rows, None)?;
        let sidecar = write_sidecar(&audio_path, &rows)?;
        converted_samples.push(OtoConversionItem {
            audio_path: audio_path.display().to_string(),
            sidecar_path: sidecar.display().to_string(),
            annotation_count: rows.len(),
        });
    }

    Ok(OtoConversionResult {
        oto_files: oto_files.len(),
        converted_samples,
        warnings,
    })
}

fn collect_oto_files(path: &Path) -> Result<Vec<PathBuf>, String> {
    if path.is_file() {
        return Ok(
            if path
                .file_name()
                .and_then(|value| value.to_str())
                .map(|value| value.eq_ignore_ascii_case("oto.ini"))
                .unwrap_or(false)
            {
                vec![path.to_path_buf()]
            } else {
                Vec::new()
            },
        );
    }
    if !path.is_dir() {
        return Err(format!("path does not exist: {}", path.display()));
    }
    let mut out = Vec::new();
    collect_oto_files_recursive(path, &mut out)?;
    out.sort();
    Ok(out)
}

fn collect_oto_files_recursive(dir: &Path, out: &mut Vec<PathBuf>) -> Result<(), String> {
    let entries =
        fs::read_dir(dir).map_err(|error| format!("failed to read {}: {error}", dir.display()))?;
    for entry in entries {
        let entry = entry.map_err(|error| error.to_string())?;
        let path = entry.path();
        if path.is_dir() {
            collect_oto_files_recursive(&path, out)?;
        } else if path
            .file_name()
            .and_then(|value| value.to_str())
            .map(|value| value.eq_ignore_ascii_case("oto.ini"))
            .unwrap_or(false)
        {
            out.push(path);
        }
    }
    Ok(())
}

fn parse_oto_file(path: &Path) -> Result<Vec<OtoEntry>, String> {
    let bytes =
        fs::read(path).map_err(|error| format!("failed to read {}: {error}", path.display()))?;
    let shift_jis_text = SHIFT_JIS.decode(&bytes).0.into_owned();
    let utf8_declared = shift_jis_text.lines().take(10).any(|line| {
        let compact: String = line
            .trim()
            .to_ascii_lowercase()
            .chars()
            .filter(|ch| !ch.is_whitespace())
            .collect();
        compact.starts_with("#charset:utf-8") || compact.starts_with("#charset:utf8")
    });
    let text = if utf8_declared
        || (std::str::from_utf8(&bytes).is_ok() && bytes.starts_with(&[0xef, 0xbb, 0xbf]))
    {
        String::from_utf8_lossy(&bytes)
            .trim_start_matches('\u{feff}')
            .to_string()
    } else {
        shift_jis_text
    };

    let mut out = Vec::new();
    for (line_index, raw) in text.lines().enumerate() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let Some((wav, rhs)) = line.split_once('=') else {
            continue;
        };
        let fields: Vec<&str> = rhs.split(',').collect();
        let parse = |index: usize| -> Result<f64, String> {
            let raw = fields.get(index).copied().unwrap_or("").trim();
            if raw.is_empty() {
                Ok(0.0)
            } else {
                raw.parse::<f64>().map_err(|_| {
                    format!(
                        "{}:{} contains an invalid numeric oto field",
                        path.display(),
                        line_index + 1
                    )
                })
            }
        };
        let alias = fields.first().copied().unwrap_or("").trim();
        let fallback_alias = Path::new(wav.trim())
            .file_stem()
            .and_then(|value| value.to_str())
            .unwrap_or("sample");
        out.push(OtoEntry {
            wav: wav.trim().to_string(),
            alias: if alias.is_empty() {
                fallback_alias
            } else {
                alias
            }
            .to_string(),
            offset_ms: parse(1)?,
            consonant_ms: parse(2)?,
            cutoff_ms: parse(3)?,
            preutter_ms: parse(4)?,
        });
    }
    Ok(out)
}

fn parse_csv(text: &str) -> Result<Vec<SampleRegionAnnotation>, String> {
    let mut rows = Vec::new();
    let mut first_record = true;
    for (line_index, raw) in text.lines().enumerate() {
        let line = raw.trim_start_matches('\u{feff}').trim();
        if line.is_empty() {
            continue;
        }
        let fields = split_csv_line(line)?;
        if first_record
            && fields
                .first()
                .map(|value| value.eq_ignore_ascii_case("name"))
                .unwrap_or(false)
        {
            first_record = false;
            continue;
        }
        first_record = false;
        if fields.len() < 5 {
            return Err(format!(
                "CSV row {} has fewer than five fields",
                line_index + 1
            ));
        }
        let number = |index: usize| -> Result<f64, String> {
            fields[index].trim().parse::<f64>().map_err(|_| {
                format!(
                    "CSV row {} field {} is not a number",
                    line_index + 1,
                    index + 1
                )
            })
        };
        rows.push(SampleRegionAnnotation {
            name: fields[0].clone(),
            region_start_sec: number(1)?,
            region_end_sec: number(2)?,
            note_alignment_sec: number(3)?,
            fixed_duration_sec: number(4)?,
            relative_pitch_cents: if fields.len() >= 6 { number(5)? } else { 0.0 },
            melodyne_project_data: fields.get(6).map(|v| v.trim() == "1" || v.eq_ignore_ascii_case("true")).unwrap_or(false),
            melodyne_pitch_center_cents: if fields.len() >= 8 { number(7)? } else { 0.0 },
            melodyne_original_pitch_center_cents: if fields.len() >= 9 { number(8)? } else { 0.0 },
            melodyne_pitch_drift_factor: if fields.len() >= 10 { number(9)? } else { 1.0 },
            melodyne_pitch_modulation_factor: if fields.len() >= 11 { number(10)? } else { 1.0 },
            melodyne_transition_sec: if fields.len() >= 12 { number(11)? } else { 0.0 },
            melodyne_formant_offset_cents: if fields.len() >= 13 { number(12)? } else { 0.0 },
            melodyne_amplitude_factor: if fields.len() >= 14 { number(13)? } else { 1.0 },
            melodyne_sibilant_balance: if fields.len() >= 15 { number(14)? } else { 0.0 },
            melodyne_attack_duration_sec: if fields.len() >= 16 { number(15)? } else { 0.0 },
            melodyne_decay_elongation: if fields.len() >= 17 { number(16)? } else { 0.0 },
        });
    }
    validate_annotations(&rows, None)
}

fn split_csv_line(line: &str) -> Result<Vec<String>, String> {
    let mut out = Vec::new();
    let mut field = String::new();
    let mut chars = line.chars().peekable();
    let mut quoted = false;
    while let Some(ch) = chars.next() {
        match ch {
            '"' if quoted && chars.peek() == Some(&'"') => {
                field.push('"');
                chars.next();
            }
            '"' => quoted = !quoted,
            ',' if !quoted => {
                out.push(std::mem::take(&mut field));
            }
            _ => field.push(ch),
        }
    }
    if quoted {
        return Err("unterminated quoted CSV field".to_string());
    }
    out.push(field);
    Ok(out)
}

fn escape_csv(value: &str) -> String {
    if value.contains([',', '"', '\n', '\r']) {
        format!("\"{}\"", value.replace('"', "\"\""))
    } else {
        value.to_string()
    }
}

fn format_time(value: f64) -> String {
    let mut text = format!("{value:.6}");
    while text.ends_with('0') {
        text.pop();
    }
    if text.ends_with('.') {
        text.push('0');
    }
    text
}

fn interleaved_to_mono(input: &[f32], channels: usize) -> Vec<f32> {
    let channels = channels.max(1);
    input
        .chunks_exact(channels)
        .map(|frame| frame.iter().copied().sum::<f32>() / channels as f32)
        .collect()
}

fn estimate_pitch(
    samples: &[f32],
    sample_rate: u32,
    start_sec: f64,
    window_sec: f64,
) -> (f32, f32) {
    let start = (start_sec.max(0.0) * sample_rate as f64).round() as usize;
    let window = (window_sec * sample_rate as f64).round() as usize;
    if window < 64 || start + window > samples.len() {
        return (0.0, 0.0);
    }
    let slice = &samples[start..start + window];
    // Autocorrelation at the source rate is needlessly expensive for the
    // 65-800 Hz range.  Work at roughly 8 kHz while retaining enough samples
    // for reliable speech-pitch estimates.
    let stride = (sample_rate / 8_000).max(1) as usize;
    let effective_rate = sample_rate as f32 / stride as f32;
    let sample_count = slice.len().div_ceil(stride);
    let mean = slice.iter().step_by(stride).copied().sum::<f32>() / sample_count as f32;
    let energy = slice
        .iter()
        .step_by(stride)
        .map(|value| {
            let centered = *value - mean;
            centered * centered
        })
        .sum::<f32>();
    if energy <= 1e-7 {
        return (0.0, 0.0);
    }
    let min_lag = (effective_rate / 800.0).floor().max(1.0) as usize;
    let max_lag = (effective_rate / 65.0).ceil().max(min_lag as f32 + 1.0) as usize;
    let max_lag = max_lag.min(sample_count / 2);
    let mut best_lag = 0usize;
    let mut best = -1.0f32;
    for lag in min_lag..=max_lag {
        let mut cross = 0.0f32;
        let mut left_energy = 0.0f32;
        let mut right_energy = 0.0f32;
        for index in 0..sample_count - lag {
            let left = slice[index * stride] - mean;
            let right = slice[(index + lag) * stride] - mean;
            cross += left * right;
            left_energy += left * left;
            right_energy += right * right;
        }
        let correlation = cross / (left_energy * right_energy).sqrt().max(1e-12);
        if correlation > best {
            best = correlation;
            best_lag = lag;
        }
    }
    if best_lag == 0 || best <= 0.0 {
        (0.0, 0.0)
    } else {
        (effective_rate / best_lag as f32, best.clamp(0.0, 1.0))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn csv_round_trip_parser_supports_quoted_names() {
        let parsed = parse_csv(
            "name,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec\n\"a,b\",0.1,1.2,0.3,0.25\n",
        )
        .unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0].name, "a,b");
        assert_eq!(parsed[0].fixed_duration_sec, 0.25);
    }

    #[test]
    fn csv_accepts_bom_and_blank_line_before_header() {
        let parsed = parse_csv(
            "\u{feff}\nname,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec\na,0,1,0.2,0.1\n",
        )
        .unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0].name, "a");
    }

    #[test]
    fn parses_negative_cutoff_as_used_length() {
        let entry = OtoEntry {
            wav: "a.wav".into(),
            alias: "a".into(),
            offset_ms: 100.0,
            consonant_ms: 80.0,
            cutoff_ms: -500.0,
            preutter_ms: 120.0,
        };
        let start = entry.offset_ms / 1000.0;
        let end = start + (-entry.cutoff_ms / 1000.0);
        assert!((end - 0.6).abs() < 1e-9);
    }

    #[test]
    fn consonant_onset_precedes_game_beat_without_splitting_note() {
        let sample_rate = 16_000u32;
        let mut audio = vec![0.0f32; (sample_rate as f32 * 0.20) as usize];
        // Unvoiced consonant/attack from 200-280 ms.
        for index in 0..(sample_rate as f32 * 0.08) as usize {
            let sign = if index % 2 == 0 { 1.0 } else { -1.0 };
            audio.push(sign * 0.025 * (index as f32 / 64.0).min(1.0));
        }
        // Vowel starts at the authoritative GAME beat point (280 ms).
        for index in 0..(sample_rate as f32 * 0.30) as usize {
            let phase = index as f32 * 2.0 * std::f32::consts::PI * 220.0
                / sample_rate as f32;
            audio.push(phase.sin() * 0.12);
        }
        let notes = vec![SamplePitchNote {
            start_sec: 0.28,
            end_sec: 0.58,
            midi_note: 57.0,
            confidence: 1.0,
        }];
        let starts = detect_consonant_onsets_before_game_notes(&audio, sample_rate, &notes);
        assert_eq!(starts.len(), 1);
        assert!(starts[0] < notes[0].start_sec - 0.02);
        assert!(starts[0] >= 0.17);
    }
}
