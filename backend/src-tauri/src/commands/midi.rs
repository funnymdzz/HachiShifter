// MIDI 导入命令
//
// 提供两个 Tauri 命令：
// - get_midi_tracks: 解析 MIDI 文件并返回轨道列表（供前端轨道选择面板使用）
// - import_midi_to_pitch: 将选中的 MIDI 轨道音符写入 pitch_edit

use crate::midi_import::{self, MidiTrackInfo};
use crate::state::{AppState, PitchAnalysisAlgo, Track};

fn midi_log(message: impl AsRef<str>) {
    eprintln!("[midi_import] {}", message.as_ref());
}

fn error_payload(error: &str) -> crate::models::TimelineStatePayload {
    crate::models::TimelineStatePayload {
        ok: false,
        tracks: vec![],
        clips: vec![],
        created_clip_ids: None,
        selected_track_id: None,
        selected_clip_id: None,
        bpm: 120.0,
        playhead_sec: 0.0,
        project_sec: None,
        project: None,
        missing_files: Some(vec![error.to_string()]),
    }
}

fn validate_midi_import_target(track: &Track) -> Result<(), &'static str> {
    if !track.compose_enabled {
        return Err("pitch_requires_compose");
    }

    if matches!(track.pitch_analysis_algo, PitchAnalysisAlgo::None) {
        return Err("pitch_requires_algo");
    }

    Ok(())
}

/// 读取 MIDI 文件并返回轨道摘要列表。
pub(super) fn get_midi_tracks(midi_path: String) -> serde_json::Value {
    let path = std::path::Path::new(&midi_path);
    midi_log(format!("get_midi_tracks: path={midi_path}"));

    if !path.exists() {
        midi_log("get_midi_tracks: file_not_found");
        return serde_json::json!({"ok": false, "error": "file_not_found"});
    }

    match midi_import::parse_midi_file(path, None) {
        Ok(result) => {
            // 只返回有音符的轨道
            let tracks_with_notes: Vec<&MidiTrackInfo> =
                result.tracks.iter().filter(|t| t.note_count > 0).collect();

            midi_log(format!(
                "get_midi_tracks: parsed tracks_total={} tracks_with_notes={}",
                result.tracks.len(),
                tracks_with_notes.len()
            ));

            serde_json::json!({
                "ok": true,
                "tracks": tracks_with_notes,
            })
        }
        Err(e) => {
            midi_log(format!("get_midi_tracks: parse_error={e}"));
            serde_json::json!({"ok": false, "error": e})
        }
    }
}

