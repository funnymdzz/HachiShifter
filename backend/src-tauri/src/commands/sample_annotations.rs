use crate::sample_annotations::{
    self, NoteDetectorKind, SampleAudioEvent, SamplePitchNote, SampleRegionAnnotation,
};
use crate::state::AppState;
use serde::Serialize;
use std::path::Path;
use tauri::State;

const ACTIVE_ANNOTATION_PARAM: &str = "hachi_active_annotation";

fn apply_melodyne_note_controls(
    timeline: &mut crate::state::TimelineState,
    clip: &crate::state::Clip,
    annotations: &[SampleRegionAnnotation],
) {
    if clip.reversed || !annotations.iter().any(|row| row.melodyne_project_data) {
        return;
    }
    let Some(root) = timeline.resolve_root_track_id(&clip.track_id) else {
        return;
    };
    let Some(entry) = timeline.params_by_root_track.get_mut(&root) else { return; };
    if entry.pitch_edit.is_empty() { return; }
    let fp_sec = entry.frame_period_ms.max(0.1) / 1000.0;
    let rate = (clip.playback_rate as f64).max(1e-6);
    let first = (clip.start_sec.max(0.0) / fp_sec).floor().max(0.0) as usize;
    let last = ((clip.start_sec + clip.length_sec.max(0.0)) / fp_sec)
        .ceil().max(0.0) as usize;
    let curve_len = entry.pitch_edit.len();
    let mut updates = Vec::new();
    for frame in first..last.min(curve_len) {
        let timeline_sec = frame as f64 * fp_sec;
        let source_sec = clip.source_start_sec + (timeline_sec - clip.start_sec) * rate;
        let Some(row) = annotations.iter().find(|row| {
            row.melodyne_project_data
                && source_sec >= row.region_start_sec
                && source_sec <= row.region_end_sec
        }) else { continue; };
        let raw = entry.pitch_orig.get(frame).copied().unwrap_or(0.0) * 100.0;
        if raw <= 0.0 { continue; }
        let without = entry.extra_curves.get("mld5_pitch_without_vibrato")
            .and_then(|curve| curve.get(frame)).copied().unwrap_or(raw / 100.0) * 100.0;
        let original_center = if row.melodyne_original_pitch_center_cents > 0.0 {
            row.melodyne_original_pitch_center_cents as f32
        } else { raw };
        let edited = row.melodyne_pitch_center_cents as f32
            + row.melodyne_pitch_drift_factor as f32 * (without - original_center)
            + row.melodyne_pitch_modulation_factor as f32 * (raw - without);
        updates.push((
            frame,
            (edited / 100.0).clamp(0.0, 127.0),
            row.melodyne_formant_offset_cents as f32,
            row.melodyne_amplitude_factor as f32,
            row.melodyne_sibilant_balance as f32,
        ));
    }
    if updates.is_empty() { return; }
    entry.extra_curves.entry("formant_shift_cents".to_string())
        .or_insert_with(|| vec![0.0; curve_len]).resize(curve_len, 0.0);
    entry.extra_curves.entry("volume".to_string())
        .or_insert_with(|| vec![1.0; curve_len]).resize(curve_len, 1.0);
    entry.extra_curves.entry("mld5_sibilant_balance".to_string())
        .or_insert_with(|| vec![0.0; curve_len]).resize(curve_len, 0.0);
    for (frame, pitch, formant, volume, sibilant) in updates {
        entry.pitch_edit[frame] = pitch;
        entry.extra_curves.get_mut("formant_shift_cents").unwrap()[frame] = formant;
        entry.extra_curves.get_mut("volume").unwrap()[frame] = volume;
        entry.extra_curves.get_mut("mld5_sibilant_balance").unwrap()[frame] = sibilant;
    }
    // Do not infer or apply joins while saving source-note metadata. Explicit
    // Connect/Disconnect actions own pitch smoothing and operate on the small
    // selection centred on the chosen boundary.
    entry.pitch_edit_user_modified = true;
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "snake_case")]
struct ClipAnnotationPayload {
    ok: bool,
    clip_id: String,
    audio_path: String,
    sidecar_path: String,
    source_duration_sec: f64,
    annotations: Vec<SampleRegionAnnotation>,
    pitch_notes: Vec<SamplePitchNote>,
    audio_events: Vec<SampleAudioEvent>,
    note_detector: NoteDetectorKind,
    detector_message: Option<String>,
    active_annotation_index: usize,
}

