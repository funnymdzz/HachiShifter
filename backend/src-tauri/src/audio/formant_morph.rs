use aberth::{AberthSolver, StopReason};
use crate::state::ClipFormantMorph;
use num_complex::Complex32;
use std::collections::BTreeMap;

const PRE_EMPHASIS_COEF: f32 = 0.97;

const MIN_SAMPLE_RATE: u32 = 8_000;
const MIN_INPUT_SAMPLES: usize = 512;

const F1_MIN_HZ: f32 = 250.0;
const F1_MAX_HZ: f32 = 1_000.0;
const F2_MIN_HZ: f32 = 540.0;
const F2_MAX_HZ: f32 = 2_600.0;

const FORMANT_SEARCH_MIN_HZ: f32 = 150.0;
const FORMANT_SEARCH_MAX_HZ: f32 = 3_000.0;

const MIN_F1_F2_GAP_HZ: f32 = 280.0;

const EPSILON: f32 = 1.0e-8;
const MAX_ROOT_RADIUS: f32 = 0.992;
const MAX_FRAME_GAIN: f32 = 3.5;
const MIN_FRAME_GAIN: f32 = 0.25;
const MAX_IIR_ABS: f32 = 24.0;

#[allow(dead_code)]
pub fn vowel_formant_preset(vowel: &str) -> Option<(f64, f64)> {
    match vowel.trim().to_ascii_lowercase().as_str() {
        "a" | "aa" | "ah" | "啊" | "あ" | "ア" => Some((800.0, 1_200.0)),
        "e" | "eh" | "诶" | "欸" | "え" | "エ" => Some((500.0, 1_900.0)),
        "i" | "ee" | "yi" | "衣" | "い" | "イ" => Some((300.0, 2_300.0)),
        "o" | "oh" | "哦" | "お" | "オ" => Some((500.0, 900.0)),
        "u" | "oo" | "wu" | "乌" | "う" | "ウ" => Some((350.0, 750.0)),
        _ => None,
    }
}