/// 将 MIDI 文件中指定轨道的音符写入当前选中根轨的 pitch_edit。
///
/// 导入逻辑与 `paste_midi_clipboard_inner`（Reaper 剪贴板 Standard MIDI File）完全一致：
/// - 使用工程 BPM 作为 Tempo 回退
/// - 偏移量为光标位置或选区起始帧对应秒，**第一个音符对齐该偏移量**（即所有音符整体平移）
/// - 支持选区约束（selection_start_frame / selection_max_frames）
pub(super) fn import_midi_to_pitch(
    state: &AppState,
    midi_path: String,
    track_index: Option<usize>,
    selection_start_frame: Option<usize>,
    selection_max_frames: Option<usize>,
) -> serde_json::Value {
    let path = std::path::Path::new(&midi_path);
    midi_log(format!(
        "import_midi_to_pitch: path={} track_index={:?} sel_start={:?} sel_max={:?}",
        midi_path, track_index, selection_start_frame, selection_max_frames
    ));

    if !path.exists() {
        midi_log("import_midi_to_pitch: file_not_found");
        return serde_json::json!({"ok": false, "error": "file_not_found"});
    }

    // 先锁 timeline 读取 bpm / playhead / 选中轨道等信息（与 paste_midi_clipboard_inner 一致）
    let mut tl = state.timeline.lock().unwrap_or_else(|e| e.into_inner());

    let bpm = tl.bpm;
    let playhead_sec = tl.playhead_sec;
    let frame_period_ms_raw = tl.frame_period_ms().max(0.1);

    // 使用工程 BPM 作为 fallback tempo（与 Reaper 剪贴板路径一致）
    let parse_result = match midi_import::parse_midi_file(path, Some(bpm)) {
        Ok(r) => r,
        Err(e) => {
            midi_log(format!("import_midi_to_pitch: parse_error={e}"));
            return serde_json::json!({"ok": false, "error": e});
        }
    };

    // 收集要写入的音符：如果指定了 track_index 则只取该轨道，否则合并所有轨道
    let notes: Vec<midi_import::MidiNoteEvent> = match track_index {
        Some(idx) => {
            if idx >= parse_result.track_notes.len() {
                midi_log(format!(
                    "import_midi_to_pitch: track_index_out_of_range idx={} available={}",
                    idx,
                    parse_result.track_notes.len()
                ));
                return serde_json::json!({"ok": false, "error": "track_index_out_of_range"});
            }
            parse_result.track_notes[idx].clone()
        }
        None => {
            // 合并所有轨道的音符（与 paste_midi_clipboard_inner 一致）
            let mut all_notes: Vec<midi_import::MidiNoteEvent> =
                parse_result.track_notes.into_iter().flatten().collect();
            all_notes.sort_by(|a, b| {
                a.start_sec
                    .partial_cmp(&b.start_sec)
                    .unwrap_or(std::cmp::Ordering::Equal)
            });
            all_notes
        }
    };

    if notes.is_empty() {
        midi_log("import_midi_to_pitch: no_notes_in_track");
        return serde_json::json!({"ok": false, "error": "no_notes_in_track"});
    }

    midi_log(format!(
        "import_midi_to_pitch: notes_selected={} first_start={:.3} last_end={:.3}",
        notes.len(),
        notes.first().map(|n| n.start_sec).unwrap_or(0.0),
        notes.last().map(|n| n.end_sec).unwrap_or(0.0),
    ));

    // 确定目标轨道
    let Some(selected_track_id) = tl.selected_track_id.clone() else {
        midi_log("import_midi_to_pitch: no_pitch_line_selected (selected_track_id missing)");
        return serde_json::json!({"ok": false, "error": "no_pitch_line_selected"});
    };

    let Some(root_track_id) = tl.resolve_root_track_id(&selected_track_id) else {
        midi_log(format!(
            "import_midi_to_pitch: no_pitch_line_selected (resolve_root_track_id failed for selected_track_id={})",
            selected_track_id
        ));
        return serde_json::json!({"ok": false, "error": "no_pitch_line_selected"});
    };

    let Some(root_track) = tl.tracks.iter().find(|track| track.id == root_track_id) else {
        midi_log(format!(
            "import_midi_to_pitch: no_pitch_line_selected (root_track missing root_track_id={})",
            root_track_id
        ));
        return serde_json::json!({"ok": false, "error": "no_pitch_line_selected"});
    };

    if let Err(error) = validate_midi_import_target(root_track) {
        midi_log(format!(
            "import_midi_to_pitch: validation_failed error={error}"
        ));
        return serde_json::json!({"ok": false, "error": error});
    }

    tl.ensure_params_for_root(&root_track_id);
    let frame_period_ms = tl.frame_period_ms().max(0.1);

    state.checkpoint_timeline(&tl);

    let Some(entry) = tl.params_by_root_track.get_mut(&root_track_id) else {
        midi_log(format!(
            "import_midi_to_pitch: params_missing root_track_id={}",
            root_track_id
        ));
        return serde_json::json!({"ok": false, "error": "params_missing"});
    };

    // 根据是否有选区约束决定偏移和写入范围（与 paste_midi_clipboard_inner 完全一致）
    let touched = if let Some(sel_start) = selection_start_frame {
        // 以选区起始帧对应秒作为目标偏移，所有音符整体平移使第一个音符对齐该偏移
        let offset_sec = (sel_start as f64 * frame_period_ms_raw) / 1000.0;
        let first_start = notes
            .iter()
            .map(|n| n.start_sec)
            .fold(f64::INFINITY, f64::min);
        let align_offset = offset_sec - first_start;
        let max_frame = sel_start + selection_max_frames.unwrap_or(usize::MAX - sel_start);
        let clamp_len = max_frame.min(entry.pitch_edit.len());
        midi_log(format!(
            "import_midi_to_pitch: selection mode offset_sec={:.3} align_offset={:.3} clamp_len={}",
            offset_sec, align_offset, clamp_len
        ));
        // 先清除目标范围，避免已有编辑阻挡新导入的 MIDI 音符
        midi_import::clear_pitch_edit_range_for_notes(
            &notes,
            frame_period_ms,
            &mut entry.pitch_edit[..clamp_len],
            align_offset,
        );
        midi_import::write_notes_to_pitch_edit(
            &notes,
            frame_period_ms,
            &mut entry.pitch_edit[..clamp_len],
            align_offset,
        )
    } else {
        // 以光标位置为目标偏移，所有音符整体平移使第一个音符对齐该偏移
        let first_start = notes
            .iter()
            .map(|n| n.start_sec)
            .fold(f64::INFINITY, f64::min);
        let align_offset = playhead_sec - first_start;
        midi_log(format!(
            "import_midi_to_pitch: playhead mode offset_sec={:.3} align_offset={:.3}",
            playhead_sec, align_offset
        ));
        // 先清除目标范围，避免已有编辑阻挡新导入的 MIDI 音符
        midi_import::clear_pitch_edit_range_for_notes(
            &notes,
            frame_period_ms,
            &mut entry.pitch_edit,
            align_offset,
        );
        midi_import::write_notes_to_pitch_edit(
            &notes,
            frame_period_ms,
            &mut entry.pitch_edit,
            align_offset,
        )
    };

    if touched > 0 {
        entry.pitch_edit_user_modified = true;
        midi_log(format!(
            "import_midi_to_pitch: success frames_touched={} notes_imported={}",
            touched,
            notes.len()
        ));
    } else {
        midi_log(format!(
            "import_midi_to_pitch: no_frames_touched notes={} pitch_edit_len={} frame_period_ms={:.3}",
            notes.len(), entry.pitch_edit.len(), frame_period_ms
        ));
        return serde_json::json!({"ok": false, "error": "no_frames_touched"});
    }

    state.audio_engine.update_timeline(tl.clone());

    serde_json::json!({
        "ok": true,
        "notes_imported": notes.len(),
        "frames_touched": touched,
    })
}

