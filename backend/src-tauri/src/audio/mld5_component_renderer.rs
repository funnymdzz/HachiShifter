//! Monophonic component renderer reconstructed from the observable Melodyne 5
//! pipeline: one persistent source-component phase state, a destination-time
//! pitch-ratio function, independent spectral envelope, transient phase reset,
//! and unshifted aperiodic/sibilant frames.
//!
//! This module intentionally has no WORLD/Harvest/DIO dependency.  Imported
//! MPD pitch and time functions are already analysed data; analysing them a
//! second time is slower and introduces the F0 disagreement reported around
//! portamento and stretched note tails.

use rustfft::{num_complex::Complex32, FftPlanner};

fn wrap_phase(value: f32) -> f32 {
    let tau = std::f32::consts::TAU;
    (value + std::f32::consts::PI).rem_euclid(tau) - std::f32::consts::PI
}

fn curve_linear(curve: &[f32], position: f64) -> Option<f32> {
    if curve.is_empty() || !position.is_finite() || position < 0.0 {
        return None;
    }
    let left = position.floor() as usize;
    if left >= curve.len() {
        return None;
    }
    let right = (left + 1).min(curve.len() - 1);
    let fraction = (position - left as f64).clamp(0.0, 1.0) as f32;
    let a = curve[left];
    let b = curve[right];
    if !(a.is_finite() && b.is_finite() && a > 0.0 && b > 0.0) {
        return None;
    }
    Some(a + (b - a) * fraction)
}

fn signed_curve_linear(curve: Option<&[f32]>, position: f64) -> f32 {
    let Some(curve) = curve else { return 0.0; };
    if curve.is_empty() || !position.is_finite() || position < 0.0 {
        return 0.0;
    }
    let left = (position.floor() as usize).min(curve.len() - 1);
    let right = (left + 1).min(curve.len() - 1);
    let fraction = (position - left as f64).clamp(0.0, 1.0) as f32;
    let a = curve[left];
    let b = curve[right];
    if a.is_finite() && b.is_finite() {
        a + (b - a) * fraction
    } else {
        0.0
    }
}

fn reflected(input: &[f32], position: isize) -> f32 {
    if input.is_empty() {
        return 0.0;
    }
    if input.len() == 1 {
        return input[0];
    }
    let period = (input.len() * 2 - 2) as isize;
    let phase = position.rem_euclid(period);
    let index = if phase < input.len() as isize {
        phase
    } else {
        period - phase
    } as usize;
    input[index]
}