pub fn apply_formant_morph_mono(
    input: &[f32],
    sample_rate: u32,
    params: &ClipFormantMorph,
) -> Result<Vec<f32>, String> {
    if !params.enabled || input.is_empty() {
        return Ok(input.to_vec());
    }

    if sample_rate < MIN_SAMPLE_RATE || input.len() < MIN_INPUT_SAMPLES {
        return Ok(input.to_vec());
    }

    let strength = params.strength.clamp(0.0, 1.0) as f32;
    if strength <= 1.0e-5 {
        return Ok(input.to_vec());
    }

    let (target_f1, target_f2) = sanitize_target_formants(
        params.target_f1_hz as f32,
        params.target_f2_hz as f32,
        sample_rate,
    );

    let frame_len = ((sample_rate as f32) * 0.025)
        .round()
        .clamp(256.0_f32, 2_048.0_f32) as usize;

    let hop_len = (frame_len / 4).max(64);
    let order = lpc_order_for_sample_rate(sample_rate);

    let window = hann_window(frame_len);
    let emphasized = pre_emphasis(input, PRE_EMPHASIS_COEF);
    let padded = pad_for_overlap_add(&emphasized, frame_len, hop_len);

    let mut overlap = vec![0.0_f32; padded.len()];
    let mut window_sum = vec![0.0_f32; padded.len()];

    let total_frames = 1 + padded.len().saturating_sub(frame_len) / hop_len;
    let mut processed_frames = 0usize;
    let mut low_energy_frames = 0usize;
    let mut lpc_ok_frames = 0usize;
    let mut modify_ok_frames = 0usize;
    let mut lpc_fail_reasons: BTreeMap<&'static str, usize> = BTreeMap::new();
    let mut modify_fail_reasons: BTreeMap<&'static str, usize> = BTreeMap::new();

    for start in (0..=padded.len().saturating_sub(frame_len)).step_by(hop_len) {
        let frame = &padded[start..start + frame_len];

        let windowed: Vec<f32> = frame
            .iter()
            .zip(window.iter())
            .map(|(sample, win)| sample * win)
            .collect();

        let mean_energy = frame_energy(&windowed) / frame_len as f32;
        let confidence = voiced_confidence(&windowed, mean_energy);
        let effective_strength = strength * confidence;

        let mut wet_windowed = if mean_energy < 1.0e-8_f32 || effective_strength < 0.015_f32 {
            low_energy_frames += 1;
            windowed.clone()
        } else {
            match lpc_coefficients_with_reason(&windowed, order) {
                Ok(a_orig) => {
                    lpc_ok_frames += 1;

                    match modify_lpc_coefficients_with_reason(
                        &a_orig,
                        sample_rate,
                        target_f1,
                        target_f2,
                        effective_strength,
                    ) {
                        Ok(a_target) => {
                            let residual = fir_filter(&windowed, &a_orig);

                            match all_pole_filter_checked(&residual, &a_target, MAX_IIR_ABS) {
                                Some(mut synthesized) => {
                                    match_energy_limited(
                                        &windowed,
                                        &mut synthesized,
                                        MIN_FRAME_GAIN,
                                        MAX_FRAME_GAIN,
                                    );

                                    let dry_peak = peak_abs(&windowed).max(0.01_f32);
                                    limit_frame(
                                        &mut synthesized,
                                        (dry_peak * 3.2_f32).clamp(0.05_f32, 0.98_f32),
                                    );

                                    let wet_amount = effective_strength.clamp(0.0_f32, 1.0_f32).powf(0.40_f32);

                                    let mut mixed = vec![0.0_f32; frame_len];
                                    for idx in 0..frame_len {
                                        mixed[idx] = windowed[idx]
                                            + wet_amount * (synthesized[idx] - windowed[idx]);
                                    }

                                    limit_frame(
                                        &mut mixed,
                                        (dry_peak * 3.0_f32).clamp(0.05_f32, 0.98_f32),
                                    );

                                    processed_frames += 1;
                                    modify_ok_frames += 1;

                                    mixed
                                }
                                None => {
                                    *modify_fail_reasons
                                        .entry("unstable_synthesis")
                                        .or_default() += 1;
                                    windowed.clone()
                                }
                            }
                        }
                        Err(reason) => {
                            *modify_fail_reasons.entry(reason).or_default() += 1;
                            windowed.clone()
                        }
                    }
                }
                Err(reason) => {
                    *lpc_fail_reasons.entry(reason).or_default() += 1;
                    windowed.clone()
                }
            }
        };

        remove_bad_samples(&mut wet_windowed);

        for idx in 0..frame_len {
            overlap[start + idx] += wet_windowed[idx] * window[idx];
            window_sum[start + idx] += window[idx] * window[idx];
        }
    }

    if processed_frames == 0 {
        if crate::formant_cache::formant_debug_enabled() {
            crate::formant_cache::formant_debug_log(format!(
                "dsp summary sr={} samples={} total_frames={} low_energy={} lpc_ok={} modify_ok={} processed_frames={} final_diff={:.8} lpc_fail={:?} modify_fail={:?} early_return=input",
                sample_rate,
                input.len(),
                total_frames,
                low_energy_frames,
                lpc_ok_frames,
                modify_ok_frames,
                processed_frames,
                0.0_f32,
                lpc_fail_reasons,
                modify_fail_reasons,
            ));
        }

        return Ok(input.to_vec());
    }

    normalize_overlap_add(&mut overlap, &window_sum);

    let mut out = de_emphasis(&overlap[..input.len()], PRE_EMPHASIS_COEF);
    remove_dc(&mut out);
    final_output_protect(&mut out, input);

    if crate::formant_cache::formant_debug_enabled() {
        crate::formant_cache::formant_debug_log(format!(
            "dsp summary sr={} samples={} total_frames={} low_energy={} lpc_ok={} modify_ok={} processed_frames={} final_diff={:.8} lpc_fail={:?} modify_fail={:?}",
            sample_rate,
            input.len(),
            total_frames,
            low_energy_frames,
            lpc_ok_frames,
            modify_ok_frames,
            processed_frames,
            crate::formant_cache::average_abs_diff(input, &out),
            lpc_fail_reasons,
            modify_fail_reasons,
        ));
    }

    Ok(out)
}

