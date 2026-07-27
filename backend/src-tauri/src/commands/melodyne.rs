use crate::melodyne_correction::MelodyneCorrectionSettings;
use crate::state::AppState;
use std::path::Path;
use tauri::State;

pub(super) fn get_game_status() -> serde_json::Value {
    serde_json::to_value(crate::game_detector::status())
        .unwrap_or_else(|error| serde_json::json!({"available": false, "message": error.to_string()}))
}

pub(super) fn apply_melodyne_correction(
    state: State<'_, AppState>,
    clip_id: String,
    settings: MelodyneCorrectionSettings,
) -> serde_json::Value {
    let (clip, use_game_fcpe) = {
        let timeline = state
            .timeline
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        match timeline.clips.iter().find(|clip| clip.id == clip_id) {
            Some(clip) => {
                let use_game_fcpe = timeline
                    .resolve_root_track_id(&clip.track_id)
                    .and_then(|root| timeline.params_by_root_track.get(&root))
                    .and_then(|params| params.extra_params.get("mld5_pitch_source"))
                    .copied()
                    .unwrap_or(0.0)
                    >= 0.5;
                (clip.clone(), use_game_fcpe)
            }
            None => {
                return serde_json::json!({"ok": false, "error": format!("clip not found: {clip_id}")})
            }
        }
    };
    let Some(source) = clip.source_path.as_deref() else {
        return serde_json::json!({"ok": false, "error": "selected clip has no audio source"});
    };
    let analysis_result = if clip.melodyne_warp_segments.is_empty() || use_game_fcpe {
        crate::sample_annotations::load_or_detect_with_game_mode(
            Path::new(source),
            settings.performance_mode,
        )
    } else {
        crate::sample_annotations::melodyne_project_analysis(Path::new(source))
            .ok_or_else(|| "Melodyne project note data is unavailable".to_string())
    };
    let analysis = match analysis_result {
        Ok(analysis) => analysis,
        Err(error) => return serde_json::json!({"ok": false, "error": error}),
    };

    let mut timeline = state
        .timeline
        .lock()
        .unwrap_or_else(|error| error.into_inner());
    let snapshot = timeline.clone();
    match crate::melodyne_correction::apply(&mut timeline, &clip, &analysis, settings) {
        Ok(summary) => {
            state.checkpoint_timeline(&snapshot);
            state.audio_engine.update_timeline(timeline.clone());
            serde_json::json!({"ok": true, "summary": summary})
        }
        Err(error) => serde_json::json!({"ok": false, "error": error}),
    }
}
