use crate::sample_annotations::{self, SamplePitchNote, SampleRegionAnnotation};
use crate::state::AppState;
use serde::Serialize;
use std::path::Path;
use tauri::State;

const ACTIVE_ANNOTATION_PARAM: &str = "hachi_active_annotation";

#[derive(Debug, Serialize)]
#[serde(rename_all = "snake_case")]
struct ClipAnnotationPayload {
    ok: bool,
    clip_id: String,
    audio_path: String,
    sidecar_path: String,
    annotations: Vec<SampleRegionAnnotation>,
    pitch_notes: Vec<SamplePitchNote>,
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
    match sample_annotations::load_or_detect(Path::new(&source)) {
        Ok(analysis) => serde_json::to_value(ClipAnnotationPayload {
            ok: true,
            clip_id,
            audio_path: source.clone(),
            sidecar_path: sample_annotations::sidecar_path(Path::new(&source))
                .display()
                .to_string(),
            active_annotation_index: active_annotation_index(&clip, analysis.annotations.len()),
            annotations: analysis.annotations,
            pitch_notes: analysis.pitch_notes,
        })
        .unwrap_or_else(|error| serde_json::json!({"ok": false, "error": error.to_string()})),
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
    match sample_annotations::analyze_audio(Path::new(&source)) {
        Ok(analysis) => {
            let annotations = match sample_annotations::validate_annotations(
                &analysis.annotations,
                clip.duration_sec,
            ) {
                Ok(rows) => rows,
                Err(error) => return serde_json::json!({"ok": false, "error": error}),
            };
            serde_json::json!({
                "ok": true,
                "clip_id": clip_id,
                "audio_path": source,
                "sidecar_path": sample_annotations::sidecar_path(Path::new(&source)).display().to_string(),
                "annotations": annotations,
                "pitch_notes": analysis.pitch_notes,
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