pub fn apply_formant_morph_interleaved(
    input: &[f32],
    sample_rate: u32,
    channels: usize,
    params: &ClipFormantMorph,
) -> Result<Vec<f32>, String> {
    if channels == 0 {
        return Err("channels == 0".to_string());
    }

    if channels == 1 {
        return apply_formant_morph_mono(input, sample_rate, params);
    }

    if input.is_empty() || !params.enabled {
        return Ok(input.to_vec());
    }

    let frames = input.len() / channels;
    if frames == 0 {
        return Ok(input.to_vec());
    }

    let mono = average_channels_to_mono(input, channels, frames);
    let processed_mono = apply_formant_morph_mono(&mono, sample_rate, params)?;

    Ok(apply_mono_delta_to_interleaved(
        input,
        channels,
        &mono,
        &processed_mono,
    ))
}

fn sanitize_target_formants(target_f1: f32, target_f2: f32, sample_rate: u32) -> (f32, f32) {
    let nyquist_safe = sample_rate as f32 * 0.5_f32 - 150.0_f32;

    let mut f1 = target_f1
        .clamp(F1_MIN_HZ, F1_MAX_HZ)
        .min(nyquist_safe - MIN_F1_F2_GAP_HZ);

    let mut f2 = target_f2
        .clamp(F2_MIN_HZ, F2_MAX_HZ.min(nyquist_safe))
        .max(f1 + MIN_F1_F2_GAP_HZ);

    if f2 > nyquist_safe {
        f2 = nyquist_safe;
        f1 = f1.min(f2 - MIN_F1_F2_GAP_HZ);
    }

    (
        f1.max(FORMANT_SEARCH_MIN_HZ),
        f2.max(f1 + MIN_F1_F2_GAP_HZ),
    )
}

fn lpc_order_for_sample_rate(sample_rate: u32) -> usize {
    match sample_rate {
        0..=16_000 => 12,
        16_001..=24_000 => 14,
        24_001..=48_000 => 16,
        _ => 18,
    }
}

fn modify_lpc_coefficients_with_reason(
    a_orig: &[f32],
    sample_rate: u32,
    target_f1: f32,
    target_f2: f32,
    strength: f32,
) -> Result<Vec<f32>, &'static str> {
    if a_orig.len() < 3 {
        return Err("bad_lpc_order");
    }

    let order = a_orig.len() - 1;
    if order < 4 {
        return Err("bad_lpc_order");
    }

    let strength = strength.clamp(0.0_f32, 1.0_f32);

    let target = target_vowel_lpc_coefficients(order, sample_rate, target_f1, target_f2)?;

    if target.len() != a_orig.len() {
        return Err("target_order_mismatch");
    }

    // 这里故意偏激进：你的需求是“明显地把 a 推成 e/i/u”，不是轻微修饰。
    let coeff_blend = (0.60_f32 + strength * 0.40_f32).clamp(0.60_f32, 1.0_f32);

    let mut out = vec![0.0_f32; a_orig.len()];
    out[0] = 1.0_f32;

    let len_f = a_orig.len() as f32;

    for idx in 1..a_orig.len() {
        let frac = idx as f32 / len_f;
        let tier = if frac <= 0.33_f32 {
            coeff_blend
        } else if frac <= 0.67_f32 {
            coeff_blend * 0.50_f32
        } else {
            coeff_blend * 0.10_f32
        };
        out[idx] = a_orig[idx] + (target[idx] - a_orig[idx]) * tier;
    }

    stabilize_lpc_coefficients(&mut out)?;

    if out
        .iter()
        .any(|value| !value.is_finite() || value.abs() > 80.0_f32)
    {
        return Err("bad_target_coeff");
    }

    Ok(out)
}

