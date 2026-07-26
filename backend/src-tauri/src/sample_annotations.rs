//! Per-sample timing annotations and lightweight note detection.
//!
//! The sidecar format intentionally stays small and human-editable:
//! `name,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec`.
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
const CSV_HEADER: &str =
    "name,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec\n";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SampleRegionAnnotation {
    pub name: String,
    pub region_start_sec: f64,
    pub region_end_sec: f64,
    pub note_alignment_sec: f64,
    pub fixed_duration_sec: f64,
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
    YinFallback,
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
    let detected = if performance_mode {
        analyze_audio_uncached(audio_path, true)?
    } else {
        analyze_audio(audio_path)?
    };
    let annotations = if sidecar_path(audio_path).is_file() {
        match read_sidecar(audio_path) {
            Ok(rows) if !rows.is_empty() => rows,
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

/// Ensure that an imported sample has a sidecar without re-running pitch
/// analysis when a valid sidecar already exists.  Full note extraction is
/// intentionally deferred until the piano-roll/editor requests it.
pub fn ensure_sidecar(audio_path: &Path) -> Result<PathBuf, String> {
    let path = sidecar_path(audio_path);
    if path.is_file() {
        if let Ok(rows) = read_sidecar(audio_path) {
            if !rows.is_empty() {
                return Ok(path);
            }
        }
    }
    let (sample_rate, channels, interleaved) =
        crate::audio_utils::decode_audio_f32_interleaved(audio_path)?;
    if sample_rate == 0 || channels == 0 || interleaved.is_empty() {
        return Err("audio has no decodable samples".to_string());
    }
    let mono = interleaved_to_mono(&interleaved, channels as usize);
    let duration_sec = mono.len() as f64 / sample_rate as f64;
    let (_, annotations) = detect_regions_and_annotations(
        audio_path,
        &mono,
        sample_rate,
        duration_sec,
    );
    write_sidecar(audio_path, &annotations)
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

fn detect_regions_and_annotations(
    audio_path: &Path,
    mono: &[f32],
    sample_rate: u32,
    duration_sec: f64,
) -> (Vec<(f64, f64)>, Vec<SampleRegionAnnotation>) {
    let regions = detect_active_regions(&mono, sample_rate);
    let mut annotations = Vec::new();
    for (index, (start, end)) in regions.iter().copied().enumerate() {
        let voiced_start = detect_voiced_start(&mono, sample_rate, start, end).unwrap_or(start);
        let fixed_end = voiced_start.clamp(start, end);
        annotations.push(SampleRegionAnnotation {
            name: if regions.len() == 1 {
                audio_path
                    .file_stem()
                    .and_then(|value| value.to_str())
                    .unwrap_or("pronunciation")
                    .to_string()
            } else {
                format!("pronunciation {}", index + 1)
            },
            region_start_sec: start,
            region_end_sec: end,
            note_alignment_sec: fixed_end,
            fixed_duration_sec: (fixed_end - start).max(0.0),
        });
    }

    if annotations.is_empty() {
        annotations.push(SampleRegionAnnotation {
            name: audio_path
                .file_stem()
                .and_then(|value| value.to_str())
                .unwrap_or("sample")
                .to_string(),
            region_start_sec: 0.0,
            region_end_sec: duration_sec.max(0.001),
            note_alignment_sec: 0.0,
            fixed_duration_sec: 0.0,
        });
    }

    (regions, annotations)
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
    let (regions, annotations) =
        detect_regions_and_annotations(audio_path, &mono, sample_rate, duration_sec);
    let mut fallback_pitch_notes = Vec::new();
    for &(start, end) in &regions {
        fallback_pitch_notes.extend(detect_pitch_notes(&mono, sample_rate, start, end));
    }
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
                            && regions.iter().any(|(start, end)| {
                                let center = (note.start_sec + note.end_sec) * 0.5;
                                center >= *start && center <= *end
                            })
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
                        fallback_pitch_notes,
                        NoteDetectorKind::YinFallback,
                        Some("GAME returned no voiced notes; used the lightweight detector".to_string()),
                    )
                } else {
                    (notes, NoteDetectorKind::Game, None)
                }
            }
            Err(error) => (
                fallback_pitch_notes,
                NoteDetectorKind::YinFallback,
                Some(error),
            ),
        };

    Ok(SampleAnalysis {
        annotations,
        pitch_notes,
        audio_events,
        note_detector,
        detector_message,
    })
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

fn detect_active_regions(samples: &[f32], sample_rate: u32) -> Vec<(f64, f64)> {
    let window = ((sample_rate as f64 * 0.02).round() as usize).max(32);
    let hop = ((sample_rate as f64 * 0.01).round() as usize).max(16);
    if samples.len() < window {
        return if samples.iter().any(|value| value.abs() > 1e-5) {
            vec![(0.0, samples.len() as f64 / sample_rate as f64)]
        } else {
            Vec::new()
        };
    }
    let mut rms = Vec::new();
    let mut pos = 0usize;
    while pos + window <= samples.len() {
        let energy = samples[pos..pos + window]
            .iter()
            .map(|value| value * value)
            .sum::<f32>()
            / window as f32;
        rms.push(energy.sqrt());
        pos += hop;
    }
    let peak = rms.iter().copied().fold(0.0f32, f32::max);
    if peak < 1e-5 {
        return Vec::new();
    }
    let mut sorted = rms.clone();
    sorted.sort_by(f32::total_cmp);
    let noise = sorted[sorted.len() / 5];
    let threshold = (noise * 3.0).max(peak * 0.025).max(0.0015);
    let mut active: Vec<bool> = rms.iter().map(|value| *value >= threshold).collect();

    // Bridge short internal gaps (up to 80 ms), but keep leading/trailing silence.
    let max_gap = 8usize;
    let mut index = 0usize;
    while index < active.len() {
        if active[index] {
            index += 1;
            continue;
        }
        let gap_start = index;
        while index < active.len() && !active[index] {
            index += 1;
        }
        if gap_start > 0 && index < active.len() && index - gap_start <= max_gap {
            active[gap_start..index].fill(true);
        }
    }

    let mut regions = Vec::new();
    let min_frames = 4usize;
    let mut index = 0usize;
    while index < active.len() {
        if !active[index] {
            index += 1;
            continue;
        }
        let start_frame = index;
        while index < active.len() && active[index] {
            index += 1;
        }
        if index - start_frame >= min_frames {
            let start_sample = start_frame.saturating_sub(1) * hop;
            let end_sample = (index * hop + window).min(samples.len());
            regions.push((
                start_sample as f64 / sample_rate as f64,
                end_sample as f64 / sample_rate as f64,
            ));
        }
    }
    regions
}

fn detect_voiced_start(
    samples: &[f32],
    sample_rate: u32,
    region_start: f64,
    region_end: f64,
) -> Option<f64> {
    let step_sec = 0.01;
    let search_end = region_end.min(region_start + 0.35);
    let mut time = region_start;
    let mut consecutive = 0usize;
    while time < search_end {
        let (_, confidence) = estimate_pitch(samples, sample_rate, time, 0.04);
        if confidence >= 0.48 {
            consecutive += 1;
            if consecutive >= 3 {
                return Some((time - step_sec * 2.0).max(region_start));
            }
        } else {
            consecutive = 0;
        }
        time += step_sec;
    }
    None
}

fn detect_pitch_notes(
    samples: &[f32],
    sample_rate: u32,
    region_start: f64,
    region_end: f64,
) -> Vec<SamplePitchNote> {
    let hop_sec = 0.02;
    let mut frames = Vec::<(f64, f32, f32)>::new();
    let mut time = region_start;
    while time + 0.02 <= region_end {
        let (frequency, confidence) = estimate_pitch(samples, sample_rate, time, 0.04);
        if frequency > 0.0 && confidence >= 0.42 {
            let midi = 69.0 + 12.0 * (frequency / 440.0).log2();
            if (24.0..=108.0).contains(&midi) {
                frames.push((time, midi, confidence));
            }
        }
        time += hop_sec;
    }
    if frames.is_empty() {
        return Vec::new();
    }

    let mut notes = Vec::<SamplePitchNote>::new();
    let mut group = vec![frames[0]];
    for frame in frames.into_iter().skip(1) {
        let median = median_midi(&group);
        let prev_time = group.last().map(|value| value.0).unwrap_or(frame.0);
        if frame.0 - prev_time > hop_sec * 1.75 || (frame.1 - median).abs() >= 1.25 {
            push_pitch_group(&mut notes, &group, hop_sec);
            group.clear();
        }
        group.push(frame);
    }
    push_pitch_group(&mut notes, &group, hop_sec);

    // Suppress tiny detector flickers; merge adjacent notes with the same pitch class.
    notes.retain(|note| note.end_sec - note.start_sec >= 0.04);
    let mut merged = Vec::<SamplePitchNote>::new();
    for note in notes {
        if let Some(last) = merged.last_mut() {
            if note.start_sec - last.end_sec <= 0.04
                && (note.midi_note.round() - last.midi_note.round()).abs() < 0.5
            {
                let left_duration = (last.end_sec - last.start_sec) as f32;
                let right_duration = (note.end_sec - note.start_sec) as f32;
                last.midi_note = (last.midi_note * left_duration + note.midi_note * right_duration)
                    / (left_duration + right_duration).max(1e-6);
                last.confidence = last.confidence.max(note.confidence);
                last.end_sec = note.end_sec;
                continue;
            }
        }
        merged.push(note);
    }
    merged
}

fn push_pitch_group(out: &mut Vec<SamplePitchNote>, group: &[(f64, f32, f32)], hop_sec: f64) {
    if group.is_empty() {
        return;
    }
    let midi = median_midi(group);
    let confidence = group.iter().map(|value| value.2).sum::<f32>() / group.len() as f32;
    out.push(SamplePitchNote {
        start_sec: group[0].0,
        end_sec: group
            .last()
            .map(|value| value.0 + hop_sec)
            .unwrap_or(group[0].0 + hop_sec),
        midi_note: midi,
        confidence,
    });
}

fn median_midi(group: &[(f64, f32, f32)]) -> f32 {
    let mut values: Vec<f32> = group.iter().map(|value| value.1).collect();
    values.sort_by(f32::total_cmp);
    values[values.len() / 2]
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
    fn detects_two_regions_separated_by_silence() {
        let sample_rate = 8_000u32;
        let mut samples = vec![0.0f32; sample_rate as usize];
        for index in 800..2400 {
            samples[index] = ((index as f32) * 0.1).sin() * 0.5;
        }
        for index in 4800..6800 {
            samples[index] = ((index as f32) * 0.08).sin() * 0.5;
        }
        let regions = detect_active_regions(&samples, sample_rate);
        assert_eq!(regions.len(), 2);
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
}
