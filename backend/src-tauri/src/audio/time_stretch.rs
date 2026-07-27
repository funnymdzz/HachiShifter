use rustfft::{num_complex::Complex32, FftPlanner};
use std::sync::{Mutex, OnceLock};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UserStretchAlgorithm {
    Linear,
    Signalsmith,
    Soundtouch,
    /// Transient-aware note-object workflow reconstructed from the Melodyne
    /// analysis.  Signalsmith supplies the phase-coherent periodic path while
    /// short attacks are re-anchored from the source below.
    MelodyneHybrid,
    /// Preserve the fixed consonant and extend the remaining vowel/tail by
    /// looping it with short equal-power crossfades.
    Loop,
    /// NSF-HiFiGAN-only stretch which changes the analysis Mel hop size and
    /// lets the vocoder synthesize the requested duration directly.
    HifiganMelHop,
}

impl Default for UserStretchAlgorithm {
    fn default() -> Self {
        Self::Signalsmith
    }
}

impl UserStretchAlgorithm {
    pub fn to_runtime(self) -> StretchAlgorithm {
        match self {
            Self::Linear => StretchAlgorithm::LinearResample,
            Self::Signalsmith => StretchAlgorithm::SignalsmithStretch,
            Self::Soundtouch => StretchAlgorithm::SoundTouchDll,
            Self::MelodyneHybrid => StretchAlgorithm::MelodyneHybrid,
            Self::Loop => StretchAlgorithm::LoopVowel,
            // External callers can still reach this branch for fixed-prefix
            // material. Use the transient-aware path there; the HiFiGAN
            // processor handles the regular variable-hop path internally.
            Self::HifiganMelHop => StretchAlgorithm::MelodyneHybrid,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StretchAlgorithm {
    /// Current fallback: linear resampling in the time domain.
    /// NOTE: This changes pitch/formants when the ratio != 1.
    LinearResample,

    /// High-quality time-stretch (pitch-preserving) via Signalsmith Stretch (MIT).
    ///
    /// Implementation uses a C wrapper over the header-only C++ library,
    /// statically linked at compile time. Always available.
    SignalsmithStretch,

    /// Default time-stretch implementation via SoundTouch Windows DLL.
    SoundTouchDll,

    /// Phase-coherent stretch plus source-aligned transient anchoring.
    MelodyneHybrid,

    /// Crossfaded looping, intended for sustained vowels or user-selected
    /// cyclic material.  Fixed consonants are handled by
    /// `time_stretch_with_fixed_prefix` before this stage.
    LoopVowel,

    /// Desired: zplane Elastique (Soloist) time-stretch preserving pitch + formants.
    /// This requires integrating the Elastique SDK (commercial).
    ElastiqueSoloist,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeStretchSettings {
    pub default_algorithm: UserStretchAlgorithm,
    pub default_hifigan_mel_stretch: bool,
    pub project_algorithm_override: Option<UserStretchAlgorithm>,
    pub project_hifigan_mel_stretch_override: Option<bool>,
}

impl Default for RuntimeStretchSettings {
    fn default() -> Self {
        Self {
            default_algorithm: UserStretchAlgorithm::default(),
            default_hifigan_mel_stretch: true,
            project_algorithm_override: None,
            project_hifigan_mel_stretch_override: None,
        }
    }
}

impl RuntimeStretchSettings {
    pub fn effective_algorithm(self) -> UserStretchAlgorithm {
        self.project_algorithm_override
            .unwrap_or(self.default_algorithm)
    }

    pub fn effective_hifigan_mel_stretch(self) -> bool {
        self.project_hifigan_mel_stretch_override
            .unwrap_or(self.default_hifigan_mel_stretch)
    }
}

fn runtime_stretch_settings_cell() -> &'static Mutex<RuntimeStretchSettings> {
    static CELL: OnceLock<Mutex<RuntimeStretchSettings>> = OnceLock::new();
    CELL.get_or_init(|| Mutex::new(RuntimeStretchSettings::default()))
}

pub fn current_runtime_stretch_settings() -> RuntimeStretchSettings {
    *runtime_stretch_settings_cell()
        .lock()
        .unwrap_or_else(|e| e.into_inner())
}

pub fn update_runtime_stretch_settings(
    default_algorithm: UserStretchAlgorithm,
    default_hifigan_mel_stretch: bool,
    project_algorithm_override: Option<UserStretchAlgorithm>,
    project_hifigan_mel_stretch_override: Option<bool>,
) {
    let mut settings = runtime_stretch_settings_cell()
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    settings.default_algorithm = default_algorithm;
    settings.default_hifigan_mel_stretch = default_hifigan_mel_stretch;
    settings.project_algorithm_override = project_algorithm_override;
    settings.project_hifigan_mel_stretch_override = project_hifigan_mel_stretch_override;
}

pub fn update_global_stretch_defaults(
    default_algorithm: UserStretchAlgorithm,
    default_hifigan_mel_stretch: bool,
) {
    let mut settings = runtime_stretch_settings_cell()
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    settings.default_algorithm = default_algorithm;
    settings.default_hifigan_mel_stretch = default_hifigan_mel_stretch;
}

pub fn update_project_stretch_overrides(
    project_algorithm_override: Option<UserStretchAlgorithm>,
    project_hifigan_mel_stretch_override: Option<bool>,
) {
    let mut settings = runtime_stretch_settings_cell()
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    settings.project_algorithm_override = project_algorithm_override;
    settings.project_hifigan_mel_stretch_override = project_hifigan_mel_stretch_override;
}

pub fn resolved_external_stretch_algorithm() -> StretchAlgorithm {
    current_runtime_stretch_settings()
        .effective_algorithm()
        .to_runtime()
}

pub fn resolved_user_external_stretch_algorithm() -> UserStretchAlgorithm {
    current_runtime_stretch_settings().effective_algorithm()
}

pub fn should_use_hifigan_mel_stretch() -> bool {
    current_runtime_stretch_settings().effective_algorithm()
        == UserStretchAlgorithm::HifiganMelHop
}

const STRETCH_SILENCE_WINDOW_MS: f64 = 10.0;
const STRETCH_MIN_SILENCE_MS: f64 = 20.0;
const STRETCH_SILENCE_RMS: f32 = 1.0e-4;

fn env_f32(name: &str) -> Option<f32> {
    std::env::var(name)
        .ok()
        .and_then(|s| s.trim().parse::<f32>().ok())
}

fn preserve_hard_silence_after_stretch(
    input: &[f32],
    output: &mut [f32],
    channels: usize,
    sample_rate: u32,
) {
    if input.is_empty() || output.is_empty() || channels == 0 {
        return;
    }

    let in_frames = input.len() / channels;
    let out_frames = output.len() / channels;
    if in_frames == 0 || out_frames == 0 {
        return;
    }

    let silence_rms = env_f32("HACHISHIFTER_STRETCH_SILENCE_RMS")
        .unwrap_or(STRETCH_SILENCE_RMS)
        .max(0.0);
    let window_frames = ((sample_rate.max(1) as f64) * (STRETCH_SILENCE_WINDOW_MS / 1000.0))
        .round()
        .max(1.0) as usize;
    let min_silence_blocks = (STRETCH_MIN_SILENCE_MS / STRETCH_SILENCE_WINDOW_MS)
        .round()
        .max(1.0) as usize;

    let block_count = in_frames.div_ceil(window_frames);
    let mut silent_blocks = vec![false; block_count];

    for (block_index, silent) in silent_blocks.iter_mut().enumerate() {
        let start_frame = block_index.saturating_mul(window_frames);
        let end_frame = (start_frame + window_frames).min(in_frames);
        if start_frame >= end_frame {
            continue;
        }

        let mut energy = 0.0f64;
        let mut sample_count = 0usize;
        for frame in start_frame..end_frame {
            let base = frame * channels;
            for channel in 0..channels {
                let sample = input[base + channel] as f64;
                energy += sample * sample;
                sample_count += 1;
            }
        }

        if sample_count == 0 {
            continue;
        }

        let rms = (energy / sample_count as f64).sqrt() as f32;
        *silent = rms <= silence_rms;
    }

    let mut run_start: Option<usize> = None;
    for index in 0..=silent_blocks.len() {
        let is_silent = silent_blocks.get(index).copied().unwrap_or(false);
        match (run_start, is_silent) {
            (None, true) => run_start = Some(index),
            (Some(start), false) => {
                if index.saturating_sub(start) < min_silence_blocks {
                    for block in &mut silent_blocks[start..index] {
                        *block = false;
                    }
                }
                run_start = None;
            }
            _ => {}
        }
    }

    if !silent_blocks.iter().any(|&silent| silent) {
        return;
    }

    let scale = if out_frames <= 1 || in_frames <= 1 {
        0.0
    } else {
        (in_frames - 1) as f64 / (out_frames - 1) as f64
    };

    for out_frame in 0..out_frames {
        let source_frame = if out_frames <= 1 || in_frames <= 1 {
            0
        } else {
            ((out_frame as f64) * scale)
                .round()
                .clamp(0.0, (in_frames - 1) as f64) as usize
        };
        let block_index = (source_frame / window_frames).min(silent_blocks.len() - 1);
        if !silent_blocks[block_index] {
            continue;
        }
        let base = out_frame * channels;
        for channel in 0..channels {
            output[base + channel] = 0.0;
        }
    }
}

fn signalsmith_stretch(
    input: &[f32],
    channels: usize,
    sample_rate: u32,
    out_frames: usize,
) -> Vec<f32> {
    let in_frames = if channels == 0 { 0 } else { input.len() / channels };
    if in_frames < 2 || out_frames < 2 {
        return linear_time_stretch_interleaved(input, channels, out_frames);
    }
    let ratio = (out_frames as f64) / (in_frames as f64);
    let result = crate::sstretch::try_time_stretch_interleaved_realtime(
        input,
        channels,
        sample_rate.max(1),
        ratio,
        out_frames,
    )
    .or_else(|_| {
        crate::sstretch::try_time_stretch_interleaved_offline(
            input,
            channels,
            sample_rate.max(1),
            ratio,
            out_frames,
        )
    });
    match result {
        Ok(mut out) => {
            preserve_hard_silence_after_stretch(input, &mut out, channels, sample_rate.max(1));
            out.resize(out_frames * channels, 0.0);
            out
        }
        Err(e) => {
            if std::env::var("HACHISHIFTER_DEBUG_COMMANDS").ok().as_deref() == Some("1") {
                eprintln!("time_stretch: SignalsmithStretch failed, falling back: {e}");
            }
            linear_time_stretch_interleaved(input, channels, out_frames)
        }
    }
}

/// Re-anchor short high-flux attacks after the periodic/phase-coherent path.
/// This follows the analysed Melodyne split between stable periodic material
/// and attack/noise material without depending on proprietary constants.
pub(crate) fn anchor_transients(
    input: &[f32],
    output: &mut [f32],
    channels: usize,
    sample_rate: u32,
) {
    if channels == 0 || input.len() < channels * 8 || output.len() < channels * 8 {
        return;
    }
    let in_frames = input.len() / channels;
    let out_frames = output.len() / channels;
    let block = ((sample_rate as usize) / 200).max(8); // about 5 ms
    let mut flux = Vec::new();
    let mut previous = 0.0f32;
    for start in (0..in_frames).step_by(block) {
        let end = (start + block).min(in_frames);
        let mut energy = 0.0f32;
        for frame in start..end {
            for channel in 0..channels {
                energy += input[frame * channels + channel].abs();
            }
        }
        energy /= ((end - start).max(1) * channels) as f32;
        flux.push((start, (energy - previous).max(0.0)));
        previous = energy;
    }
    if flux.is_empty() {
        return;
    }
    let mean = flux.iter().map(|(_, value)| *value).sum::<f32>() / flux.len() as f32;
    let threshold = (mean * 3.0).max(0.004);
    let attack_frames = ((sample_rate as usize) * 8 / 1000).max(8);
    for (source_center, value) in flux {
        if value < threshold {
            continue;
        }
        let target_center = ((source_center as f64) * out_frames as f64 / in_frames as f64)
            .round() as usize;
        for offset in 0..attack_frames {
            let source_frame = source_center.saturating_add(offset);
            let target_frame = target_center.saturating_add(offset);
            if source_frame >= in_frames || target_frame >= out_frames {
                break;
            }
            let phase = offset as f32 / attack_frames as f32;
            let weight = (1.0 - phase).powi(2).clamp(0.0, 1.0);
            for channel in 0..channels {
                let src = input[source_frame * channels + channel];
                let dst = &mut output[target_frame * channels + channel];
                *dst = *dst * (1.0 - weight) + src * weight;
            }
        }
    }
}

/// Reinsert only the non-periodic/high-frequency part of detected attacks.
/// Keeping WORLD's low-frequency periodic component avoids leaking the old F0
/// back into a pitch-shifted note, while source sibilants and plosive edges
/// retain their original timing and phase reset.
pub(crate) fn anchor_mld5_attack_residuals(
    input: &[f32],
    output: &mut [f32],
    channels: usize,
    sample_rate: u32,
) {
    if channels == 0 || input.len() < channels * 16 || output.len() < channels * 16 {
        return;
    }
    let in_frames = input.len() / channels;
    let out_frames = output.len() / channels;
    let alpha = (-2.0f32 * std::f32::consts::PI * 1_600.0 / sample_rate.max(1) as f32).exp();
    let mut input_high = vec![0.0f32; input.len()];
    let mut output_low = vec![0.0f32; output.len()];
    let mut output_high = vec![0.0f32; output.len()];
    for channel in 0..channels {
        let mut source_low = input[channel];
        for frame in 0..in_frames {
            let index = frame * channels + channel;
            source_low = (1.0 - alpha) * input[index] + alpha * source_low;
            input_high[index] = input[index] - source_low;
        }
        let mut synthesized_low = output[channel];
        for frame in 0..out_frames {
            let index = frame * channels + channel;
            synthesized_low = (1.0 - alpha) * output[index] + alpha * synthesized_low;
            output_low[index] = synthesized_low;
            output_high[index] = output[index] - synthesized_low;
        }
    }

    let block = ((sample_rate as usize) / 200).max(8);
    let mut previous_energy = 0.0f32;
    let mut flux = Vec::new();
    for start in (0..in_frames).step_by(block) {
        let end = (start + block).min(in_frames);
        let mut energy = 0.0f32;
        for frame in start..end {
            for channel in 0..channels {
                energy += input_high[frame * channels + channel].abs();
            }
        }
        energy /= ((end - start).max(1) * channels) as f32;
        flux.push((start, (energy - previous_energy).max(0.0)));
        previous_energy = energy;
    }
    if flux.is_empty() {
        return;
    }
    let mean_flux = flux.iter().map(|(_, value)| *value).sum::<f32>() / flux.len() as f32;
    let threshold = (mean_flux * 2.8).max(0.0015);
    let attack_frames = ((sample_rate as usize) * 12 / 1000).max(8);
    for (source_center, value) in flux {
        if value < threshold {
            continue;
        }
        let target_center = ((source_center as f64) * out_frames as f64 / in_frames as f64)
            .round() as usize;
        for offset in 0..attack_frames {
            let source_frame = source_center + offset;
            let target_frame = target_center + offset;
            if source_frame >= in_frames || target_frame >= out_frames {
                break;
            }
            let phase = offset as f32 / attack_frames.max(1) as f32;
            let weight = (1.0 - phase).powi(2) * 0.92;
            for channel in 0..channels {
                let source_index = source_frame * channels + channel;
                let target_index = target_frame * channels + channel;
                let high = output_high[target_index] * (1.0 - weight)
                    + input_high[source_index] * weight;
                output[target_index] = output_low[target_index] + high;
            }
        }
    }
}

/// Match the slowly varying cepstral spectral envelope of synthesized speech
/// to its source while keeping the synthesized phase/fundamental.  Melodyne's
/// analysed pipeline treats pitch and formants as independent functions; this
/// STFT/real-cepstrum pass implements the same separation for the model-free
/// mld5 path.
pub(crate) fn preserve_mld5_cepstral_formants(
    reference: &[f32],
    output: &mut [f32],
    channels: usize,
    sample_rate: u32,
    amount: f32,
) {
    let channels = channels.max(1);
    let reference_frames = reference.len() / channels;
    let output_frames = output.len() / channels;
    if reference_frames < 256 || output_frames < 256 || sample_rate == 0 {
        return;
    }
    let nominal = ((sample_rate as usize * 23) / 1000).max(256);
    let fft_size = nominal.next_power_of_two().clamp(512, 2048);
    if reference_frames < fft_size / 2 || output_frames < fft_size / 2 {
        return;
    }
    let hop = fft_size / 4;
    let amount = amount.clamp(0.0, 1.0);
    let lifter = ((sample_rate as usize * 13) / 10_000)
        .clamp(24, fft_size / 6); // about 1.3 ms of low quefrency
    let window: Vec<f32> = (0..fft_size)
        .map(|index| {
            0.5 - 0.5
                * (2.0 * std::f32::consts::PI * index as f32 / fft_size as f32).cos()
        })
        .collect();
    let mut planner = FftPlanner::<f32>::new();
    let forward = planner.plan_fft_forward(fft_size);
    let inverse = planner.plan_fft_inverse(fft_size);

    for channel in 0..channels {
        let before_rms = (output
            .iter()
            .skip(channel)
            .step_by(channels)
            .map(|value| value * value)
            .sum::<f32>()
            / output_frames.max(1) as f32)
            .sqrt();
        let mut reconstructed = vec![0.0f32; output_frames];
        let mut normalization = vec![0.0f32; output_frames];
        let mut output_spectrum = vec![Complex32::new(0.0, 0.0); fft_size];
        let mut reference_spectrum = vec![Complex32::new(0.0, 0.0); fft_size];
        let mut output_cepstrum = vec![Complex32::new(0.0, 0.0); fft_size];
        let mut reference_cepstrum = vec![Complex32::new(0.0, 0.0); fft_size];

        let mut output_start = 0usize;
        while output_start < output_frames {
            let output_center = output_start.saturating_add(fft_size / 2);
            let reference_center = ((output_center as f64 / output_frames.max(1) as f64)
                * reference_frames as f64)
                .round() as usize;
            let reference_start = reference_center.saturating_sub(fft_size / 2);
            for bin in 0..fft_size {
                let output_frame = output_start + bin;
                let reference_frame = reference_start + bin;
                output_spectrum[bin] = Complex32::new(
                    output
                        .get(output_frame * channels + channel)
                        .copied()
                        .unwrap_or(0.0)
                        * window[bin],
                    0.0,
                );
                reference_spectrum[bin] = Complex32::new(
                    reference
                        .get(reference_frame * channels + channel)
                        .copied()
                        .unwrap_or(0.0)
                        * window[bin],
                    0.0,
                );
            }
            forward.process(&mut output_spectrum);
            forward.process(&mut reference_spectrum);
            for bin in 0..fft_size {
                output_cepstrum[bin] =
                    Complex32::new(output_spectrum[bin].norm().max(1e-8).ln(), 0.0);
                reference_cepstrum[bin] =
                    Complex32::new(reference_spectrum[bin].norm().max(1e-8).ln(), 0.0);
            }
            inverse.process(&mut output_cepstrum);
            inverse.process(&mut reference_cepstrum);
            let inverse_scale = 1.0 / fft_size as f32;
            for bin in 0..fft_size {
                let keep = bin <= lifter || bin >= fft_size.saturating_sub(lifter);
                if keep {
                    output_cepstrum[bin] *= inverse_scale;
                    reference_cepstrum[bin] *= inverse_scale;
                } else {
                    output_cepstrum[bin] = Complex32::new(0.0, 0.0);
                    reference_cepstrum[bin] = Complex32::new(0.0, 0.0);
                }
            }
            forward.process(&mut output_cepstrum);
            forward.process(&mut reference_cepstrum);
            for bin in 0..fft_size {
                let envelope_delta =
                    (reference_cepstrum[bin].re - output_cepstrum[bin].re) * amount;
                let gain = envelope_delta.exp().clamp(0.52, 1.92);
                output_spectrum[bin] *= gain;
            }
            inverse.process(&mut output_spectrum);
            for bin in 0..fft_size {
                let frame = output_start + bin;
                if frame >= output_frames {
                    break;
                }
                let weight = window[bin];
                reconstructed[frame] += output_spectrum[bin].re * inverse_scale * weight;
                normalization[frame] += weight * weight;
            }
            output_start = output_start.saturating_add(hop);
        }

        let mut after_energy = 0.0f32;
        for frame in 0..output_frames {
            let index = frame * channels + channel;
            let corrected = if normalization[frame] > 1e-6 {
                reconstructed[frame] / normalization[frame]
            } else {
                output[index]
            };
            output[index] = output[index] * 0.08 + corrected * 0.92;
            after_energy += output[index] * output[index];
        }
        let after_rms = (after_energy / output_frames.max(1) as f32).sqrt();
        if before_rms > 1e-7 && after_rms > 1e-7 {
            let gain = (before_rms / after_rms).clamp(0.84, 1.18);
            for frame in 0..output_frames {
                output[frame * channels + channel] *= gain;
            }
        }
    }
}

/// Move the low-quefrency spectral envelope independently of F0. Melodyne's
/// project stores `formantOffset` in cents; sampling the envelope at
/// `frequency / 2^(cent/1200)` moves its peaks by that ratio while preserving
/// the synthesized harmonic phases.
pub(crate) fn apply_mld5_formant_curve(
    output: &mut [f32],
    sample_rate: u32,
    segment_start_sec: f64,
    frame_period_ms: f64,
    curve: &[f32],
) {
    if output.len() < 512 || sample_rate == 0 || curve.is_empty() {
        return;
    }
    let fft_size = if sample_rate >= 24_000 { 2048 } else { 1024 };
    if output.len() < fft_size / 2 { return; }
    let hop = fft_size / 4;
    let half = fft_size / 2 + 1;
    let window: Vec<f32> = (0..fft_size).map(|i| {
        0.5 - 0.5 * (2.0 * std::f32::consts::PI * i as f32 / fft_size as f32).cos()
    }).collect();
    let mut planner = FftPlanner::<f32>::new();
    let forward = planner.plan_fft_forward(fft_size);
    let inverse = planner.plan_fft_inverse(fft_size);
    let mut reconstructed = vec![0.0f32; output.len()];
    let mut normalization = vec![0.0f32; output.len()];
    let mut spectrum = vec![Complex32::new(0.0, 0.0); fft_size];
    let mut cepstrum = vec![Complex32::new(0.0, 0.0); fft_size];
    let mut envelope = vec![0.0f32; half];
    let lifter = ((sample_rate as usize * 13) / 10_000).clamp(24, fft_size / 6);
    let inv_n = 1.0 / fft_size as f32;

    for start in (0..output.len()).step_by(hop) {
        let center_sec = segment_start_sec + (start + fft_size / 2) as f64 / sample_rate as f64;
        let curve_index = ((center_sec.max(0.0) * 1000.0) / frame_period_ms.max(0.1))
            .round().max(0.0) as usize;
        let cents = curve.get(curve_index).copied().unwrap_or(0.0).clamp(-2400.0, 2400.0);
        for i in 0..fft_size {
            spectrum[i] = Complex32::new(
                output.get(start + i).copied().unwrap_or(0.0) * window[i], 0.0,
            );
        }
        forward.process(&mut spectrum);
        if cents.abs() >= 0.5 {
            for i in 0..fft_size {
                cepstrum[i] = Complex32::new(spectrum[i].norm().max(1e-8).ln(), 0.0);
            }
            inverse.process(&mut cepstrum);
            for i in 0..fft_size {
                if i <= lifter || i >= fft_size - lifter {
                    cepstrum[i] *= inv_n;
                } else {
                    cepstrum[i] = Complex32::new(0.0, 0.0);
                }
            }
            forward.process(&mut cepstrum);
            for i in 0..half { envelope[i] = cepstrum[i].re.exp().max(1e-8); }
            let ratio = 2.0f32.powf(cents / 1200.0);
            for i in 0..half {
                let source = (i as f32 / ratio).clamp(0.0, (half - 1) as f32);
                let lo = source.floor() as usize;
                let hi = (lo + 1).min(half - 1);
                let frac = source - lo as f32;
                let shifted = envelope[lo] + (envelope[hi] - envelope[lo]) * frac;
                let gain = (shifted / envelope[i].max(1e-8)).clamp(0.35, 2.85);
                spectrum[i] *= gain;
            }
            for i in 1..half - 1 { spectrum[fft_size - i] = spectrum[i].conj(); }
        }
        inverse.process(&mut spectrum);
        for i in 0..fft_size {
            let frame = start + i;
            if frame >= output.len() { break; }
            reconstructed[frame] += spectrum[i].re * inv_n * window[i];
            normalization[frame] += window[i] * window[i];
        }
    }
    for i in 0..output.len() {
        if normalization[i] > 1e-6 { output[i] = reconstructed[i] / normalization[i]; }
        if !output[i].is_finite() { output[i] = 0.0; }
    }
}

/// Keep the broad spectral tilt of a stretched/synthesized vocal close to its
/// source. This is deliberately gentle: it does not move formant peaks, but it
/// prevents accumulated stretch + pitch processing from turning the voice
/// unnaturally dark ("older") or bright ("younger").
pub(crate) fn stabilize_vocal_timbre(
    input: &[f32],
    output: &mut [f32],
    channels: usize,
    sample_rate: u32,
) {
    let channels = channels.max(1);
    if input.len() < channels * 32 || output.len() < channels * 32 {
        return;
    }
    let alpha = (-2.0f32 * std::f32::consts::PI * 1_400.0 / sample_rate.max(1) as f32).exp();
    for channel in 0..channels {
        let measure = |samples: &[f32]| {
            let mut signal = 0.0f64;
            let mut difference = 0.0f64;
            let mut previous = samples[channel];
            let mut count = 0usize;
            for frame in (channel..samples.len()).step_by(channels) {
                let value = samples[frame];
                signal += (value as f64) * (value as f64);
                let delta = value - previous;
                difference += (delta as f64) * (delta as f64);
                previous = value;
                count += 1;
            }
            let count = count.max(1) as f64;
            ((signal / count).sqrt(), (difference / count).sqrt())
        };
        let (source_rms, source_diff) = measure(input);
        let (before_rms, output_diff) = measure(output);
        if source_rms < 1e-7 || before_rms < 1e-7 {
            continue;
        }
        let source_brightness = source_diff / source_rms;
        let output_brightness = output_diff / before_rms;
        let high_gain = (source_brightness / output_brightness.max(1e-7)).clamp(0.78, 1.28) as f32;
        if (high_gain - 1.0).abs() < 0.015 {
            continue;
        }
        let mut low = output[channel];
        for frame in (channel..output.len()).step_by(channels) {
            let value = output[frame];
            low = (1.0 - alpha) * value + alpha * low;
            output[frame] = low + (value - low) * high_gain;
        }
        let (after_rms, _) = measure(output);
        let level = (before_rms / after_rms.max(1e-7)).clamp(0.85, 1.18) as f32;
        for frame in (channel..output.len()).step_by(channels) {
            output[frame] *= level;
        }
    }
}

fn loop_stretch_interleaved(
    input: &[f32],
    channels: usize,
    out_frames: usize,
) -> Vec<f32> {
    if input.is_empty() || channels == 0 || out_frames == 0 {
        return Vec::new();
    }
    let in_frames = input.len() / channels;
    if out_frames <= in_frames {
        return linear_time_stretch_interleaved(input, channels, out_frames);
    }
    let mut out = vec![0.0f32; out_frames * channels];
    let crossfade = (in_frames / 8).clamp(8, 2048).min(in_frames / 2);
    for frame in 0..out_frames {
        let source_frame = frame % in_frames;
        for channel in 0..channels {
            let mut value = input[source_frame * channels + channel];
            if frame >= in_frames && source_frame < crossfade {
                let previous_frame = in_frames - crossfade + source_frame;
                let phase = source_frame as f32 / crossfade.max(1) as f32;
                let fade_in = (phase * std::f32::consts::FRAC_PI_2).sin();
                let fade_out = (phase * std::f32::consts::FRAC_PI_2).cos();
                value = input[previous_frame * channels + channel] * fade_out
                    + value * fade_in;
            }
            out[frame * channels + channel] = value;
        }
    }
    out
}

pub fn time_stretch_interleaved(
    input: &[f32],
    channels: usize,
    sample_rate: u32,
    out_frames: usize,
    algorithm: StretchAlgorithm,
) -> Vec<f32> {
    match algorithm {
        StretchAlgorithm::LinearResample => {
            linear_time_stretch_interleaved(input, channels, out_frames)
        }
        StretchAlgorithm::SignalsmithStretch => {
            let mut out = signalsmith_stretch(input, channels, sample_rate, out_frames);
            stabilize_vocal_timbre(input, &mut out, channels, sample_rate.max(1));
            out
        }
        StretchAlgorithm::MelodyneHybrid => {
            let mut out = signalsmith_stretch(input, channels, sample_rate, out_frames);
            anchor_transients(input, &mut out, channels, sample_rate.max(1));
            stabilize_vocal_timbre(input, &mut out, channels, sample_rate.max(1));
            out
        }
        StretchAlgorithm::LoopVowel => loop_stretch_interleaved(input, channels, out_frames),
        StretchAlgorithm::SoundTouchDll => {
            let in_frames = if channels == 0 {
                0
            } else {
                input.len() / channels
            };
            if in_frames < 2 || out_frames < 2 {
                return linear_time_stretch_interleaved(input, channels, out_frames);
            }
            let ratio = (out_frames as f64) / (in_frames as f64);
            let result = crate::soundtouch::try_time_stretch_interleaved_realtime(
                input,
                channels,
                sample_rate.max(1),
                ratio,
                out_frames,
            )
            .or_else(|_| {
                crate::soundtouch::try_time_stretch_interleaved_offline(
                    input,
                    channels,
                    sample_rate.max(1),
                    ratio,
                    out_frames,
                )
            });

            match result {
                Ok(mut out) => {
                    preserve_hard_silence_after_stretch(
                        input,
                        &mut out,
                        channels,
                        sample_rate.max(1),
                    );
                    out.resize(out_frames * channels, 0.0);
                    stabilize_vocal_timbre(input, &mut out, channels, sample_rate.max(1));
                    out
                }
                Err(e) => {
                    if std::env::var("HACHISHIFTER_DEBUG_COMMANDS").ok().as_deref() == Some("1") {
                        eprintln!("time_stretch: SoundTouch failed, falling back: {e}");
                    }
                    linear_time_stretch_interleaved(input, channels, out_frames)
                }
            }
        }
        StretchAlgorithm::ElastiqueSoloist => {
            // TODO: integrate Elastique SDK and implement true pitch/formant-preserving stretch.
            // For now, fall back to the existing linear method to keep the app functional.
            linear_time_stretch_interleaved(input, channels, out_frames)
        }
    }
}

/// Render Melodyne's piecewise source-time/warp-time mapping. Each persisted
/// note object owns its source interval and destination interval; rendering
/// them independently preserves manual time handles that a single clip-wide
/// playback-rate ratio would discard.
fn continue_periodic_phase_at_join(
    audio: &mut [f32],
    channels: usize,
    sample_rate: u32,
    boundary: usize,
    available_right: usize,
    connected: bool,
    amplitude_transition_frames: usize,
) {
    if channels == 0 || boundary < 4 || available_right < 2 {
        return;
    }
    let total_frames = audio.len() / channels;
    if boundary >= total_frames {
        return;
    }

    // Melodyne's renderer exposes `continueAllPhasesByDefault` and
    // `resetAllPhasesAtAttack`. For a note connection, continue the measured
    // periodic phase briefly and hand it to the independently warped right
    // element with a raised-cosine blend. For an unconnected cut, use only a
    // compact de-click bridge. No pitch curve is changed here.
    let min_period = (sample_rate as usize / 900).max(2);
    let max_period = (sample_rate as usize / 65).max(min_period + 1);
    let search_window = (sample_rate as usize / 80).max(32); // 12.5 ms
    let mut best_period = 0usize;
    let mut best_corr = -1.0f64;
    for period in min_period..=max_period.min(boundary.saturating_sub(2)) {
        let window = search_window.min(boundary.saturating_sub(period));
        if window < 16 {
            continue;
        }
        let start = boundary - window;
        let mut dot = 0.0f64;
        let mut aa = 0.0f64;
        let mut bb = 0.0f64;
        for frame in start..boundary {
            let older = frame - period;
            let mut a = 0.0f64;
            let mut b = 0.0f64;
            for channel in 0..channels {
                a += audio[older * channels + channel] as f64;
                b += audio[frame * channels + channel] as f64;
            }
            a /= channels as f64;
            b /= channels as f64;
            dot += a * b;
            aa += a * a;
            bb += b * b;
        }
        let corr = dot / (aa * bb).sqrt().max(1e-12);
        if corr > best_corr {
            best_corr = corr;
            best_period = period;
        }
    }

    // This is a phase-continuity/de-click splice, not an automatic crossfade:
    // automatic crossfades are created only where two source regions overlap.
    // A touching discontinuity still needs a compact bridge to avoid a click.
    let requested = if connected { 12 } else { 8 };
    let default_fade = ((sample_rate as usize * requested) / 1000).max(8);
    // `joinsAmplitudes` carries its own transition duration. Honour that
    // persisted boundary ramp while keeping it bounded so it cannot smear a
    // whole note when an old project contains a stale duration.
    let stored_fade = amplitude_transition_frames
        .min((sample_rate as usize / 10).max(8));
    let fade = default_fade
        .max(stored_fade)
        .max(8)
        .min(available_right)
        .min(total_frames - boundary);
    if fade < 2 {
        return;
    }
    let periodic = connected && best_period > 0 && best_corr >= 0.32;
    for i in 0..fade {
        let x = i as f32 / (fade - 1) as f32;
        let right_gain = 0.5 - 0.5 * (std::f32::consts::PI * x).cos();
        let left_gain = 1.0 - right_gain;
        for channel in 0..channels {
            let predictor = if periodic {
                let phase_frame = boundary - best_period + (i % best_period);
                audio[phase_frame * channels + channel]
            } else {
                audio[(boundary - 1) * channels + channel]
            };
            let index = (boundary + i) * channels + channel;
            audio[index] = predictor * left_gain + audio[index] * right_gain;
        }
    }
}

pub fn render_melodyne_warp_segments(
    input: &[f32],
    channels: usize,
    sample_rate: u32,
    clip_start_sec: f64,
    clip_source_start_sec: f64,
    target_frames: usize,
    segments: &[crate::state::MelodyneWarpSegment],
) -> Vec<f32> {
    let channels = channels.max(1);
    let input_frames = input.len() / channels;
    if input_frames == 0 || target_frames == 0 || segments.is_empty() { return input.to_vec(); }
    let mut out = vec![0.0f32; target_frames * channels];
    let mut weights = vec![0.0f32; target_frames];
    let mut rendered_ranges = Vec::<(usize, usize, bool, bool, bool, usize)>::new();
    let edge = ((sample_rate as usize * 5) / 1000).max(4);
    for segment in segments {
        let src_start = ((segment.source_start_sec - clip_source_start_sec).max(0.0)
            * sample_rate as f64).round() as usize;
        let src_end = ((segment.source_end_sec - clip_source_start_sec).max(0.0)
            * sample_rate as f64).round() as usize;
        let src_start = src_start.min(input_frames);
        if src_start >= input_frames { continue; }
        let src_end = src_end.clamp(src_start.saturating_add(1), input_frames);
        if src_end <= src_start { continue; }
        let dst_start = ((segment.timeline_start_sec - clip_start_sec).max(0.0)
            * sample_rate as f64).round() as usize;
        let dst_end = ((segment.timeline_end_sec - clip_start_sec).max(0.0)
            * sample_rate as f64).round() as usize;
        let dst_start = dst_start.min(target_frames);
        if dst_start >= target_frames { continue; }
        let dst_end = dst_end.clamp(dst_start.saturating_add(1), target_frames);
        if dst_end <= dst_start { continue; }
        let piece_frames = dst_end - dst_start;
        let mut stretched = vec![0.0f32; piece_frames * channels];
        let mut mapped_any = false;
        let mut internal_boundaries = Vec::new();
        if segment.time_map_points.len() >= 2 {
            for pair in segment.time_map_points.windows(2) {
                let map_src_start = ((pair[0].source_sec - clip_source_start_sec).max(0.0)
                    * sample_rate as f64).round() as usize;
                let map_src_end = ((pair[1].source_sec - clip_source_start_sec).max(0.0)
                    * sample_rate as f64).round() as usize;
                let map_dst_start = ((pair[0].timeline_sec - clip_start_sec).max(0.0)
                    * sample_rate as f64).round() as usize;
                let map_dst_end = ((pair[1].timeline_sec - clip_start_sec).max(0.0)
                    * sample_rate as f64).round() as usize;
                let map_src_start = map_src_start.clamp(src_start, src_end);
                let map_dst_start = map_dst_start.clamp(dst_start, dst_end);
                if map_src_start >= src_end || map_dst_start >= dst_end {
                    continue;
                }
                let map_src_end = map_src_end.clamp(map_src_start + 1, src_end);
                let map_dst_end = map_dst_end.clamp(map_dst_start + 1, dst_end);
                if map_src_end <= map_src_start || map_dst_end <= map_dst_start {
                    continue;
                }
                let rendered = time_stretch_interleaved(
                    &input[map_src_start * channels..map_src_end * channels],
                    channels,
                    sample_rate,
                    map_dst_end - map_dst_start,
                    StretchAlgorithm::MelodyneHybrid,
                );
                let local_start = map_dst_start.saturating_sub(dst_start);
                let frames = (rendered.len() / channels).min(piece_frames.saturating_sub(local_start));
                stretched[local_start * channels..(local_start + frames) * channels]
                    .copy_from_slice(&rendered[..frames * channels]);
                mapped_any = true;
                if map_dst_end < dst_end {
                    internal_boundaries.push(map_dst_end - dst_start);
                }
            }
        }
        if !mapped_any {
            stretched = time_stretch_interleaved(
                &input[src_start * channels..src_end * channels],
                channels,
                sample_rate,
                piece_frames,
                StretchAlgorithm::MelodyneHybrid,
            );
        } else {
            // Preserve phase through Melodyne's internal attack/sustain warp
            // anchors.  These anchors change time ratio, not pitch or phase.
            for boundary in internal_boundaries {
                continue_periodic_phase_at_join(
                    &mut stretched,
                    channels,
                    sample_rate,
                    boundary,
                    piece_frames.saturating_sub(boundary),
                    true,
                    (sample_rate as usize * 4) / 1000,
                );
            }
        }
        let fade_in = (segment.fade_in_sec.max(0.0) * sample_rate as f64).round() as usize;
        let fade_out = (segment.fade_out_sec.max(0.0) * sample_rate as f64).round() as usize;
        for local in 0..piece_frames {
            let in_gain = if fade_in > 0 && local < fade_in {
                (local as f32 / fade_in.max(1) as f32)
                    .clamp(0.0, 1.0)
                    .powf(segment.fade_in_shape_pow.clamp(0.1, 8.0))
            } else { 1.0 };
            let remaining = piece_frames.saturating_sub(local + 1);
            let out_gain = if fade_out > 0 && remaining < fade_out {
                (remaining as f32 / fade_out.max(1) as f32)
                    .clamp(0.0, 1.0)
                    .powf(segment.fade_out_shape_pow.clamp(0.1, 8.0))
            } else { 1.0 };
            // Apply the element gain before overlap composition. A single
            // track-wide volume curve cannot represent two concurrent source
            // elements and was the cause of level steps at sample joins.
            let gain = in_gain * out_gain * segment.amplitude_factor.max(0.0);
            for channel in 0..channels {
                stretched[local * channels + channel] *= gain;
            }
        }
        rendered_ranges.push((
            dst_start,
            dst_end,
            segment.connected_to_next,
            segment.connected_phase_to_next,
            segment.connected_amplitude_to_next,
            (segment.amplitude_transition_sec.max(0.0) * sample_rate as f64).round() as usize,
        ));
        for local in 0..(dst_end - dst_start) {
            let frame = dst_start + local;
            let left = (local as f32 / edge as f32).clamp(0.0, 1.0);
            let right = ((dst_end - dst_start - 1 - local) as f32 / edge as f32).clamp(0.0, 1.0);
            let weight = (left.min(right) * std::f32::consts::FRAC_PI_2).sin().max(0.001);
            weights[frame] += weight;
            for channel in 0..channels {
                out[frame * channels + channel] += stretched[local * channels + channel] * weight;
            }
        }
    }
    for frame in 0..target_frames {
        if weights[frame] > 1e-6 {
            for channel in 0..channels { out[frame * channels + channel] /= weights[frame]; }
        }
    }
    rendered_ranges.sort_by_key(|range| range.0);
    let join_tolerance = ((sample_rate as usize * 2) / 1000).max(2);
    for pair in rendered_ranges.windows(2) {
        let (left_start, left_end, connected, joins_phase, joins_amplitude, amplitude_transition_frames) = pair[0];
        let (right_start, right_end, _, _, _, _) = pair[1];
        if left_end <= left_start || right_end <= right_start {
            continue;
        }
        // Preserve intentional rests. Touching notes and tiny rounding gaps
        // are treated as one acoustic connection.
        if right_start > left_end.saturating_add(join_tolerance) {
            continue;
        }
        // Actual overlaps have already been gain-balanced by the weighted
        // source composition above. Do not add a synthetic bridge unless the
        // MPD explicitly requests phase/pitch continuity.
        if right_start < left_end && !(connected || joins_phase) {
            continue;
        }
        continue_periodic_phase_at_join(
            &mut out,
            channels,
            sample_rate,
            right_start,
            right_end.saturating_sub(right_start),
            connected || joins_phase,
            if joins_amplitude { amplitude_transition_frames } else { 0 },
        );
    }
    out
}

/// Preserve the source prefix at 1:1 speed and apply the selected stretcher
/// only to the remainder. This implements UTAU-style fixed consonants while
/// keeping the existing clip length contract.
pub fn time_stretch_with_fixed_prefix(
    input_interleaved: &[f32],
    channels: usize,
    sample_rate: u32,
    target_frames: usize,
    fixed_prefix_frames: usize,
    algorithm: StretchAlgorithm,
) -> Vec<f32> {
    let channels = channels.max(1);
    let input_frames = input_interleaved.len() / channels;
    if input_frames == 0 || target_frames == 0 {
        return Vec::new();
    }
    let fixed_frames = fixed_prefix_frames
        .min(input_frames.saturating_sub(1))
        .min(target_frames.saturating_sub(1));
    if fixed_frames == 0 {
        return time_stretch_interleaved(
            input_interleaved,
            channels,
            sample_rate,
            target_frames,
            algorithm,
        );
    }

    let fixed_samples = fixed_frames * channels;
    let tail_target_frames = target_frames - fixed_frames;
    let stretched_tail = time_stretch_interleaved(
        &input_interleaved[fixed_samples..],
        channels,
        sample_rate,
        tail_target_frames,
        algorithm,
    );
    let mut out = Vec::with_capacity(target_frames * channels);
    out.extend_from_slice(&input_interleaved[..fixed_samples]);
    out.extend_from_slice(&stretched_tail);
    out.resize(target_frames * channels, 0.0);
    out.truncate(target_frames * channels);

    // A variable-hop/vocoder tail can have a different phase and DC level at
    // the consonant-vowel boundary. Smooth the splice in-place without moving
    // the annotated boundary or changing the exact output length. The source
    // continuation supplies a stable left candidate while the stretched tail
    // supplies the right candidate; a raised-cosine ramp removes both the
    // waveform jump and the derivative click.
    let fade_frames = ((sample_rate.max(1) as usize * 12) / 1000)
        .max(8)
        .min(fixed_frames)
        .min(tail_target_frames);
    if fade_frames > 1 && stretched_tail.len() >= fade_frames * channels {
        for i in 0..fade_frames {
            let out_frame = fixed_frames + i;
            if out_frame >= target_frames {
                break;
            }
            let x = i as f32 / (fade_frames - 1) as f32;
            let right_gain = 0.5 - 0.5 * (std::f32::consts::PI * x).cos();
            let left_gain = 1.0 - right_gain;
            let source_frame = out_frame.min(input_frames - 1);
            let tail_frame = i.min(tail_target_frames - 1);
            for channel in 0..channels {
                let left = input_interleaved[source_frame * channels + channel];
                let right = stretched_tail[tail_frame * channels + channel];
                out[out_frame * channels + channel] = left * left_gain + right * right_gain;
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::{
        current_runtime_stretch_settings, resolved_external_stretch_algorithm,
        should_use_hifigan_mel_stretch, time_stretch_interleaved,
        time_stretch_with_fixed_prefix, update_runtime_stretch_settings, StretchAlgorithm,
        UserStretchAlgorithm,
    };

    #[test]
    fn soundtouch_fallback_keeps_requested_length() {
        let input = vec![0.0f32, 0.5, 0.25, -0.25];
        let out = time_stretch_interleaved(&input, 1, 44_100, 8, StretchAlgorithm::SoundTouchDll);
        assert_eq!(out.len(), 8);
    }

    #[test]
    fn fixed_prefix_is_bit_exact_and_only_tail_stretches() {
        let input: Vec<f32> = (0..20).map(|value| value as f32).collect();
        let out =
            time_stretch_with_fixed_prefix(
                &input,
                1,
                44_100,
                30,
                8,
                StretchAlgorithm::LinearResample,
            );
        assert_eq!(out.len(), 30);
        assert_eq!(&out[..8], &input[..8]);
    }

    #[test]
    fn default_algorithm_symbol_exists() {
        let algo = StretchAlgorithm::SoundTouchDll;
        assert!(matches!(algo, StretchAlgorithm::SoundTouchDll));
    }

    #[test]
    fn project_override_inherits_and_resolves_from_global_defaults() {
        update_runtime_stretch_settings(UserStretchAlgorithm::Signalsmith, true, None, None);
        let settings = current_runtime_stretch_settings();
        assert_eq!(
            settings.effective_algorithm(),
            UserStretchAlgorithm::Signalsmith
        );
        assert!(settings.effective_hifigan_mel_stretch());
        assert!(matches!(
            resolved_external_stretch_algorithm(),
            StretchAlgorithm::SignalsmithStretch
        ));
        assert!(!should_use_hifigan_mel_stretch());

        update_runtime_stretch_settings(
            UserStretchAlgorithm::Signalsmith,
            true,
            Some(UserStretchAlgorithm::Linear),
            Some(false),
        );
        let settings = current_runtime_stretch_settings();
        assert_eq!(settings.effective_algorithm(), UserStretchAlgorithm::Linear);
        assert!(!settings.effective_hifigan_mel_stretch());
        assert!(matches!(
            resolved_external_stretch_algorithm(),
            StretchAlgorithm::LinearResample
        ));
        assert!(!should_use_hifigan_mel_stretch());
    }
}

fn linear_time_stretch_interleaved(input: &[f32], channels: usize, out_frames: usize) -> Vec<f32> {
    if input.is_empty() || channels == 0 {
        return vec![];
    }
    let in_frames = input.len() / channels;
    if in_frames == 0 {
        return vec![];
    }
    if in_frames == out_frames {
        return input.to_vec();
    }
    if out_frames <= 1 || in_frames <= 1 {
        let mut out = vec![0.0f32; out_frames * channels];
        let copy_frames = in_frames.min(out_frames);
        out[..copy_frames * channels].copy_from_slice(&input[..copy_frames * channels]);
        return out;
    }

    let mut out = vec![0.0f32; out_frames * channels];
    let scale = (in_frames - 1) as f64 / (out_frames - 1) as f64;

    for of in 0..out_frames {
        let t_in = (of as f64) * scale;
        let i0 = t_in as usize;
        let i1 = (i0 + 1).min(in_frames - 1);
        let frac = (t_in - (i0 as f64)) as f32;

        let base0 = i0 * channels;
        let base1 = i1 * channels;
        let out_base = of * channels;

        for ch in 0..channels {
            let a = input[base0 + ch];
            let b = input[base1 + ch];
            out[out_base + ch] = a + (b - a) * frac;
        }
    }

    out
}