fn target_vowel_lpc_coefficients(
    order: usize,
    sample_rate: u32,
    target_f1: f32,
    target_f2: f32,
) -> Result<Vec<f32>, &'static str> {
    let nyquist = sample_rate as f32 * 0.5_f32;
    let pair_count = order / 2;

    if pair_count < 2 {
        return Err("order_too_low");
    }

    let f1 = target_f1.clamp(250.0_f32, nyquist - 500.0_f32);
    let f2 = target_f2.clamp(f1 + 250.0_f32, nyquist - 400.0_f32);

    // F2 低时偏 o/u，F2 高时偏 e/i。
    let roundedness: f32 = if f2 < 1_000.0_f32 {
        1.0_f32
    } else {
        0.0_f32
    };

    let f3_base: f32 = if roundedness > 0.5_f32 {
        2_300.0_f32
    } else if f2 > 2_000.0_f32 {
        3_000.0_f32
    } else {
        2_600.0_f32
    };

    let f4_base: f32 = if roundedness > 0.5_f32 {
        3_300.0_f32
    } else {
        3_600.0_f32
    };

    let mut formants: Vec<(f32, f32)> = Vec::new();

    // F1/F2 是主要元音色彩。
    // 带宽不能太窄，否则容易啸叫；也不能太宽，否则听不出变化。
    formants.push((f1, 100.0_f32));
    formants.push((f2, 120.0_f32));

    if pair_count >= 3 {
        let f3 = f3_base
            .min(nyquist - 350.0_f32)
            .max(f2 + 350.0_f32);
        formants.push((f3, 220.0_f32));
    }

    if pair_count >= 4 {
        let f4 = f4_base
            .min(nyquist - 250.0_f32)
            .max(f3_base + 350.0_f32);
        formants.push((f4, 330.0_f32));
    }

    while formants.len() < pair_count {
        let idx = formants.len();
        let frac = idx as f32 / pair_count.max(1) as f32;

        let freq = lerp_linear(3_800.0_f32, nyquist - 300.0_f32, frac)
            .clamp(500.0_f32, nyquist - 250.0_f32);

        let bandwidth = lerp_linear(450.0_f32, 900.0_f32, frac);

        formants.push((freq, bandwidth));
    }

    let mut roots = Vec::with_capacity(order);

    for (freq, bandwidth) in formants.into_iter().take(pair_count) {
        let freq = freq.clamp(80.0_f32, nyquist - 80.0_f32);
        let bandwidth = bandwidth.clamp(70.0_f32, 1_200.0_f32);

        let radius = (-std::f32::consts::PI * bandwidth / sample_rate as f32)
            .exp()
            .clamp(0.20_f32, MAX_ROOT_RADIUS);

        let phase = 2.0_f32 * std::f32::consts::PI * freq / sample_rate as f32;
        let root = Complex32::from_polar(radius, phase);

        roots.push(root);
        roots.push(root.conj());
    }

    while roots.len() < order {
        roots.push(Complex32::new(0.15_f32, 0.0_f32));
    }

    roots.truncate(order);

    polynomial_from_roots(&roots).ok_or("bad_target_poly")
}

fn stabilize_lpc_coefficients(coeffs: &mut [f32]) -> Result<(), &'static str> {
    if coeffs.len() < 2 {
        return Err("bad_lpc_order");
    }

    let roots = polynomial_roots(coeffs).ok_or("stabilize_root_solver")?;

    let mut stable_roots = Vec::with_capacity(roots.len());

    for root in roots {
        let stable = if root.norm() > MAX_ROOT_RADIUS {
            clamp_root_radius(root, MAX_ROOT_RADIUS)
        } else {
            root
        };

        stable_roots.push(stable);
    }

    let stable_coeffs = polynomial_from_roots(&stable_roots).ok_or("stabilize_bad_poly")?;

    if stable_coeffs.len() != coeffs.len() {
        return Err("stabilize_order_mismatch");
    }

    for (dst, src) in coeffs.iter_mut().zip(stable_coeffs.iter()) {
        *dst = *src;
    }

    coeffs[0] = 1.0_f32;

    Ok(())
}

#[cfg(test)]
fn modify_lpc_coefficients(
    a_orig: &[f32],
    sample_rate: u32,
    target_f1: f32,
    target_f2: f32,
    strength: f32,
) -> Option<Vec<f32>> {
    modify_lpc_coefficients_with_reason(a_orig, sample_rate, target_f1, target_f2, strength).ok()
}

#[cfg(test)]
fn lpc_coefficients(frame: &[f32], order: usize) -> Option<Vec<f32>> {
    lpc_coefficients_with_reason(frame, order).ok()
}

