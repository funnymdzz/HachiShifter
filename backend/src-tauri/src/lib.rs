mod audio_engine;
#[path = "audio/audio_utils.rs"]
mod audio_utils;
#[path = "pitch/clip_pitch_cache.rs"]
mod clip_pitch_cache;
#[path = "pitch/clip_rendering_state.rs"]
mod clip_rendering_state;
mod commands;
#[path = "audio/hifigan_tension.rs"]
mod hifigan_tension;
#[path = "audio/formant_morph.rs"]
mod formant_morph;
mod formant_cache;
mod launch_args;
#[path = "audio/mixdown.rs"]
mod mixdown;
mod models;
mod pitch_analysis;
#[path = "pitch/game_detector.rs"]
mod game_detector;
#[path = "pitch/melodyne_correction.rs"]
mod melodyne_correction;
#[path = "pitch/pitch_clip.rs"]
mod pitch_clip;
#[path = "pitch/pitch_config.rs"]
mod pitch_config;
mod pitch_editing;
#[path = "pitch/pitch_progress.rs"]
mod pitch_progress;
mod renderer;
mod synth_clip_cache;

#[cfg(feature = "onnx")]
#[path = "vocoder/ort_session.rs"]
mod vocoder_ort_session;

#[cfg(target_os = "windows")]
#[path = "vocoder/gpu_info.rs"]
mod gpu_info;

#[cfg(not(target_os = "windows"))]
#[path = "vocoder/gpu_info_stub.rs"]
mod gpu_info;

#[cfg(target_os = "windows")]
#[path = "vocoder/dml_adapters.rs"]
mod dml_adapters;

#[cfg(not(target_os = "windows"))]
#[path = "vocoder/dml_adapters_stub.rs"]
mod dml_adapters;

#[cfg(feature = "onnx")]
#[path = "vocoder/mel_utils.rs"]
mod mel_utils;

#[cfg(feature = "onnx")]
#[path = "vocoder/nsf_hifigan_onnx.rs"]
mod nsf_hifigan_onnx;
#[cfg(not(feature = "onnx"))]
#[path = "vocoder/nsf_hifigan_onnx_stub.rs"]
mod nsf_hifigan_onnx_stub;
#[cfg(not(feature = "onnx"))]
use nsf_hifigan_onnx_stub as nsf_hifigan_onnx;

#[cfg(feature = "onnx")]
#[path = "vocoder/hnsep_onnx.rs"]
mod hnsep_onnx;
#[cfg(not(feature = "onnx"))]
#[path = "vocoder/hnsep_onnx_stub.rs"]
mod hnsep_onnx_stub;
#[cfg(not(feature = "onnx"))]
use hnsep_onnx_stub as hnsep_onnx;

#[cfg(feature = "onnx")]
#[path = "vocoder/fcpe_onnx.rs"]
mod fcpe_onnx;
#[cfg(not(feature = "onnx"))]
#[path = "vocoder/fcpe_onnx_stub.rs"]
mod fcpe_onnx_stub;
#[cfg(not(feature = "onnx"))]
use fcpe_onnx_stub as fcpe_onnx;

mod config;
#[path = "audio/hfspeaks_v2.rs"]
mod hfspeaks_v2;
#[path = "import/midi_import.rs"]
mod midi_import;
#[path = "import/melodyne_import.rs"]
mod melodyne_import;
mod project;
#[path = "import/reaper_import.rs"]
mod reaper_import;
#[path = "import/reaper_parser.rs"]
mod reaper_parser;
#[path = "audio/sstretch.rs"]
mod sstretch;
#[path = "audio/soundtouch.rs"]
mod soundtouch;
mod sample_annotations;
mod state;
#[path = "vocoder/streaming_world.rs"]
mod streaming_world;
mod temp_manager;
#[path = "audio/time_stretch.rs"]
mod time_stretch;
#[path = "import/vocalshifter_clipboard.rs"]
mod vocalshifter_clipboard;
#[path = "import/vocalshifter_import.rs"]
mod vocalshifter_import;
#[cfg(feature = "vslib")]
#[path = "vocoder/vslib.rs"]
mod vslib;
#[path = "vocoder/world_vocoder.rs"]
mod world_vocoder;

