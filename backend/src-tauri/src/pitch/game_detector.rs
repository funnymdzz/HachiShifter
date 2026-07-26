//! GAME (Generative Adaptive MIDI Extractor) ONNX note segmentation.
//!
//! The desktop application uses the large ONNX export by default and switches
//! to the small export in performance mode. The four-stage inference graph is:
//! encoder -> iterative segmenter -> boundary-to-duration -> estimator.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct GameOptions {
    pub d3pm_steps: usize,
    pub segmentation_threshold: f32,
    pub segmentation_radius: i64,
    pub estimation_threshold: f32,
    pub language: i64,
    #[serde(default)]
    pub performance_mode: bool,
}

impl Default for GameOptions {
    fn default() -> Self {
        Self {
            d3pm_steps: 8,
            segmentation_threshold: 0.20,
            segmentation_radius: 2,
            estimation_threshold: 0.20,
            language: 0,
            performance_mode: false,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GameNote {
    pub start_sec: f64,
    pub end_sec: f64,
    pub midi_note: f32,
    pub is_rest: bool,
    pub confidence: f32,
}

#[derive(Debug, Clone, Serialize)]
pub struct GameStatus {
    pub available: bool,
    pub model_dir: Option<String>,
    pub performance_available: bool,
    pub performance_model_dir: Option<String>,
    pub message: String,
}

#[cfg(feature = "onnx")]
mod implementation {
    use super::{GameNote, GameOptions, GameStatus};
    use ort::session::Session;
    use ort::value::Tensor;
    use serde::Deserialize;
    use std::path::{Path, PathBuf};
    use std::sync::{Mutex, OnceLock};

    const MAX_ENCODER_FRAMES: usize = 5_000;

    #[derive(Debug, Clone, Deserialize)]
    struct GameConfig {
        samplerate: u32,
        timestep: f32,
        #[serde(rename = "loop")]
        loop_enabled: bool,
        #[serde(default = "default_embedding_dim")]
        embedding_dim: usize,
    }

    fn default_embedding_dim() -> usize {
        256
    }

    struct GameSessions {
        performance_mode: bool,
        config: GameConfig,
        encoder: Session,
        segmenter: Session,
        estimator: Session,
        bd2dur: Session,
    }

    static SESSIONS: OnceLock<Mutex<Option<GameSessions>>> = OnceLock::new();
    static ORT_INIT: OnceLock<()> = OnceLock::new();

    fn env_path(name: &str) -> Option<PathBuf> {
        std::env::var(name)
            .ok()
            .map(|value| value.trim().trim_matches('"').to_string())
            .filter(|value| !value.is_empty())
            .map(PathBuf::from)
    }

    fn is_model_dir(path: &Path) -> bool {
        [
            "encoder.onnx",
            "segmenter.onnx",
            "estimator.onnx",
            "bd2dur.onnx",
            "config.json",
        ]
        .iter()
        .all(|name| path.join(name).is_file())
    }

    fn resolve_model_dir(performance_mode: bool) -> Result<PathBuf, String> {
        let override_name = if performance_mode {
            "HACHISHIFTER_GAME_SMALL_MODEL_DIR"
        } else {
            "HACHISHIFTER_GAME_MODEL_DIR"
        };
        if let Some(path) = env_path(override_name) {
            if is_model_dir(&path) {
                return Ok(path);
            }
            return Err(format!(
                "GAME model directory is incomplete: {}",
                path.display()
            ));
        }

        if let Some(root) = crate::game_model_dir() {
            let path = if performance_mode {
                root.join("small")
            } else {
                root.to_path_buf()
            };
            if is_model_dir(&path) {
                return Ok(path);
            }
        }

        let development = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("resources")
            .join("models")
            .join("game");
        let development = if performance_mode {
            development.join("small")
        } else {
            development
        };
        if is_model_dir(&development) {
            return Ok(development);
        }

        if let Ok(executable) = std::env::current_exe() {
            if let Some(parent) = executable.parent() {
                let portable_root = parent.join("models").join("game");
                let portable = if performance_mode {
                    portable_root.join("small")
                } else {
                    portable_root
                };
                if is_model_dir(&portable) {
                    return Ok(portable);
                }
            }
        }

        Err(format!(
            "GAME {} ONNX model pack was not found",
            if performance_mode { "small" } else { "large" }
        ))
    }

    fn load_sessions(performance_mode: bool) -> Result<GameSessions, String> {
        ORT_INIT.get_or_init(|| {
            ort::init().with_name("hachishifter-game").commit();
        });

        let dir = resolve_model_dir(performance_mode)?;
        let config_text = std::fs::read_to_string(dir.join("config.json"))
            .map_err(|error| format!("read GAME config failed: {error}"))?;
        let config: GameConfig = serde_json::from_str(&config_text)
            .map_err(|error| format!("parse GAME config failed: {error}"))?;
        if config.samplerate == 0 || !(config.timestep.is_finite() && config.timestep > 0.0) {
            return Err("GAME config contains an invalid sample rate or timestep".to_string());
        }

        let build = |name: &str| {
            crate::vocoder_ort_session::build_ort_session(
                &dir.join(name),
                crate::vocoder_ort_session::OrtSessionRole::PitchDetector,
            )
            .map(|value| value.0)
            .map_err(|error| format!("load GAME {name} failed: {error}"))
        };

        Ok(GameSessions {
            performance_mode,
            config,
            encoder: build("encoder.onnx")?,
            segmenter: build("segmenter.onnx")?,
            estimator: build("estimator.onnx")?,
            bd2dur: build("bd2dur.onnx")?,
        })
    }

    fn with_sessions<T>(
        performance_mode: bool,
        callback: impl FnOnce(&mut GameSessions) -> Result<T, String>,
    ) -> Result<T, String> {
        let sessions = SESSIONS.get_or_init(|| Mutex::new(None));
        let mut guard = sessions
            .lock()
            .map_err(|error| format!("GAME session lock poisoned: {error}"))?;
        if guard
            .as_ref()
            .map(|sessions| sessions.performance_mode != performance_mode)
            .unwrap_or(true)
        {
            *guard = Some(load_sessions(performance_mode)?);
        }
        callback(guard.as_mut().expect("GAME sessions initialized"))
    }

    fn linear_resample(input: &[f32], input_rate: u32, output_rate: u32) -> Vec<f32> {
        if input_rate == output_rate || input.is_empty() {
            return input.to_vec();
        }
        let ratio = output_rate as f64 / input_rate.max(1) as f64;
        let output_len = ((input.len() as f64) * ratio).round().max(1.0) as usize;
        let mut output = Vec::with_capacity(output_len);
        for index in 0..output_len {
            let source = index as f64 / ratio;
            let left = source.floor() as usize;
            let right = (left + 1).min(input.len() - 1);
            let fraction = (source - left as f64) as f32;
            output.push(input[left] * (1.0 - fraction) + input[right] * fraction);
        }
        output
    }

    fn make_chunks(sample_count: usize, max_chunk_samples: usize) -> Vec<(usize, usize)> {
        if sample_count == 0 {
            return Vec::new();
        }
        let max_chunk_samples = max_chunk_samples.max(1);
        let mut chunks = Vec::new();
        let mut start = 0usize;
        while start < sample_count {
            let end = (start + max_chunk_samples).min(sample_count);
            chunks.push((start, end));
            start = end;
        }
        chunks
    }

    fn process_chunk(
        sessions: &mut GameSessions,
        waveform: &[f32],
        chunk_start_sec: f64,
        options: GameOptions,
    ) -> Result<Vec<GameNote>, String> {
        if waveform.is_empty() {
            return Ok(Vec::new());
        }
        let duration = waveform.len() as f32 / sessions.config.samplerate as f32;
        let waveform_tensor = Tensor::from_array((
            [1usize, waveform.len()],
            waveform.to_vec().into_boxed_slice(),
        ))
        .map_err(|error| format!("build GAME waveform tensor failed: {error}"))?;
        let duration_tensor = Tensor::from_array(([1usize], vec![duration].into_boxed_slice()))
            .map_err(|error| format!("build GAME duration tensor failed: {error}"))?;
        let encoder_outputs = sessions
            .encoder
            .run(ort::inputs![
                "waveform" => waveform_tensor,
                "duration" => duration_tensor
            ])
            .map_err(|error| format!("GAME encoder inference failed: {error}"))?;

        let (x_seg_shape, x_seg_data) = encoder_outputs
            .get("x_seg")
            .ok_or_else(|| "GAME encoder did not return x_seg".to_string())?
            .try_extract_tensor::<f32>()
            .map_err(|error| format!("extract GAME x_seg failed: {error}"))?;
        let (_x_est_shape, x_est_data) = encoder_outputs
            .get("x_est")
            .ok_or_else(|| "GAME encoder did not return x_est".to_string())?
            .try_extract_tensor::<f32>()
            .map_err(|error| format!("extract GAME x_est failed: {error}"))?;
        let (_mask_shape, mask_t_data) = encoder_outputs
            .get("maskT")
            .ok_or_else(|| "GAME encoder did not return maskT".to_string())?
            .try_extract_tensor::<bool>()
            .map_err(|error| format!("extract GAME maskT failed: {error}"))?;

        let shape: Vec<usize> = x_seg_shape
            .iter()
            .map(|value| (*value).max(0) as usize)
            .collect();
        let time_frames = shape.get(1).copied().unwrap_or(mask_t_data.len());
        let channels = shape
            .get(2)
            .copied()
            .unwrap_or(sessions.config.embedding_dim);
        if time_frames == 0 || channels == 0 {
            return Ok(Vec::new());
        }
        let feature_len = time_frames.saturating_mul(channels);
        if x_seg_data.len() < feature_len || x_est_data.len() < feature_len {
            return Err("GAME encoder returned truncated feature tensors".to_string());
        }
        let x_seg = x_seg_data[..feature_len].to_vec();
        let x_est = x_est_data[..feature_len].to_vec();
        let mask_t = mask_t_data[..time_frames.min(mask_t_data.len())].to_vec();
        drop(encoder_outputs);

        let mut previous_boundaries = vec![false; time_frames];
        let known_boundaries = vec![false; time_frames];
        let steps = if sessions.config.loop_enabled {
            options.d3pm_steps.max(1)
        } else {
            1
        };
        for step in 0..steps {
            let timestep = if sessions.config.loop_enabled {
                step as f32 / steps as f32
            } else {
                0.0
            };
            let outputs = sessions
                .segmenter
                .run(ort::inputs![
                    "x_seg" => Tensor::from_array(([1usize, time_frames, channels], x_seg.clone().into_boxed_slice())).map_err(|error| format!("build GAME x_seg input failed: {error}"))?,
                    "maskT" => Tensor::from_array(([1usize, time_frames], mask_t.clone())).map_err(|error| format!("build GAME maskT input failed: {error}"))?,
                    "known_boundaries" => Tensor::from_array(([1usize, time_frames], known_boundaries.clone())).map_err(|error| format!("build GAME known-boundary input failed: {error}"))?,
                    "prev_boundaries" => Tensor::from_array(([1usize, time_frames], previous_boundaries.clone())).map_err(|error| format!("build GAME previous-boundary input failed: {error}"))?,
                    "threshold" => Tensor::from_array(((), vec![options.segmentation_threshold])).map_err(|error| format!("build GAME segmentation threshold failed: {error}"))?,
                    "radius" => Tensor::from_array(((), vec![options.segmentation_radius])).map_err(|error| format!("build GAME segmentation radius failed: {error}"))?,
                    "t" => Tensor::from_array(([1usize], vec![timestep])).map_err(|error| format!("build GAME diffusion timestep failed: {error}"))?,
                    "language" => Tensor::from_array(([1usize], vec![options.language])).map_err(|error| format!("build GAME language failed: {error}"))?
                ])
                .map_err(|error| format!("GAME segmenter inference failed: {error}"))?;
            let boundary_output = outputs
                .get("boundaries")
                .or_else(|| outputs.get("output"))
                .unwrap_or(&outputs[0]);
            let (_, data) = boundary_output
                .try_extract_tensor::<bool>()
                .map_err(|error| format!("extract GAME boundaries failed: {error}"))?;
            previous_boundaries.clear();
            previous_boundaries.extend(data.iter().copied().take(time_frames));
            previous_boundaries.resize(time_frames, false);
        }

        let duration_outputs = sessions
            .bd2dur
            .run(ort::inputs![
                "boundaries" => Tensor::from_array(([1usize, time_frames], previous_boundaries.clone())).map_err(|error| format!("build GAME boundary tensor failed: {error}"))?,
                "maskT" => Tensor::from_array(([1usize, time_frames], mask_t.clone())).map_err(|error| format!("build GAME duration mask failed: {error}"))?
            ])
            .map_err(|error| format!("GAME boundary-to-duration inference failed: {error}"))?;
        let duration_value = duration_outputs
            .get("durations")
            .unwrap_or(&duration_outputs[0]);
        let (_, duration_data) = duration_value
            .try_extract_tensor::<f32>()
            .map_err(|error| format!("extract GAME durations failed: {error}"))?;
        let durations = duration_data.to_vec();
        let mask_n = if let Some(value) = duration_outputs.get("maskN") {
            value
                .try_extract_tensor::<bool>()
                .map_err(|error| format!("extract GAME maskN failed: {error}"))?
                .1
                .to_vec()
        } else {
            vec![true; durations.len()]
        };
        drop(duration_outputs);

        let note_count = durations.len().min(mask_n.len());
        if note_count == 0 {
            return Ok(Vec::new());
        }
        let estimator_outputs = sessions
            .estimator
            .run(ort::inputs![
                "x_est" => Tensor::from_array(([1usize, time_frames, channels], x_est.into_boxed_slice())).map_err(|error| format!("build GAME x_est input failed: {error}"))?,
                "maskT" => Tensor::from_array(([1usize, time_frames], mask_t)).map_err(|error| format!("build GAME estimator time mask failed: {error}"))?,
                "boundaries" => Tensor::from_array(([1usize, time_frames], previous_boundaries)).map_err(|error| format!("build GAME estimator boundaries failed: {error}"))?,
                "maskN" => Tensor::from_array(([1usize, note_count], mask_n[..note_count].to_vec())).map_err(|error| format!("build GAME estimator note mask failed: {error}"))?,
                "threshold" => Tensor::from_array(([1usize], vec![options.estimation_threshold])).map_err(|error| format!("build GAME estimator threshold failed: {error}"))?
            ])
            .map_err(|error| format!("GAME estimator inference failed: {error}"))?;
        let (_, score_data) = estimator_outputs
            .get("scores")
            .ok_or_else(|| "GAME estimator did not return scores".to_string())?
            .try_extract_tensor::<f32>()
            .map_err(|error| format!("extract GAME scores failed: {error}"))?;
        let presence = if let Some(value) = estimator_outputs.get("presence") {
            value
                .try_extract_tensor::<bool>()
                .map_err(|error| format!("extract GAME presence failed: {error}"))?
                .1
                .to_vec()
        } else {
            vec![true; score_data.len()]
        };
        let final_mask = if let Some(value) = estimator_outputs.get("maskN") {
            value
                .try_extract_tensor::<bool>()
                .map_err(|error| format!("extract GAME final mask failed: {error}"))?
                .1
                .to_vec()
        } else {
            mask_n
        };

        let count = durations
            .len()
            .min(score_data.len())
            .min(presence.len())
            .min(final_mask.len());
        let mut notes = Vec::with_capacity(count);
        let mut cursor = chunk_start_sec;
        for index in 0..count {
            if !final_mask[index] {
                break;
            }
            let note_duration = durations[index].max(0.0) as f64;
            let end = cursor + note_duration;
            if note_duration > 0.001 && score_data[index].is_finite() {
                notes.push(GameNote {
                    start_sec: cursor,
                    end_sec: end,
                    midi_note: score_data[index],
                    is_rest: !presence[index],
                    confidence: if presence[index] { 1.0 } else { 0.0 },
                });
            }
            cursor = end;
        }
        Ok(notes)
    }

    pub fn detect_notes(
        mono: &[f32],
        sample_rate: u32,
        options: GameOptions,
    ) -> Result<Vec<GameNote>, String> {
        if sample_rate == 0 || mono.is_empty() {
            return Ok(Vec::new());
        }
        with_sessions(options.performance_mode, |sessions| {
            let waveform = linear_resample(mono, sample_rate, sessions.config.samplerate);
            let samples_per_frame =
                (sessions.config.samplerate as f32 * sessions.config.timestep)
                    .round()
                    .max(1.0) as usize;
            let chunks = make_chunks(
                waveform.len(),
                MAX_ENCODER_FRAMES.saturating_mul(samples_per_frame),
            );
            let mut notes = Vec::new();
            for (start, end) in chunks {
                let start_sec = start as f64 / sessions.config.samplerate as f64;
                notes.extend(process_chunk(
                    sessions,
                    &waveform[start..end],
                    start_sec,
                    options,
                )?);
            }
            notes.sort_by(|left, right| left.start_sec.total_cmp(&right.start_sec));
            Ok(notes)
        })
    }

    pub fn status() -> GameStatus {
        let large = resolve_model_dir(false);
        let small = resolve_model_dir(true);
        let available = large.is_ok();
        let performance_available = small.is_ok();
        let message = match (&large, &small) {
            (Ok(_), Ok(_)) => "GAME large is the default; small performance mode is ready".to_string(),
            (Err(error), _) => error.clone(),
            (_, Err(error)) => error.clone(),
        };
        GameStatus {
            available,
            model_dir: large.ok().map(|path| path.display().to_string()),
            performance_available,
            performance_model_dir: small.ok().map(|path| path.display().to_string()),
            message,
        }
    }
}

#[cfg(feature = "onnx")]
pub use implementation::{detect_notes, status};

#[cfg(not(feature = "onnx"))]
pub fn detect_notes(
    _mono: &[f32],
    _sample_rate: u32,
    _options: GameOptions,
) -> Result<Vec<GameNote>, String> {
    Err("GAME inference requires the ONNX build feature".to_string())
}

#[cfg(not(feature = "onnx"))]
pub fn status() -> GameStatus {
    GameStatus {
        available: false,
        model_dir: None,
        performance_available: false,
        performance_model_dir: None,
        message: "GAME inference is disabled in this build".to_string(),
    }
}