fn lpc_coefficients_with_reason(frame: &[f32], order: usize) -> Result<Vec<f32>, &'static str> {
    if frame.len() <= order + 2 {
        return Err("too_short");
    }

    let mut a = vec![0.0_f32; order + 1];
    a[0] = 1.0_f32;

    let mut ef = frame[1..].to_vec();
    let mut eb = frame[..frame.len() - 1].to_vec();

    let mut error = frame.iter().map(|sample| sample * sample).sum::<f32>() / frame.len() as f32;

    if !error.is_finite() || error <= EPSILON {
        return Err("bad_initial_error");
    }

    for m in 0..order {
        if ef.len() < 2 || eb.len() < 2 {
            break;
        }

        let numerator = -2.0_f32
            * eb.iter()
                .zip(ef.iter())
                .map(|(backward, forward)| backward * forward)
                .sum::<f32>();

        let denominator = eb.iter().map(|sample| sample * sample).sum::<f32>()
            + ef.iter().map(|sample| sample * sample).sum::<f32>();

        if !denominator.is_finite() || denominator <= EPSILON {
            return Err("bad_denominator");
        }

        let reflection = (numerator / denominator).clamp(-0.985_f32, 0.985_f32);
        if !reflection.is_finite() {
            return Err("bad_reflection");
        }

        let prev = a.clone();

        for i in 1..=m {
            a[i] = prev[i] + reflection * prev[m + 1 - i];
        }

        a[m + 1] = reflection;

        let mut ef_next = Vec::with_capacity(ef.len().saturating_sub(1));
        let mut eb_next = Vec::with_capacity(eb.len().saturating_sub(1));

        for i in 1..ef.len() {
            ef_next.push(ef[i] + reflection * eb[i]);
        }

        for i in 0..eb.len().saturating_sub(1) {
            eb_next.push(eb[i] + reflection * ef[i]);
        }

        ef = ef_next;
        eb = eb_next;

        error *= 1.0_f32 - reflection * reflection;

        if !error.is_finite() || error <= EPSILON {
            break;
        }
    }

    if a.iter().any(|value| !value.is_finite()) {
        return Err("bad_lpc_coeff");
    }

    Ok(a)
}

fn polynomial_roots(coeffs: &[f32]) -> Option<Vec<Complex32>> {
    if coeffs.len() < 2 || coeffs[0].abs() < EPSILON {
        return None;
    }

    let degree = coeffs.len() - 1;

    if degree == 1 {
        return Some(vec![Complex32::new(
            -coeffs[1] / coeffs[0],
            0.0_f32,
        )]);
    }

    let monic: Vec<Complex32> = coeffs
        .iter()
        .map(|value| Complex32::new(*value / coeffs[0], 0.0_f32))
        .collect();

    let mut solver = AberthSolver::<f32>::new();
    solver.max_iterations = 256;
    solver.epsilon = 1.0e-4_f32;

    let reversed: Vec<f32> = coeffs.iter().rev().map(|value| *value / coeffs[0]).collect();

    let aberth_roots = solver.find_roots(&reversed);

    let roots = match aberth_roots.stop_reason {
        StopReason::Converged(_) | StopReason::MaxIteration(_) => Some(
            aberth_roots
                .iter()
                .map(|root| Complex32::new(root.re, root.im))
                .collect(),
        ),
        StopReason::Failed(_) => None,
    }
    .or_else(|| polynomial_roots_via_qr(&monic))
    .or_else(|| polynomial_roots_durand_kerner(&monic))?;

    if roots.iter().any(|root| {
        !root.re.is_finite()
            || !root.im.is_finite()
            || evaluate_polynomial(&monic, *root).norm() > 3.5e-1_f32
    }) {
        return None;
    }

    Some(roots)
}

fn polynomial_roots_via_qr(monic: &[Complex32]) -> Option<Vec<Complex32>> {
    let degree = monic.len().checked_sub(1)?;
    if degree == 0 {
        return Some(Vec::new());
    }

    let mut matrix = vec![Complex32::new(0.0_f32, 0.0_f32); degree * degree];

    for col in 0..degree {
        matrix[col] = -monic[col + 1];
    }

    for row in 1..degree {
        matrix[row * degree + (row - 1)] = Complex32::new(1.0_f32, 0.0_f32);
    }

    for _ in 0..384 {
        let subdiag_norm = (1..degree)
            .map(|row| matrix[row * degree + (row - 1)].norm_sqr())
            .sum::<f32>()
            .sqrt();

        if subdiag_norm < 1.0e-5_f32 {
            break;
        }

        let shift = matrix[(degree - 1) * degree + (degree - 1)];

        for idx in 0..degree {
            matrix[idx * degree + idx] -= shift;
        }

        let (q, r) = qr_decompose_complex(&matrix, degree)?;
        matrix = mat_mul_complex(&r, &q, degree);

        for idx in 0..degree {
            matrix[idx * degree + idx] += shift;
        }
    }

    Some(
        (0..degree)
            .map(|idx| matrix[idx * degree + idx])
            .collect(),
    )
}