fn find_clip_source(
    state: &AppState,
    clip_id: &str,
) -> Result<(crate::state::Clip, String), String> {
    let timeline = state
        .timeline
        .lock()
        .unwrap_or_else(|error| error.into_inner());
    let clip = timeline
        .clips
        .iter()
        .find(|clip| clip.id == clip_id)
        .cloned()
        .ok_or_else(|| format!("clip not found: {clip_id}"))?;
    let source = clip
        .source_path
        .clone()
        .ok_or_else(|| "selected clip has no audio source".to_string())?;
    Ok((clip, source))
}

fn active_annotation_index(clip: &crate::state::Clip, count: usize) -> usize {
    let index = clip
        .extra_params
        .as_ref()
        .and_then(|params| params.get(ACTIVE_ANNOTATION_PARAM))
        .copied()
        .unwrap_or(0.0)
        .round()
        .max(0.0) as usize;
    index.min(count.saturating_sub(1))
}

pub(super) fn get_clip_sample_annotations(
    state: State<'_, AppState>,
    clip_id: String,
) -> serde_json::Value {
    let (clip, source) = match find_clip_source(&state, &clip_id) {
        Ok(value) => value,
        Err(error) => return serde_json::json!({"ok": false, "error": error}),
    };
    // MPD clips must remain entirely graph-driven. Their note identity,
    // boundaries, timing handles and controls were already restored during
    // import; running GAME here would silently replace that information.
    let loaded = if clip.melodyne_warp_segments.is_empty() {
        sample_annotations::load_or_detect(Path::new(&source))
    } else {
        sample_annotations::melodyne_project_analysis(Path::new(&source))
            .ok_or_else(|| "Melodyne project note data is unavailable".to_string())
    };
    match loaded {
        Ok(analysis) => {
            let detected_end = analysis
                .annotations
                .iter()
                .map(|row| row.region_end_sec)
                .chain(analysis.pitch_notes.iter().map(|note| note.end_sec))
                .chain(analysis.audio_events.iter().map(|event| event.end_sec))
                .fold(0.0f64, f64::max);
            let source_duration_sec = clip
                .duration_sec
                .filter(|duration| duration.is_finite() && *duration > 0.0)
                .or_else(|| {
                    crate::audio_utils::try_read_wav_info(Path::new(&source), 16)
                        .map(|info| info.duration_sec)
                })
                .unwrap_or(detected_end)
                .max(detected_end);
            serde_json::to_value(ClipAnnotationPayload {
                ok: true,
                clip_id,
                audio_path: source.clone(),
                sidecar_path: sample_annotations::sidecar_path(Path::new(&source))
                    .display()
                    .to_string(),
                source_duration_sec,
                active_annotation_index: active_annotation_index(
                    &clip,
                    analysis.annotations.len(),
                ),
                annotations: analysis.annotations,
                pitch_notes: analysis.pitch_notes,
                audio_events: analysis.audio_events,
                note_detector: analysis.note_detector,
                detector_message: analysis.detector_message,
            })
            .unwrap_or_else(|error| serde_json::json!({"ok": false, "error": error.to_string()}))
        }
        Err(error) => serde_json::json!({"ok": false, "error": error}),
    }
}

pub(super) fn save_clip_sample_annotations(
    state: State<'_, AppState>,
    clip_id: String,
    annotations: Vec<SampleRegionAnnotation>,
    active_annotation_index: Option<usize>,
) -> serde_json::Value {
    let (clip, source) = match find_clip_source(&state, &clip_id) {
        Ok(value) => value,
        Err(error) => return serde_json::json!({"ok": false, "error": error}),
    };
    let duration = clip.duration_sec;
    let annotations = match sample_annotations::validate_annotations(&annotations, duration) {
        Ok(rows) if !rows.is_empty() => rows,
        Ok(_) => {
            return serde_json::json!({"ok": false, "error": "at least one annotation is required"})
        }
        Err(error) => return serde_json::json!({"ok": false, "error": error}),
    };
    let sidecar = match sample_annotations::write_sidecar(Path::new(&source), &annotations) {
        Ok(path) => path,
        Err(error) => return serde_json::json!({"ok": false, "error": error}),
    };
    sample_annotations::update_melodyne_project_annotations(Path::new(&source), &annotations);

    let selected_index = active_annotation_index
        .unwrap_or_else(|| active_annotation_index_from_clip(&clip))
        .min(annotations.len().saturating_sub(1));
    {
        let mut timeline = state
            .timeline
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        state.checkpoint_timeline(&timeline);
        if let Some(target) = timeline
            .clips
            .iter_mut()
            .find(|candidate| candidate.id == clip_id)
        {
            target
                .extra_params
                .get_or_insert_with(Default::default)
                .insert(ACTIVE_ANNOTATION_PARAM.to_string(), selected_index as f64);
        }
        apply_melodyne_note_controls(&mut timeline, &clip, &annotations);
        crate::synth_clip_cache::invalidate_clip_all_caches(&clip_id);
        state.audio_engine.update_timeline(timeline.clone());
    }
    state.bump_timeline_version();
    serde_json::json!({
        "ok": true,
        "clip_id": clip_id,
        "sidecar_path": sidecar.display().to_string(),
        "annotations": annotations,
        "active_annotation_index": selected_index,
    })
}