/// Render one already-time-warped monophonic component.
pub fn render(
    input: &[f32],
    sample_rate: u32,
    seg_start_sec: f64,
    clip_start_sec: f64,
    frame_period_ms: f64,
    source_midi: &[f32],
    target_midi_global: &[f32],
    fallback_pitch_delta: Option<&[f32]>,
) -> Vec<f32> {
    if input.len() < 64 || sample_rate == 0 {
        return input.to_vec();
    }

    let fft_size = if sample_rate >= 32_000 { 2048usize } else { 1024usize };
    let hop = fft_size / 8;
    let half = fft_size / 2;
    let pad = fft_size / 2;
    let frame_count = (input.len() + hop - 1) / hop + 1;
    let fp = frame_period_ms.max(0.1);
    let window = (0..fft_size)
        .map(|index| {
            // sqrt-Hann: analysis*synthesis is Hann and normalises cleanly at
            // the 8x overlap used by Melodyne-like component playback.
            (0.5 - 0.5 * (std::f32::consts::TAU * index as f32 / fft_size as f32).cos())
                .max(0.0)
                .sqrt()
        })
        .collect::<Vec<_>>();

    let mut planner = FftPlanner::<f32>::new();
    let forward = planner.plan_fft_forward(fft_size);
    let inverse = planner.plan_fft_inverse(fft_size);
    let mut spectrum = vec![Complex32::new(0.0, 0.0); fft_size];
    let mut shifted = vec![Complex32::new(0.0, 0.0); fft_size];
    let mut previous_phase = vec![0.0f32; half + 1];
    let mut synthesis_phase = vec![0.0f32; half + 1];
    let mut previous_magnitude = vec![0.0f32; half + 1];
    let mut magnitude = vec![0.0f32; half + 1];
    let mut phase = vec![0.0f32; half + 1];
    let mut log_envelope = vec![0.0f32; half + 1];
    let mut mapped_magnitude = vec![0.0f32; half + 1];
    let mut mapped_source_phase = vec![0.0f32; half + 1];
    let mut output = vec![0.0f32; input.len() + fft_size];
    let mut normalisation = vec![0.0f32; output.len()];
    let expected_scale = std::f32::consts::TAU * hop as f32 / fft_size as f32;
    let inverse_scale = 1.0 / fft_size as f32;

    for frame in 0..frame_count {
        let centre = frame * hop;
        let input_start = centre as isize - pad as isize;
        for index in 0..fft_size {
            spectrum[index] = Complex32::new(
                reflected(input, input_start + index as isize) * window[index],
                0.0,
            );
        }
        forward.process(&mut spectrum);
        for bin in 0..=half {
            magnitude[bin] = spectrum[bin].norm().max(1e-9);
            phase[bin] = spectrum[bin].arg();
        }

        // Cepstral-like low-pass envelope in log magnitude.  A frequency
        // width rather than a fixed bin count keeps vowels stable at every
        // supported sample rate.
        let envelope_radius = ((180.0 * fft_size as f32 / sample_rate as f32).round() as usize)
            .clamp(4, 18);
        let mut prefix = vec![0.0f32; half + 2];
        for bin in 0..=half {
            prefix[bin + 1] = prefix[bin] + magnitude[bin].ln();
        }
        for bin in 0..=half {
            let begin = bin.saturating_sub(envelope_radius);
            let end = (bin + envelope_radius + 1).min(half + 1);
            log_envelope[bin] = (prefix[end] - prefix[begin]) / (end - begin).max(1) as f32;
        }

        let abs_sec = seg_start_sec + centre.min(input.len()) as f64 / sample_rate as f64;
        let clip_position = ((abs_sec - clip_start_sec).max(0.0) * 1000.0) / fp;
        let global_position = (abs_sec.max(0.0) * 1000.0) / fp;
        let source = curve_linear(source_midi, clip_position);
        let target = curve_linear(target_midi_global, global_position);
        // An MPD contour gap is an aperiodic/unanalysed component, not an
        // invitation to transpose every FFT bin.  The old fallback shifted
        // breath and consonant noise without confirming an F0, which was the
        // dominant source of the harsh/raspy mld5 output.  A signed fallback
        // is used only when it is effectively neutral; detected-F0 fallback
        // paths populate source_midi before entering this renderer.
        let semitones = match (source, target) {
            (Some(source), Some(target)) => (target - source).clamp(-24.0, 24.0),
            _ => {
                let fallback = signed_curve_linear(fallback_pitch_delta, clip_position);
                if fallback.abs() <= 1e-4 { fallback } else { 0.0 }
            }
        };
        let ratio = 2.0f32.powf(semitones / 12.0).clamp(0.25, 4.0);

        let energy = magnitude.iter().sum::<f32>().max(1e-9);
        let positive_flux = magnitude
            .iter()
            .zip(previous_magnitude.iter())
            .map(|(current, previous)| (current - previous).max(0.0))
            .sum::<f32>() / energy;
        let transient = frame == 0 || positive_flux > 0.34;
        shifted.fill(Complex32::new(0.0, 0.0));

        mapped_magnitude.fill(0.0);
        mapped_source_phase.fill(0.0);
        for output_bin in 0..=half {
            let source_bin_f = output_bin as f32 / ratio;
            if source_bin_f > half as f32 {
                continue;
            }
            let source_left = source_bin_f.floor() as usize;
            let source_right = (source_left + 1).min(half);
            let fraction = source_bin_f - source_left as f32;
            let source_magnitude = magnitude[source_left]
                + (magnitude[source_right] - magnitude[source_left]) * fraction;
            let source_envelope = log_envelope[source_left]
                + (log_envelope[source_right] - log_envelope[source_left]) * fraction;
            let target_envelope = log_envelope[output_bin];
            // Shift harmonic fine structure while leaving the vocal-tract
            // envelope at its original frequency.
            let mapped_mag = source_magnitude
                * (target_envelope - source_envelope).exp().clamp(0.18, 5.5);

            let source_phase = phase[source_left]
                + wrap_phase(phase[source_right] - phase[source_left]) * fraction;
            let previous = previous_phase[source_left]
                + wrap_phase(previous_phase[source_right] - previous_phase[source_left])
                    * fraction;
            let expected = expected_scale * source_bin_f;
            let phase_error = wrap_phase(source_phase - previous - expected);
            let instantaneous = (expected + phase_error) * ratio;
            if transient {
                synthesis_phase[output_bin] = source_phase;
            } else {
                synthesis_phase[output_bin] =
                    wrap_phase(synthesis_phase[output_bin] + instantaneous);
            }
            mapped_magnitude[output_bin] = mapped_mag;
            mapped_source_phase[output_bin] = source_phase;
        }

        // Identity phase locking: advance only spectral-peak phases and keep
        // neighbouring partial/noise bins at their instantaneous phase offset
        // from the owning peak.  Independent per-bin phase propagation made
        // vowels diffuse and metallic after even a small correction.
        let mut peaks = Vec::<usize>::new();
        for bin in 2..half.saturating_sub(1) {
            if magnitude[bin] >= magnitude[bin - 1]
                && magnitude[bin] > magnitude[bin + 1]
                && magnitude[bin] > energy * 1e-5
            {
                peaks.push(bin);
            }
        }
        for output_bin in 0..=half {
            let locked_phase = if transient || peaks.is_empty() {
                synthesis_phase[output_bin]
            } else {
                let source_bin = output_bin as f32 / ratio;
                let peak = peaks
                    .partition_point(|candidate| *candidate as f32 < source_bin)
                    .min(peaks.len());
                let owner = match (peak.checked_sub(1), peaks.get(peak).copied()) {
                    (Some(left), Some(right)) => {
                        let left = peaks[left];
                        if (source_bin - left as f32).abs() <= (right as f32 - source_bin).abs() {
                            left
                        } else {
                            right
                        }
                    }
                    (Some(left), None) => peaks[left],
                    (None, Some(right)) => right,
                    (None, None) => output_bin,
                };
                let owner_output = ((owner as f32 * ratio).round() as usize).min(half);
                synthesis_phase[owner_output]
                    + wrap_phase(mapped_source_phase[output_bin] - phase[owner])
            };
            shifted[output_bin] =
                Complex32::from_polar(mapped_magnitude[output_bin], locked_phase);
        }
        for bin in 1..half {
            shifted[fft_size - bin] = shifted[bin].conj();
        }
        inverse.process(&mut shifted);
        for index in 0..fft_size {
            let destination = centre + index;
            if destination >= output.len() {
                break;
            }
            let weight = window[index];
            output[destination] += shifted[index].re * inverse_scale * weight;
            normalisation[destination] += weight * weight;
        }
        previous_phase.copy_from_slice(&phase);
        previous_magnitude.copy_from_slice(&magnitude);
    }

    let mut cropped = vec![0.0f32; input.len()];
    for index in 0..cropped.len() {
        let source_index = index + pad;
        let norm = normalisation.get(source_index).copied().unwrap_or(0.0);
        let wet = if norm > 1e-6 {
            output[source_index] / norm
        } else {
            input[index]
        };
        let abs_sec = seg_start_sec + index as f64 / sample_rate as f64;
        let clip_position = ((abs_sec - clip_start_sec).max(0.0) * 1000.0) / fp;
        let global_position = (abs_sec.max(0.0) * 1000.0) / fp;
        let shift = match (
            curve_linear(source_midi, clip_position),
            curve_linear(target_midi_global, global_position),
        ) {
            (Some(source), Some(target)) => (target - source).abs(),
            _ => 0.0,
        };
        // Preserve the original waveform at neutral/invalid points and use a
        // short proportional handoff into the component render. This prevents
        // phase-vocoder colour from accumulating across untouched portions of
        // imported projects and removes clicks at MPD contour gaps.
        let blend = (shift / 0.20).clamp(0.0, 1.0);
        cropped[index] = input[index] + (wet - input[index]) * blend;
    }
    cropped
}