fn polynomial_roots_durand_kerner(monic: &[Complex32]) -> Option<Vec<Complex32>> {
    let degree = monic.len().checked_sub(1)?;

    if degree == 0 {
        return Some(Vec::new());
    }

    let radius = 0.92_f32;

    let mut roots: Vec<Complex32> = (0..degree)
        .map(|idx| {
            let phase = 2.0_f32 * std::f32::consts::PI * idx as f32 / degree as f32;
            Complex32::from_polar(radius, phase) * Complex32::new(0.997_f32, 0.071_f32)
        })
        .collect();

    for _ in 0..512 {
        let mut max_delta = 0.0_f32;

        for idx in 0..degree {
            let root = roots[idx];
            let numerator = evaluate_polynomial(monic, root);

            let mut denominator = Complex32::new(1.0_f32, 0.0_f32);

            for (other_idx, other_root) in roots.iter().enumerate() {
                if other_idx != idx {
                    denominator *= root - *other_root;
                }
            }

            if denominator.norm() <= EPSILON {
                roots[idx] += Complex32::new(1.0e-3_f32 * (idx as f32 + 1.0_f32), 1.0e-3_f32);
                continue;
            }

            let next = root - numerator / denominator;
            let delta = (next - root).norm();

            max_delta = max_delta.max(delta);
            roots[idx] = next;
        }

        if max_delta < 1.0e-6_f32 {
            break;
        }
    }

    for root in &mut roots {
        for _ in 0..8 {
            let numerator = evaluate_polynomial(monic, *root);
            let denominator = evaluate_polynomial_derivative(monic, *root);

            if denominator.norm() <= EPSILON {
                break;
            }

            let next = *root - numerator / denominator;

            if (next - *root).norm() < 1.0e-7_f32 {
                *root = next;
                break;
            }

            *root = next;
        }
    }

    Some(roots)
}

fn evaluate_polynomial(coeffs: &[Complex32], x: Complex32) -> Complex32 {
    coeffs.iter().fold(
        Complex32::new(0.0_f32, 0.0_f32),
        |acc, coeff| acc * x + *coeff,
    )
}

fn evaluate_polynomial_derivative(coeffs: &[Complex32], x: Complex32) -> Complex32 {
    if coeffs.len() < 2 {
        return Complex32::new(0.0_f32, 0.0_f32);
    }

    let mut poly = coeffs[0];
    let mut deriv = Complex32::new(0.0_f32, 0.0_f32);

    for coeff in coeffs.iter().skip(1) {
        deriv = deriv * x + poly;
        poly = poly * x + *coeff;
    }

    deriv
}

fn qr_decompose_complex(
    matrix: &[Complex32],
    size: usize,
) -> Option<(Vec<Complex32>, Vec<Complex32>)> {
    let mut q = vec![Complex32::new(0.0_f32, 0.0_f32); size * size];
    let mut r = vec![Complex32::new(0.0_f32, 0.0_f32); size * size];
    let mut v = vec![Complex32::new(0.0_f32, 0.0_f32); size];

    for col in 0..size {
        for row in 0..size {
            v[row] = matrix[row * size + col];
        }

        for prev_col in 0..col {
            let mut dot = Complex32::new(0.0_f32, 0.0_f32);

            for row in 0..size {
                dot += q[row * size + prev_col].conj() * v[row];
            }

            r[prev_col * size + col] = dot;

            for row in 0..size {
                v[row] -= q[row * size + prev_col] * dot;
            }
        }

        let norm = v.iter().map(|value| value.norm_sqr()).sum::<f32>().sqrt();

        if norm <= 1.0e-8_f32 {
            return None;
        }

        r[col * size + col] = Complex32::new(norm, 0.0_f32);

        let inv_norm = 1.0_f32 / norm;
        for row in 0..size {
            q[row * size + col] = v[row] * inv_norm;
        }
    }

    Some((q, r))
}

fn mat_mul_complex(lhs: &[Complex32], rhs: &[Complex32], size: usize) -> Vec<Complex32> {
    let mut out = vec![Complex32::new(0.0_f32, 0.0_f32); size * size];

    for row in 0..size {
        for col in 0..size {
            let mut acc = Complex32::new(0.0_f32, 0.0_f32);

            for mid in 0..size {
                acc += lhs[row * size + mid] * rhs[mid * size + col];
            }

            out[row * size + col] = acc;
        }
    }

    out
}

