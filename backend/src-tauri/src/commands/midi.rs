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
        created_track_ids: None,
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
    track_indices: Vec<usize>,
    selection_start_frame: Option<usize>,
    selection_max_frames: Option<usize>,
    fill_gaps: Option<bool>,
) -> serde_json::Value {
    let path = std::path::Path::new(&midi_path);
    midi_log(format!(
        "import_midi_to_pitch: path={} track_indices={:?} sel_start={:?} sel_max={:?} fill_gaps={:?}",
        midi_path, track_indices, selection_start_frame, selection_max_frames, fill_gaps
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

    // 收集要写入的音符：合并所有选中轨道的音符
    let notes: Vec<midi_import::MidiNoteEvent> = {
        let mut all: Vec<midi_import::MidiNoteEvent> = if track_indices.is_empty() {
            // 未指定轨道则合并所有轨道
            parse_result.track_notes.into_iter().flatten().collect()
        } else {
            track_indices
                .iter()
                .filter_map(|&idx| parse_result.track_notes.get(idx))
                .flatten()
                .cloned()
                .collect()
        };
        if all.is_empty() {
            midi_log("import_midi_to_pitch: no_notes_in_track");
            return serde_json::json!({"ok": false, "error": "no_notes_in_track"});
        }
        all.sort_by(|a, b| {
            a.start_sec
                .partial_cmp(&b.start_sec)
                .unwrap_or(std::cmp::Ordering::Equal)
        });
        all
    };

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

    // 计算对齐偏移量和写入范围
    let first_start = notes
        .iter()
        .map(|n| n.start_sec)
        .fold(f64::INFINITY, f64::min);
    let (align_offset, clamp_range_end) = if let Some(sel_start) = selection_start_frame {
        let offset_sec = (sel_start as f64 * frame_period_ms_raw) / 1000.0;
        let ao = offset_sec - first_start;
        let max_frame = sel_start + selection_max_frames.unwrap_or(usize::MAX - sel_start);
        let cl = max_frame.min(entry.pitch_edit.len());
        midi_log(format!(
            "import_midi_to_pitch: selection mode offset_sec={:.3} align_offset={:.3} clamp_len={}",
            offset_sec, ao, cl
        ));
        (ao, Some(cl))
    } else {
        let ao = playhead_sec - first_start;
        midi_log(format!(
            "import_midi_to_pitch: playhead mode offset_sec={:.3} align_offset={:.3}",
            playhead_sec, ao
        ));
        (ao, None)
    };

    let target_slice = if let Some(clamp_len) = clamp_range_end {
        &mut entry.pitch_edit[..clamp_len]
    } else {
        &mut entry.pitch_edit[..]
    };

    // 先清除目标范围，避免已有编辑阻挡新导入的 MIDI 音符
    midi_import::clear_pitch_edit_range_for_notes(
        &notes,
        frame_period_ms,
        target_slice,
        align_offset,
    );
    let touched = midi_import::write_notes_to_pitch_edit(
        &notes,
        frame_period_ms,
        target_slice,
        align_offset,
    );

    // 填补音符之间的空隙（仅在导入的音符范围内）
    if fill_gaps.unwrap_or(false) {
        // 计算导入音符的实际帧范围，避免 fill_gaps_in_pitch_edit
        // 在已有非零音高值的历史编辑区域产生意外的填充
        let mut min_frame = usize::MAX;
        let mut max_frame = 0usize;
        for note in &notes {
            let start_sec = note.start_sec + align_offset;
            let end_sec = note.end_sec + align_offset;
            if start_sec < 0.0 || !start_sec.is_finite() || !end_sec.is_finite() {
                continue;
            }
            let sf = ((start_sec * 1000.0) / frame_period_ms).round() as usize;
            let ef = ((end_sec * 1000.0) / frame_period_ms).round() as usize;
            if sf < entry.pitch_edit.len() {
                min_frame = min_frame.min(sf);
                max_frame = max_frame.max(ef.min(entry.pitch_edit.len()));
            }
        }
        if min_frame < max_frame && max_frame <= entry.pitch_edit.len() {
            let filled =
                midi_import::fill_gaps_in_pitch_edit(&mut entry.pitch_edit[min_frame..max_frame]);
            if filled > 0 {
                midi_log(format!("import_midi_to_pitch: fill_gaps filled={}", filled));
            }
        }
    }

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
    track_indices: Vec<usize>,
    track_id: Option<String>,
    start_sec: f64,
    fill_gaps: Option<bool>,
    multi_track_merge: Option<bool>,
) -> crate::models::TimelineStatePayload {
    midi_log(format!(
        "import_midi_as_clip: path={} track_indices={:?} track_id={:?} start_sec={:.3} fill_gaps={:?} multi_track_merge={:?}",
        midi_path, track_indices, track_id, start_sec, fill_gaps, multi_track_merge
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

    let file_stem = path
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("MIDI")
        .to_string();

    let fill = fill_gaps.unwrap_or(false);
    let multi = multi_track_merge.unwrap_or(true);

    if multi {
        // ── 合并模式：将所有选中轨道的音符合并为单个 clip ──
        let notes: Vec<midi_import::MidiNoteEvent> = {
            let mut all: Vec<_> = if track_indices.is_empty() {
                parse_result.track_notes.into_iter().flatten().collect()
            } else {
                track_indices
                    .iter()
                    .filter_map(|&idx| parse_result.track_notes.get(idx))
                    .flatten()
                    .cloned()
                    .collect()
            };
            all.sort_by(|a, b| {
                a.start_sec
                    .partial_cmp(&b.start_sec)
                    .unwrap_or(std::cmp::Ordering::Equal)
            });
            all
        };

        if notes.is_empty() {
            midi_log("import_midi_as_clip: no_notes");
            return error_payload("no_notes_in_track");
        }

        let last_end = notes.iter().map(|n| n.end_sec).fold(0.0f64, f64::max);
        let length_sec = last_end.max(0.1);

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

        let pitch_range = {
            let min_note = normalized_notes.iter().map(|n| n.note).fold(127u8, u8::min);
            let max_note = normalized_notes.iter().map(|n| n.note).fold(0u8, u8::max);
            Some(crate::models::PitchRange {
                min: min_note as f32,
                max: max_note as f32,
            })
        };

        state.checkpoint_timeline(&tl);

        let clip_id = tl.add_clip(
            track_id,
            Some(file_stem),
            Some(start_sec),
            Some(length_sec),
            None,
        );

        if let Some(clip) = tl.clips.iter_mut().find(|c| c.id == clip_id) {
            clip.midi_note_data = Some(normalized_notes);
            clip.midi_fill_gaps = fill;
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

        let root_track_id = tl.resolve_root_track_id(
            &tl.clips
                .iter()
                .find(|c| c.id == clip_id)
                .map(|c| c.track_id.clone())
                .unwrap_or_default(),
        );
        state.audio_engine.update_timeline(tl.clone());
        let mut payload = tl.to_payload();
        payload.created_clip_ids = Some(vec![clip_id]);
        payload.project = Some(state.project_meta_payload());
        drop(tl);
        if let Some(root) = root_track_id {
            crate::pitch_analysis::maybe_schedule_pitch_orig(state, &root);
        }
        payload
    } else {
        // ── 非合并模式：每条轨道独立处理，重叠音符拆分为不同 clip ──
        let resolved_indices: Vec<usize> = if track_indices.is_empty() {
            (0..parse_result.track_notes.len()).collect()
        } else {
            track_indices
                .iter()
                .filter(|&&idx| idx < parse_result.track_notes.len())
                .copied()
                .collect()
        };

        if resolved_indices.is_empty() {
            midi_log("import_midi_as_clip: no_tracks");
            return error_payload("no_notes_in_track");
        }

        state.checkpoint_timeline(&tl);

        let mut created_clip_ids: Vec<String> = vec![];
        let mut created_track_ids: Vec<String> = vec![];
        let mut current_track_id = track_id;

        for (ti, &track_idx) in resolved_indices.iter().enumerate() {
            let track_notes = &parse_result.track_notes[track_idx];
            if track_notes.is_empty() {
                continue;
            }

            let track_info = &parse_result.tracks[track_idx];
            let track_name = if track_info.name.is_empty() {
                format!("Track {}", track_idx + 1)
            } else {
                track_info.name.clone()
            };

            let groups = midi_import::split_notes_into_non_overlapping_groups(track_notes);

            for (gi, group) in groups.iter().enumerate() {
                if group.is_empty() {
                    continue;
                }

                if current_track_id.is_none() {
                    let new_id = tl.add_track(Some(file_stem.clone()), None, None);
                    created_track_ids.push(new_id.clone());
                    current_track_id = Some(new_id);
                }

                let first_start = group
                    .iter()
                    .map(|n| n.start_sec)
                    .fold(f64::INFINITY, f64::min);
                let last_end = group
                    .iter()
                    .map(|n| n.end_sec)
                    .fold(0.0f64, f64::max);
                let normalized: Vec<midi_import::MidiNoteEvent> = group
                    .iter()
                    .map(|n| midi_import::MidiNoteEvent {
                        start_sec: n.start_sec - first_start,
                        end_sec: n.end_sec - first_start,
                        note: n.note,
                        velocity: n.velocity,
                    })
                    .collect();

                let pitch_range = {
                    let min_note = normalized.iter().map(|n| n.note).fold(127u8, u8::min);
                    let max_note = normalized.iter().map(|n| n.note).fold(0u8, u8::max);
                    Some(crate::models::PitchRange {
                        min: min_note as f32,
                        max: max_note as f32,
                    })
                };

                let clip_name = if groups.len() > 1 {
                    format!("{} - {} #{}", file_stem, track_name, gi + 1)
                } else {
                    format!("{} - {}", file_stem, track_name)
                };

                let clip_id = tl.add_clip(
                    current_track_id.clone(),
                    Some(clip_name),
                    Some(start_sec),
                    Some((last_end - first_start).max(0.1)),
                    None,
                );

                if let Some(clip) = tl.clips.iter_mut().find(|c| c.id == clip_id) {
                    clip.midi_note_data = Some(normalized);
                    clip.midi_fill_gaps = fill;
                    clip.pitch_range = pitch_range;
                    clip.color = "cyan".to_string();
                    clip.source_path = None;
                    clip.source_path_relative = None;
                }

                created_clip_ids.push(clip_id);

                if gi + 1 < groups.len() {
                    let insert_pos = current_track_id
                        .as_ref()
                        .and_then(|tid| tl.tracks.iter().position(|t| t.id == *tid))
                        .map(|pos| pos + 1);
                    let new_name = if groups.len() > 1 {
                        format!("{} - {} #{}", file_stem, track_name, gi + 2)
                    } else {
                        format!("{} - {}", file_stem, track_name)
                    };
                    let new_id = tl.add_track(Some(new_name), None, insert_pos);
                    created_track_ids.push(new_id.clone());
                    current_track_id = Some(new_id);
                }
            }

            if ti + 1 < resolved_indices.len() {
                let insert_pos = current_track_id
                    .as_ref()
                    .and_then(|tid| tl.tracks.iter().position(|t| t.id == *tid))
                    .map(|pos| pos + 1);
                let new_id = tl.add_track(Some(file_stem.clone()), None, insert_pos);
                created_track_ids.push(new_id.clone());
                current_track_id = Some(new_id);
            }
        }

        midi_log(format!(
            "import_midi_as_clip: multi_track_merge=false created_clips={} created_tracks={}",
            created_clip_ids.len(),
            created_track_ids.len()
        ));

        let mut root_track_ids: std::collections::HashSet<String> =
            std::collections::HashSet::new();
        for clip_id in &created_clip_ids {
            if let Some(clip) = tl.clips.iter().find(|c| c.id == *clip_id) {
                if let Some(root) = tl.resolve_root_track_id(&clip.track_id) {
                    root_track_ids.insert(root);
                }
            }
        }
        state.audio_engine.update_timeline(tl.clone());
        let mut payload = tl.to_payload();
        payload.created_clip_ids = Some(created_clip_ids);
        payload.created_track_ids = Some(created_track_ids);
        payload.project = Some(state.project_meta_payload());
        drop(tl);
        for root in &root_track_ids {
            crate::pitch_analysis::maybe_schedule_pitch_orig(state, root);
        }
        payload
    }
}