/// 导入 MIDI 文件为时间线上的 MIDI clip（无音频源）。
///
/// 创建一���特殊的 clip，其中 `source_path` 为 None，`midi_note_data` 包含
/// 从 MIDI 文件提取的音符事件。clip 的长度由 MIDI 中最后一个音符的结束时间决定。
///
/// 返回完整的 timeline state payload，以便前端更新 Redux store。
pub(super) fn import_midi_as_clip(
    state: &AppState,
    midi_path: String,
    track_index: Option<usize>,
    track_id: Option<String>,
    start_sec: f64,
) -> crate::models::TimelineStatePayload {
    midi_log(format!(
        "import_midi_as_clip: path={} track_index={:?} track_id={:?} start_sec={:.3}",
        midi_path, track_index, track_id, start_sec
    ));

    let path = std::path::Path::new(&midi_path);
    if !path.exists() {
        midi_log("import_midi_as_clip: file_not_found");
        return error_payload("file_not_found");
    }

    let mut tl = state.timeline.lock().unwrap_or_else(|e| e.into_inner());
    let bpm = tl.bpm;

    let parse_result = match midi_import::parse_midi_file(path, Some(bpm)) {
        Ok(r) => r,
        Err(e) => {
            midi_log(format!("import_midi_as_clip: parse_error={}", e));
            return error_payload(&e);
        }
    };

    // 收集指定轨道（或合并所有轨道）的音符
    let notes: Vec<midi_import::MidiNoteEvent> = match track_index {
        Some(idx) => {
            if idx >= parse_result.track_notes.len() {
                midi_log(format!("import_midi_as_clip: track_index_out_of_range idx={}", idx));
                return error_payload("track_index_out_of_range");
            }
            parse_result.track_notes[idx].clone()
        }
        None => {
            let mut all: Vec<_> = parse_result.track_notes.into_iter().flatten().collect();
            all.sort_by(|a, b| {
                a.start_sec
                    .partial_cmp(&b.start_sec)
                    .unwrap_or(std::cmp::Ordering::Equal)
            });
            all
        }
    };

    if notes.is_empty() {
        midi_log("import_midi_as_clip: no_notes");
        return error_payload("no_notes_in_track");
    }

    // 计算 clip 时长 = 最后一个音符的结束时间
    let last_end = notes.iter().map(|n| n.end_sec).fold(0.0f64, f64::max);
    let length_sec = last_end.max(0.1);

    // 音符时间归一化：使第一个音符从时间 0 开始
    let first_start = notes
        .iter()
        .map(|n| n.start_sec)
        .fold(f64::INFINITY, f64::min);
    let normalized_notes: Vec<midi_import::MidiNoteEvent> = notes
        .into_iter()
        .map(|n| midi_import::MidiNoteEvent {
            start_sec: n.start_sec - first_start,
            end_sec: n.end_sec - first_start,
            note: n.note,
            velocity: n.velocity,
        })
        .collect();

    // 计算音高范围
    let pitch_range = {
        let min_note = normalized_notes.iter().map(|n| n.note).fold(127u8, u8::min);
        let max_note = normalized_notes.iter().map(|n| n.note).fold(0u8, u8::max);
        Some(crate::models::PitchRange {
            min: min_note as f32,
            max: max_note as f32,
        })
    };

    let name = path
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("MIDI")
        .to_string();

    state.checkpoint_timeline(&tl);

    let clip_id = tl.add_clip(
        track_id,
        Some(name.clone()),
        Some(start_sec),
        Some(length_sec),
        None, // MIDI clip 无 source_path
    );

    // 设置 MIDI 专属字段
    if let Some(clip) = tl.clips.iter_mut().find(|c| c.id == clip_id) {
        clip.midi_note_data = Some(normalized_notes);
        clip.pitch_range = pitch_range;
        clip.color = "cyan".to_string();
        clip.source_path = None;
        clip.source_path_relative = None;
    }

    midi_log(format!(
        "import_midi_as_clip: created clip_id={} length_sec={:.3} notes={}",
        clip_id,
        length_sec,
        tl.clips
            .iter()
            .find(|c| c.id == clip_id)
            .and_then(|c| c.midi_note_data.as_ref())
            .map(|n| n.len())
            .unwrap_or(0)
    ));

    state.audio_engine.update_timeline(tl.clone());
    let mut payload = tl.to_payload();
    payload.project = Some(state.project_meta_payload());
    payload
}