use std::path::{Path, PathBuf};
use std::sync::OnceLock;
use tauri::Manager;

static NSF_HIFIGAN_MODEL_DIR: OnceLock<PathBuf> = OnceLock::new();
static HNSEP_MODEL_DIR: OnceLock<PathBuf> = OnceLock::new();
static FCPE_ONNX_PATH: OnceLock<PathBuf> = OnceLock::new();
static GAME_MODEL_DIR: OnceLock<PathBuf> = OnceLock::new();

pub fn nsf_hifigan_model_dir() -> Option<&'static Path> {
    NSF_HIFIGAN_MODEL_DIR.get().map(|p| p.as_path())
}

pub fn hnsep_model_dir() -> Option<&'static Path> {
    HNSEP_MODEL_DIR.get().map(|p| p.as_path())
}

pub fn fcpe_onnx_path() -> Option<&'static Path> {
    FCPE_ONNX_PATH.get().map(|p| p.as_path())
}

pub fn game_model_dir() -> Option<&'static Path> {
    GAME_MODEL_DIR.get().map(|path| path.as_path())
}

pub fn nsf_hifigan_onnx_probe() -> Result<String, String> {
    // Probe ONNX model availability.
    #[cfg(feature = "onnx")]
    {
        nsf_hifigan_onnx::probe_load().map(|_| "ok".to_string())
    }
    #[cfg(not(feature = "onnx"))]
    {
        Err("onnx feature disabled".to_string())
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    if let Some(result) = try_compare_vocal_f0_from_args() {
        match result {
            Ok(()) => std::process::exit(0),
            Err(error) => {
                eprintln!("vocal F0 comparison failed: {error}");
                std::process::exit(2);
            }
        }
    }
    if let Some(result) = try_render_mpd_vocal_from_args() {
        match result {
            Ok(()) => std::process::exit(0),
            Err(error) => {
                eprintln!("headless MPD vocal render failed: {error}");
                std::process::exit(2);
            }
        }
    }
    tauri::Builder::default()
        .manage(state::AppState::default())
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            // 打包后的应用：从 resource_dir 查找内嵌的 ONNX 模型
            if let Ok(res_dir) = app.path().resource_dir() {
                let p = res_dir.join("models").join("nsf_hifigan");
                if p.join("pc_nsf_hifigan.onnx").exists() && p.join("config.json").exists() {
                    let _ = NSF_HIFIGAN_MODEL_DIR.set(p);
                }
            }

            if let Ok(res_dir) = app.path().resource_dir() {
                let p = res_dir.join("models").join("hnsep");
                if p.join("hnsep.onnx").exists() {
                    let _ = HNSEP_MODEL_DIR.set(p);
                }
            }

            if let Ok(res_dir) = app.path().resource_dir() {
                let p = res_dir.join("models").join("fcpe").join("fcpe.onnx");
                if p.exists() {
                    let _ = FCPE_ONNX_PATH.set(p);
                }
            }

            if let Ok(res_dir) = app.path().resource_dir() {
                let p = res_dir.join("models").join("game");
                if p.join("encoder.onnx").exists()
                    && p.join("segmenter.onnx").exists()
                    && p.join("estimator.onnx").exists()
                    && p.join("bd2dur.onnx").exists()
                    && p.join("config.json").exists()
                {
                    let _ = GAME_MODEL_DIR.set(p);
                }
            }

            let state = app.state::<state::AppState>();

            // 从进程启动参数中解析工程路径（双击文件关联场景）。
            let startup_project_path =
                launch_args::extract_project_path_from_args(std::env::args_os());
            state.set_pending_startup_project_path(startup_project_path);

            // Expose app handle for background workers.
            let _ = state.app_handle.set(app.handle().clone());

            // 将 app_handle 传递给 audio engine worker，使其能向前端推送事件。
            state.audio_engine.set_app_handle(app.handle().clone());

            // Prefer the OS-level app cache dir so peaks persist across runs.
            let base = app
                .path()
                .app_cache_dir()
                .unwrap_or_else(|_| hfspeaks_v2::default_cache_dir());
            let dir = base.join("hachishifter").join("waveform_peaks_cache");
            {
                let mut d = state
                    .waveform_cache_dir
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                *d = dir.clone();
            }
            let _ = hfspeaks_v2::ensure_cache_dir(&dir);

            // 加载持久化的最近工程列表
            if let Ok(cfg_base) = app.path().app_config_dir() {
                let cfg_dir = cfg_base.join("HachiShifter");
                let _ = std::fs::create_dir_all(&cfg_dir);
                let recent = crate::config::load_recent(&cfg_dir);
                {
                    let mut p = state.project.lock().unwrap_or_else(|e| e.into_inner());
                    p.recent = recent;
                }
                let _ = state.config_dir.set(cfg_dir);
            }

            // 尝试恢复上次运行时保存的窗口状态（非强制性）
            if let Some(cfg_dir) = state.config_dir.get() {
                if let Some(win) = app.get_webview_window("main") {
                    let ws = crate::config::load_window_state(cfg_dir);
                    // 应用尺寸与位置（非最大化/全屏状态先应用尺寸/位置，再切换最大化）
                    if let (Some(w), Some(h)) = (ws.width, ws.height) {
                        let _ = win.set_size(tauri::Size::Logical(tauri::LogicalSize {
                            width: w,
                            height: h,
                        }));
                    }
                    if let (Some(x), Some(y)) = (ws.x, ws.y) {
                        let _ =
                            win.set_position(tauri::Position::Logical(tauri::LogicalPosition {
                                x: x as f64,
                                y: y as f64,
                            }));
                    }
                    if ws.fullscreen.unwrap_or(false) {
                        let _ = win.set_fullscreen(true);
                    } else if ws.maximized.unwrap_or(false) {
                        let _ = win.maximize();
                    } else {
                        let _ = win.set_fullscreen(false);
                    }
                }
            }

            // 启动时清理上次遗留的临时文件（后台线程，不阻塞启动）
            temp_manager::cleanup_stale_temp_files();

            Ok(())
        })
        // 在窗口事件中监听 CloseRequested，保存窗口状态到配置目录
        .on_window_event(|win, event| {
            if let tauri::WindowEvent::CloseRequested { .. } = event {
                // 仅针对主窗口保存状态
                if win.label() != "main" {
                    return;
                }

                let maximized = win.is_maximized().unwrap_or(false);
                let fullscreen = win.is_fullscreen().unwrap_or(false);
                let mut x_opt = None;
                let mut y_opt = None;
                let mut w_opt = None;
                let mut h_opt = None;
                if let Ok(pos) = win.outer_position() {
                    x_opt = Some(pos.x);
                    y_opt = Some(pos.y);
                }
                if let Ok(size) = win.inner_size() {
                    w_opt = Some(size.width as f64);
                    h_opt = Some(size.height as f64);
                }

                if let Some(cfg_dir) = win.app_handle().state::<state::AppState>().config_dir.get()
                {
                    let ws = crate::config::WindowState {
                        x: x_opt,
                        y: y_opt,
                        width: w_opt,
                        height: h_opt,
                        maximized: Some(maximized),
                        fullscreen: Some(fullscreen),
                    };
                    crate::config::save_window_state(cfg_dir, &ws);
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::ping,
            commands::get_runtime_info,
            commands::consume_startup_project_path,
            commands::set_ui_locale,
            commands::get_timeline_state,
            commands::get_timeline_state_lite,
            commands::set_transport,
            commands::close_window,
            commands::undo_timeline,
            commands::redo_timeline,
            commands::begin_undo_group,
            commands::end_undo_group,
            commands::get_project_meta,
            commands::new_project,
            commands::open_project_dialog,
            commands::open_project,
            commands::inspect_melodyne_project_tracks,
            commands::save_project,
            commands::save_project_as,
            commands::get_auto_backup_settings,
            commands::save_auto_backup_settings,
            commands::run_timed_auto_backup,
            commands::set_project_base_scale,
            commands::set_project_custom_scale,
            commands::set_project_stretch_settings,
            commands::set_project_timeline_settings,
            commands::open_audio_dialog,
            commands::open_audio_dialog_multi,
            commands::open_oto_dialog,
            commands::get_clip_sample_annotations,
            commands::save_clip_sample_annotations,
            commands::redetect_clip_sample_annotations,
            commands::convert_oto_to_annotations,
            commands::convert_oto_and_refresh_clip,
            commands::get_game_status,
            commands::apply_melodyne_correction,
            commands::pick_output_path,
            commands::pick_directory,
            commands::open_midi_dialog,
            commands::get_root_mix_waveform_peaks_segment,
            commands::get_track_mix_waveform_peaks_segment,
            commands::clear_waveform_cache,
            commands::get_waveform_mipmap_binary,
            commands::preload_waveform_mipmap,
            commands::batch_get_waveform_mipmap,
            commands::import_audio_item,
            commands::import_audio_bytes,
            commands::add_track,
            commands::remove_track,
            commands::duplicate_track,
            commands::move_track,
            commands::set_track_state,
            commands::select_track,
            commands::set_project_length,
            commands::get_track_summary,
            commands::get_param_frames,
            commands::set_param_frames,
            commands::restore_param_frames,
            commands::add_clip,
            commands::create_clips_bulk,
            commands::get_static_param,
            commands::set_static_param,
            commands::remove_clip,
            commands::remove_clips,
            commands::move_clip,
            commands::move_clips,
            commands::get_clip_linked_params,
            commands::apply_clip_linked_params,
            commands::set_melodyne_note_boundary,
            commands::set_melodyne_note_connection,
            commands::set_clip_state,
            commands::set_clips_state_bulk,
            commands::duplicate_clips_bulk,
            commands::replace_clip_source,
            commands::check_source_files_changed,
            commands::split_clip,
            commands::split_clips_at,
            commands::glue_clips,
            commands::group_clips,
            commands::ungroup_clips,
            commands::toggle_group_disabled,
            commands::convert_clips_to_pitch_reference,
            commands::update_pitch_reference,
            commands::select_clip,
            commands::load_default_model,
            commands::load_model,
            commands::set_pitch_shift,
            commands::process_audio,
            commands::synthesize,
            commands::save_synthesized,
            commands::save_separated,
            commands::export_audio_advanced,
            commands::cancel_export_audio,
            commands::get_export_audio_defaults,
            commands::preview_export_audio_plan,
            commands::quick_export_selected_clips,
            commands::play_original,
            commands::stop_audio,
            commands::get_playback_state,
            commands::start_background_render,
            commands::cancel_background_render,
            commands::debug_realtime_render_stats,
            commands::get_pitch_analysis_progress,
            commands::get_onnx_status,
            commands::get_onnx_diagnostic,
            commands::run_vocoder_benchmark,
            commands::get_gpu_devices,
            commands::get_dml_adapters,
            commands::clear_pitch_cache,
            commands::get_pitch_cache_stats,
            commands::list_directory,
            commands::get_audio_file_info,
            commands::read_audio_preview,
            commands::search_files_recursive,
            commands::open_vocalshifter_dialog,
            commands::import_vocalshifter_project,
            commands::paste_vocalshifter_clipboard,
            commands::open_reaper_dialog,
            commands::import_reaper_project,
            commands::paste_reaper_clipboard,
            commands::clear_cache,
            commands::get_processor_params,
            commands::get_midi_tracks,
            commands::read_midi_clipboard_to_memory,
            commands::import_midi_to_pitch,
            commands::import_midi_as_clip,
            commands::replace_midi_clip_data,
            commands::pick_midi_output_path,
            commands::export_pitch_to_midi,
            commands::get_ui_settings,
            commands::save_ui_settings,
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|app_handle, event| {
            if let tauri::RunEvent::Exit = event {
                // Shut down audio engine: stop meter thread, send Shutdown to
                // worker threads, and drop the channel sender so all worker
                // threads exit their recv loops.
                let state = app_handle.state::<state::AppState>();
                state.audio_engine.shutdown();

                // Force-drop all ONNX sessions to release GPU memory before exit.
                crate::nsf_hifigan_onnx::drop_shared_session();
                crate::fcpe_onnx::drop_shared_session();
                crate::hnsep_onnx::drop_shared_session();
            }
        });
}

fn try_compare_vocal_f0_from_args() -> Option<Result<(), String>> {
    let args = std::env::args_os().collect::<Vec<_>>();
    let flag_index = args
        .iter()
        .position(|arg| arg.as_os_str() == std::ffi::OsStr::new("--compare-vocal-f0"))?;
    let reference = args.get(flag_index + 1).map(std::path::PathBuf::from);
    let rendered = args.get(flag_index + 2).map(std::path::PathBuf::from);
    Some((|| {
        let reference = reference.ok_or_else(|| "missing reference vocal WAV".to_string())?;
        let rendered = rendered.ok_or_else(|| "missing rendered vocal WAV".to_string())?;
        fn read_mono(path: &std::path::Path) -> Result<(u32, Vec<f64>), String> {
            let (rate, channels, pcm) = crate::audio_utils::decode_audio_f32_interleaved(path)?;
            let channels = channels.max(1) as usize;
            let mono = pcm
                .chunks_exact(channels)
                .map(|frame| frame.iter().map(|value| *value as f64).sum::<f64>() / channels as f64)
                .collect();
            Ok((rate, mono))
        }
        fn resample(input: &[f64], in_rate: u32, out_rate: u32) -> Vec<f64> {
            if in_rate == out_rate || input.len() < 2 { return input.to_vec(); }
            let frames = ((input.len() as f64 * out_rate as f64 / in_rate as f64).round())
                .max(1.0) as usize;
            (0..frames)
                .map(|index| {
                    let position = index as f64 * in_rate as f64 / out_rate as f64;
                    let left = (position.floor() as usize).min(input.len() - 1);
                    let right = (left + 1).min(input.len() - 1);
                    input[left] + (input[right] - input[left]) * (position - left as f64)
                })
                .collect()
        }
        fn envelope(input: &[f64], hop: usize) -> Vec<f64> {
            input
                .chunks(hop.max(1))
                .map(|chunk| (chunk.iter().map(|value| value * value).sum::<f64>()
                    / chunk.len().max(1) as f64).sqrt())
                .collect()
        }
        let target_rate = 48_000;
        let (reference_rate, reference_pcm) = read_mono(&reference)?;
        let (rendered_rate, rendered_pcm) = read_mono(&rendered)?;
        let reference_pcm = resample(&reference_pcm, reference_rate, target_rate);
        let rendered_pcm = resample(&rendered_pcm, rendered_rate, target_rate);
        if rendered_pcm.len() < reference_pcm.len() {
            return Err("rendered vocal is shorter than reference".to_string());
        }
        let ref_env = envelope(&reference_pcm, 480);
        let rendered_env = envelope(&rendered_pcm, 480);
        let mut best = (f64::NEG_INFINITY, 0usize);
        for offset in 0..=rendered_env.len().saturating_sub(ref_env.len()) {
            let candidate = &rendered_env[offset..offset + ref_env.len()];
            let ref_mean = ref_env.iter().sum::<f64>() / ref_env.len().max(1) as f64;
            let candidate_mean = candidate.iter().sum::<f64>() / candidate.len().max(1) as f64;
            let mut dot = 0.0;
            let mut aa = 0.0;
            let mut bb = 0.0;
            for (left, right) in ref_env.iter().zip(candidate) {
                let a = left - ref_mean;
                let b = right - candidate_mean;
                dot += a * b;
                aa += a * a;
                bb += b * b;
            }
            let correlation = dot / (aa * bb).sqrt().max(1e-12);
            if correlation > best.0 { best = (correlation, offset); }
        }
        let offset_samples = best.1 * 480;
        let aligned = &rendered_pcm[offset_samples..(offset_samples + reference_pcm.len())
            .min(rendered_pcm.len())];
        let length = aligned.len().min(reference_pcm.len());
        let reference_f0 = crate::world_vocoder::analyze_f0_harvest(
            &reference_pcm[..length], target_rate, 5.0,
        )?;
        let rendered_f0 = crate::world_vocoder::analyze_f0_harvest(
            &aligned[..length], target_rate, 5.0,
        )?;
        let frame_count = reference_f0.len().min(rendered_f0.len());
        let mut errors = Vec::new();
        let mut signed = Vec::new();
        let mut reference_voiced = 0usize;
        let mut rendered_voiced = 0usize;
        for index in 0..frame_count {
            let left = reference_f0[index];
            let right = rendered_f0[index];
            reference_voiced += usize::from(left > 0.0);
            rendered_voiced += usize::from(right > 0.0);
            if left > 0.0 && right > 0.0 {
                let cents = 1200.0 * (right / left).log2();
                signed.push(cents);
                errors.push(cents.abs());
            }
        }
        errors.sort_by(f64::total_cmp);
        signed.sort_by(f64::total_cmp);
        let percentile = |values: &[f64], fraction: f64| -> f64 {
            if values.is_empty() { return 0.0; }
            values[((values.len() - 1) as f64 * fraction).round() as usize]
        };
        println!("{{\"reference\":\"{}\",\"rendered\":\"{}\",\"alignment_sec\":{:.6},\"envelope_correlation\":{:.6},\"frames\":{},\"reference_voiced_frames\":{},\"rendered_voiced_frames\":{},\"paired_voiced_frames\":{},\"median_abs_cents\":{:.3},\"p90_abs_cents\":{:.3},\"median_signed_cents\":{:.3}}}",
            reference.display(), rendered.display(), offset_samples as f64 / target_rate as f64,
            best.0, frame_count, reference_voiced, rendered_voiced, errors.len(),
            percentile(&errors, 0.5), percentile(&errors, 0.9), percentile(&signed, 0.5));
        Ok(())
    })())
}

/// Small headless reference path used to compare the imported `vocal` track
/// with Melodyne's own export without starting GTK/WebView or downloading any
/// model. MLD5 is model-free, so the normal no-model CI artifact is enough.
fn try_render_mpd_vocal_from_args() -> Option<Result<(), String>> {
    let args = std::env::args_os().collect::<Vec<_>>();
    let flag_index = args
        .iter()
        .position(|arg| arg.as_os_str() == std::ffi::OsStr::new("--render-mpd-vocal"))?;
    let input = args.get(flag_index + 1).map(std::path::PathBuf::from);
    let output = args.get(flag_index + 2).map(std::path::PathBuf::from);
    Some((|| {
        let input = input.ok_or_else(|| "missing MPD input path".to_string())?;
        let output = output.ok_or_else(|| "missing WAV output path".to_string())?;
        let mut imported = crate::melodyne_import::import_mpd_file(
            &input,
            &|progress, stage, current, total| {
                eprintln!(
                    "[headless-mpd] {stage} {:.0}% ({current}/{total})",
                    progress * 100.0
                );
            },
            // Let the importer restore every track Melodyne marked as melodic.
            // Track order in an MPD is not tied to the visible mixer order (in
            // the reference project index 0 is the MixDown track, not vocal),
            // so selecting index 0 would silently skip the vocal note edits.
            None,
            None,
        )?;
        let vocal_track_id = imported
            .timeline
            .tracks
            .iter()
            .find(|track| track.name.eq_ignore_ascii_case("vocal"))
            .or_else(|| imported.timeline.tracks.first())
            .map(|track| track.id.clone())
            .ok_or_else(|| "MPD contains no vocal track".to_string())?;
        for track in &mut imported.timeline.tracks {
            track.muted = track.id != vocal_track_id;
            track.solo = false;
        }
        crate::mixdown::render_mixdown_wav(
            &imported.timeline,
            &output,
            crate::mixdown::MixdownOptions {
                sample_rate: 48_000,
                start_sec: 0.0,
                end_sec: None,
                stretch: crate::time_stretch::StretchAlgorithm::MelodyneHybrid,
                apply_pitch_edit: true,
                export_format: crate::mixdown::ExportFormat::Wav16,
                quality_preset: crate::mixdown::QualityPreset::Export,
                cancel_flag: None,
            },
        )?;
        eprintln!("[headless-mpd] wrote {}", output.display());
        Ok(())
    })())
}