fn active_annotation_index_from_clip(clip: &crate::state::Clip) -> usize {
    clip.extra_params
        .as_ref()
        .and_then(|params| params.get(ACTIVE_ANNOTATION_PARAM))
        .copied()
        .unwrap_or(0.0)
        .round()
        .max(0.0) as usize
}

pub(super) fn redetect_clip_sample_annotations(
    state: State<'_, AppState>,
    clip_id: String,
) -> serde_json::Value {
    let (clip, source) = match find_clip_source(&state, &clip_id) {
        Ok(value) => value,
        Err(error) => return serde_json::json!({"ok": false, "error": error}),
    };
    let analysis_result = if clip.melodyne_warp_segments.is_empty() {
        sample_annotations::reanalyze_audio(Path::new(&source))
    } else {
        // "Redetect" on an MPD clip means reload its persisted Melodyne
        // objects. Acoustic GAME analysis is intentionally excluded.
        sample_annotations::melodyne_project_analysis(Path::new(&source))
            .ok_or_else(|| "Melodyne project note data is unavailable".to_string())
    };
    match analysis_result {
        Ok(analysis) => {
            let annotations = match sample_annotations::validate_annotations(
                &analysis.annotations,
                clip.duration_sec,
            ) {
                Ok(rows) => rows,
                Err(error) => return serde_json::json!({"ok": false, "error": error}),
            };
            let detected_end = annotations
                .iter()
                .map(|row| row.region_end_sec)
                .chain(analysis.pitch_notes.iter().map(|note| note.end_sec))
                .chain(analysis.audio_events.iter().map(|event| event.end_sec))
                .fold(0.0f64, f64::max);
            let source_duration_sec = clip.duration_sec.unwrap_or(detected_end).max(detected_end);
            serde_json::json!({
                "ok": true,
                "clip_id": clip_id,
                "audio_path": source,
                "sidecar_path": sample_annotations::sidecar_path(Path::new(&source)).display().to_string(),
                "source_duration_sec": source_duration_sec,
                "annotations": annotations,
                "pitch_notes": analysis.pitch_notes,
                "audio_events": analysis.audio_events,
                "note_detector": analysis.note_detector,
                "detector_message": analysis.detector_message,
                "active_annotation_index": 0,
            })
        }
        Err(error) => serde_json::json!({"ok": false, "error": error}),
    }
}

pub(super) fn convert_oto_to_annotations(oto_path: String) -> serde_json::Value {
    match sample_annotations::convert_oto_path(Path::new(&oto_path)) {
        Ok(result) => serde_json::json!({"ok": true, "result": result}),
        Err(error) => serde_json::json!({"ok": false, "error": error}),
    }
}

pub(super) fn open_oto_dialog() -> serde_json::Value {
    let picked = rfd::FileDialog::new()
        .add_filter("UTAU oto.ini", &["ini"])
        .pick_file();
    match picked {
        None => serde_json::json!({"ok": true, "canceled": true}),
        Some(path) => serde_json::json!({
            "ok": true,
            "canceled": false,
            "path": path.display().to_string(),
        }),
    }
}

pub(super) fn convert_oto_and_refresh_clip(
    state: State<'_, AppState>,
    clip_id: String,
    oto_path: String,
) -> serde_json::Value {
    let conversion = match sample_annotations::convert_oto_path(Path::new(&oto_path)) {
        Ok(result) => result,
        Err(error) => return serde_json::json!({"ok": false, "error": error}),
    };
    let payload = get_clip_sample_annotations(state, clip_id);
    serde_json::json!({"ok": true, "conversion": conversion, "clip": payload})
}