fn polynomial_from_roots(roots: &[Complex32]) -> Option<Vec<f32>> {
    let mut coeffs = vec![Complex32::new(1.0_f32, 0.0_f32)];

    for root in roots {
        let mut next = vec![Complex32::new(0.0_f32, 0.0_f32); coeffs.len() + 1];

        for (idx, coeff) in coeffs.iter().enumerate() {
            next[idx] += *coeff;
            next[idx + 1] -= *coeff * *root;
        }

        coeffs = next;
    }

    if coeffs
        .iter()
        .any(|coeff| !coeff.re.is_finite() || !coeff.im.is_finite())
    {
        return None;
    }

    Some(coeffs.into_iter().map(|coeff| coeff.re).collect())
}

fn clamp_root_radius(root: Complex32, max_radius: f32) -> Complex32 {
    let radius = root.norm();

    if !radius.is_finite() || radius <= EPSILON {
        return Complex32::new(0.0_f32, 0.0_f32);
    }

    if radius > max_radius {
        root * (max_radius / radius)
    } else {
        root
    }
}

fn fir_filter(input: &[f32], taps: &[f32]) -> Vec<f32> {
    let mut out = vec![0.0_f32; input.len()];

    for n in 0..input.len() {
        let mut acc = 0.0_f32;

        for k in 0..taps.len() {
            if n >= k {
                acc += taps[k] * input[n - k];
            }
        }

        out[n] = acc;
    }

    out
}

fn all_pole_filter_checked(input: &[f32], denominator: &[f32], max_abs: f32) -> Option<Vec<f32>> {
    if denominator.is_empty() || denominator[0].abs() < EPSILON {
        return Some(input.to_vec());
    }

    let mut out = vec![0.0_f32; input.len()];
    let a0 = denominator[0];

    for n in 0..input.len() {
        let mut acc = input[n];

        for k in 1..denominator.len() {
            if n >= k {
                acc -= denominator[k] * out[n - k];
            }
        }

        let sample = acc / a0;

        if !sample.is_finite() {
            return None;
        }

        out[n] = sample.clamp(-max_abs, max_abs);
    }

    Some(out)
}

fn match_energy_limited(reference: &[f32], candidate: &mut [f32], min_gain: f32, max_gain: f32) {
    let ref_energy = frame_energy(reference);
    let cand_energy = frame_energy(candidate);

    if ref_energy <= EPSILON || cand_energy <= EPSILON {
        return;
    }

    let gain = (ref_energy / cand_energy).sqrt().clamp(min_gain, max_gain);

    for sample in candidate {
        *sample *= gain;
    }
}

fn voiced_confidence(frame: &[f32], mean_energy: f32) -> f32 {
    if mean_energy < 1.0e-8_f32 {
        return 0.0_f32;
    }

    let peak = peak_abs(frame);
    if peak < 0.001_f32 {
        return 0.0_f32;
    }

    let zcr = zero_crossing_rate(frame);

    if zcr > 0.35_f32 {
        0.35_f32
    } else if zcr > 0.28_f32 {
        0.65_f32
    } else {
        1.0_f32
    }
}

fn zero_crossing_rate(frame: &[f32]) -> f32 {
    if frame.len() < 2 {
        return 0.0_f32;
    }

    let mut crossings = 0usize;

    for idx in 1..frame.len() {
        let prev_positive = frame[idx - 1] >= 0.0_f32;
        let curr_positive = frame[idx] >= 0.0_f32;

        if prev_positive != curr_positive {
            crossings += 1;
        }
    }

    crossings as f32 / frame.len() as f32
}

fn peak_abs(input: &[f32]) -> f32 {
    input
        .iter()
        .fold(0.0_f32, |peak, sample| peak.max(sample.abs()))
}

fn limit_frame(frame: &mut [f32], max_abs: f32) {
    if max_abs <= EPSILON {
        return;
    }

    let peak = peak_abs(frame);

    if peak > max_abs {
        let gain = max_abs / peak;

        for sample in frame {
            *sample *= gain;
        }
    }
}

fn remove_bad_samples(buffer: &mut [f32]) {
    for sample in buffer {
        if !sample.is_finite() {
            *sample = 0.0_f32;
        }
    }
}

fn normalize_overlap_add(overlap: &mut [f32], window_sum: &[f32]) {
    for (sample, weight) in overlap.iter_mut().zip(window_sum.iter()) {
        if *weight > EPSILON {
            *sample /= *weight;
        }
    }
}

fn final_output_protect(output: &mut [f32], input: &[f32]) {
    remove_bad_samples(output);

    for sample in output.iter_mut() {
        *sample = soft_limiter(*sample, 1.25_f32);
    }

    let input_peak = peak_abs(input).max(0.001_f32);
    let output_peak = peak_abs(output).max(0.001_f32);

    let max_allowed_peak = (input_peak * 2.2_f32).clamp(0.05_f32, 0.98_f32);

    if output_peak > max_allowed_peak {
        let gain = max_allowed_peak / output_peak;

        for sample in output.iter_mut() {
            *sample *= gain;
        }
    }

    for sample in output {
        *sample = sample.clamp(-0.98_f32, 0.98_f32);
    }
}

fn soft_limiter(x: f32, drive: f32) -> f32 {
    if !x.is_finite() {
        return 0.0_f32;
    }

    let drive = drive.max(1.0_f32);
    (x * drive).tanh() / drive.tanh()
}

fn remove_dc(buffer: &mut [f32]) {
    if buffer.is_empty() {
        return;
    }

    let mean = buffer.iter().sum::<f32>() / buffer.len() as f32;

    if mean.is_finite() {
        for sample in buffer {
            *sample -= mean;
        }
    }
}

fn frame_energy(input: &[f32]) -> f32 {
    input.iter().map(|sample| sample * sample).sum::<f32>()
}

fn pad_for_overlap_add(input: &[f32], frame_len: usize, hop_len: usize) -> Vec<f32> {
    let mut out = input.to_vec();

    let mut pad = frame_len.saturating_sub(out.len() % hop_len);

    if pad == 0 {
        pad = frame_len;
    }

    out.resize(out.len() + pad, 0.0_f32);

    if out.len() < frame_len {
        out.resize(frame_len, 0.0_f32);
    }

    out
}

fn average_channels_to_mono(input: &[f32], channels: usize, frames: usize) -> Vec<f32> {
    let mut mono = vec![0.0_f32; frames];

    for frame_idx in 0..frames {
        let mut sum = 0.0_f32;

        for ch in 0..channels {
            sum += input[frame_idx * channels + ch];
        }

        mono[frame_idx] = sum / channels as f32;
    }

    mono
}

fn apply_mono_delta_to_interleaved(
    input: &[f32],
    channels: usize,
    dry_mono: &[f32],
    wet_mono: &[f32],
) -> Vec<f32> {
    let frames = dry_mono.len().min(wet_mono.len());
    let mut out = input.to_vec();

    for frame_idx in 0..frames {
        let delta = wet_mono[frame_idx] - dry_mono[frame_idx];

        for ch in 0..channels {
            let idx = frame_idx * channels + ch;
            out[idx] = soft_limiter(input[idx] + delta, 1.20_f32).clamp(-0.98_f32, 0.98_f32);
        }
    }

    out
}

fn hann_window(len: usize) -> Vec<f32> {
    if len <= 1 {
        return vec![1.0_f32; len];
    }

    (0..len)
        .map(|idx| {
            0.5_f32
                - 0.5_f32
                    * ((2.0_f32 * std::f32::consts::PI * idx as f32)
                        / (len - 1) as f32)
                        .cos()
        })
        .collect()
}

fn pre_emphasis(input: &[f32], coef: f32) -> Vec<f32> {
    let mut out = Vec::with_capacity(input.len());
    let mut prev = 0.0_f32;

    for &sample in input {
        out.push(sample - coef * prev);
        prev = sample;
    }

    out
}

fn de_emphasis(input: &[f32], coef: f32) -> Vec<f32> {
    let mut out = Vec::with_capacity(input.len());
    let mut prev = 0.0_f32;

    for &sample in input {
        let next = sample + coef * prev;
        out.push(next);
        prev = next;
    }

    out
}

fn lerp_linear(from: f32, to: f32, amount: f32) -> f32 {
    from + (to - from) * amount.clamp(0.0_f32, 1.0_f32)
}