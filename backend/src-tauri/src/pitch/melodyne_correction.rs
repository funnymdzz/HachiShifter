//! Note-aware pitch correction reconstructed from the Melodyne 5 analysis in
//! `/home/ubuntu/mdd`.
//!
//! HachiShifter already owns high-quality WORLD/NSF-HiFiGAN resynthesis, so
//! this module implements the editable control model rather than a second
//! spectral renderer: note center, slow drift and fast modulation are adjusted
//! independently and written into the existing absolute-MIDI pitch curve.

use crate::sample_annotations::{SampleAnalysis, SamplePitchNote, SampleRegionAnnotation};
use crate::state::{Clip, TimelineState};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MelodyneCorrectionSettings {
    pub center_strength: f32,
    pub drift_strength: f32,
    pub modulation_strength: f32,
    pub transition_ms: f32,
    #[serde(default)]
    pub performance_mode: bool,
    #[serde(default)]
    pub reset: bool,
    pub selection_start_sec: Option<f64>,
    pub selection_end_sec: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MelodyneCorrectionSummary {
    pub root_track_id: String,
    pub affected_notes: usize,
    pub affected_frames: usize,
    pub frame_period_ms: f64,
    pub detector: crate::sample_annotations::NoteDetectorKind,
}

fn active_annotation<'a>(clip: &Clip, analysis: &'a SampleAnalysis) -> Option<&'a SampleRegionAnnotation> {
    if analysis.annotations.is_empty() {
        return None;
    }
    let index = clip
        .extra_params
        .as_ref()
        .and_then(|params| params.get("hachi_active_annotation"))
        .copied()
        .unwrap_or(0.0)
        .round()
        .max(0.0) as usize;
    analysis
        .annotations
        .get(index.min(analysis.annotations.len() - 1))
}

fn source_to_timeline(
    clip: &Clip,
    annotation: Option<&SampleRegionAnnotation>,
    source_sec: f64,
) -> f64 {
    if let Some(segment) = clip.melodyne_warp_segments.iter().find(|segment| {
        source_sec >= segment.source_start_sec && source_sec <= segment.source_end_sec
    }) {
        let source_span = (segment.source_end_sec - segment.source_start_sec).max(1e-9);
        let phase = ((source_sec - segment.source_start_sec) / source_span).clamp(0.0, 1.0);
        return segment.timeline_start_sec
            + phase * (segment.timeline_end_sec - segment.timeline_start_sec);
    }
    let source_start = clip.source_start_sec.max(0.0);
    let source_end = clip.source_end_sec.max(source_start + 1e-6);
    let source_span = (source_end - source_start).max(1e-6);
    let target_span = clip.length_sec.max(1e-6);
    let local = (source_sec - source_start).clamp(0.0, source_span);

    let requested_fixed = annotation
        .map(|row| {
            (row.region_start_sec + row.fixed_duration_sec - source_start)
                .clamp(0.0, source_span)
        })
        .unwrap_or(0.0);
    let fixed = requested_fixed
        .min((source_span - 1e-6).max(0.0))
        .min((target_span - 1e-6).max(0.0));
    let timeline_local = if fixed > 1e-6 && fixed < source_span - 1e-6 {
        if local <= fixed {
            local
        } else {
            fixed + (local - fixed) * (target_span - fixed) / (source_span - fixed)
        }
    } else {
        local * target_span / source_span
    };
    clip.start_sec + timeline_local
}

fn nearest_scale_midi(midi: f32, pitch_classes: &[u8]) -> f32 {
    if pitch_classes.is_empty() {
        return midi.round();
    }
    let center = midi.round() as i32;
    let mut best = midi.round();
    let mut best_distance = f32::INFINITY;
    for note in (center - 12)..=(center + 12) {
        let pitch_class = note.rem_euclid(12) as u8;
        if !pitch_classes.contains(&pitch_class) {
            continue;
        }
        let distance = (note as f32 - midi).abs();
        if distance < best_distance {
            best_distance = distance;
            best = note as f32;
        }
    }
    best
}

fn median(values: &mut [f32]) -> f32 {
    values.sort_by(f32::total_cmp);
    let middle = values.len() / 2;
    if values.len() % 2 == 0 {
        (values[middle - 1] + values[middle]) * 0.5
    } else {
        values[middle]
    }
}

fn moving_average(values: &[f32], index: usize, radius: usize, fallback: f32) -> f32 {
    let start = index.saturating_sub(radius);
    let end = (index + radius + 1).min(values.len());
    let mut sum = 0.0f32;
    let mut count = 0usize;
    for value in &values[start..end] {
        if value.is_finite() && *value > 0.0 {
            sum += *value;
            count += 1;
        }
    }
    if count == 0 {
        fallback
    } else {
        sum / count as f32
    }
}

fn selection_range(settings: MelodyneCorrectionSettings) -> Option<(f64, f64)> {
    let start = settings.selection_start_sec?;
    let end = settings.selection_end_sec?;
    if !(start.is_finite() && end.is_finite()) || (end - start).abs() <= 1e-6 {
        return None;
    }
    Some((start.min(end), start.max(end)))
}

fn note_overlaps_selection(note_start: f64, note_end: f64, selection: Option<(f64, f64)>) -> bool {
    selection
        .map(|(start, end)| note_end > start && note_start < end)
        .unwrap_or(true)
}

pub fn apply(
    timeline: &mut TimelineState,
    clip: &Clip,
    analysis: &SampleAnalysis,
    settings: MelodyneCorrectionSettings,
) -> Result<MelodyneCorrectionSummary, String> {
    if clip.reversed {
        return Err("note-aware correction expects a forward audio clip".to_string());
    }
    let root = timeline
        .resolve_root_track_id(&clip.track_id)
        .ok_or_else(|| "selected clip track has no root track".to_string())?;
    let pitch_classes = timeline.project_scale_notes.clone();
    timeline.ensure_params_for_root(&root);
    let entry = timeline
        .params_by_root_track
        .get_mut(&root)
        .ok_or_else(|| "root track pitch parameters are missing".to_string())?;
    if entry.pitch_orig.is_empty() {
        return Err("pitch analysis is still empty for the selected track".to_string());
    }
    if entry.pitch_edit.len() < entry.pitch_orig.len() {
        entry.pitch_edit.resize(entry.pitch_orig.len(), 0.0);
    }

    let frame_period_ms = entry.frame_period_ms.max(0.1);
    let selection = selection_range(settings);
    let annotation = active_annotation(clip, analysis);
    let source_start = clip.source_start_sec.max(0.0);
    let source_end = clip.source_end_sec.max(source_start + 1e-6);
    let notes: Vec<&SamplePitchNote> = analysis
        .pitch_notes
        .iter()
        .filter(|note| note.end_sec > source_start && note.start_sec < source_end)
        .collect();
    if notes.is_empty() {
        return Err("the selected clip has no detected voiced notes".to_string());
    }

    let center_strength = settings.center_strength.clamp(0.0, 1.0);
    let drift_strength = settings.drift_strength.clamp(0.0, 1.0);
    let modulation_strength = settings.modulation_strength.clamp(0.0, 1.0);
    let transition_frames = ((settings.transition_ms.clamp(0.0, 500.0) as f64
        / frame_period_ms)
        .round() as usize)
        .max(1);
    let slow_radius = ((150.0 / frame_period_ms).round() as usize).max(1);

    let mut affected_notes = 0usize;
    let mut affected_frames = 0usize;
    for note in notes {
        let timeline_start = source_to_timeline(clip, annotation, note.start_sec.max(source_start));
        let timeline_end = source_to_timeline(clip, annotation, note.end_sec.min(source_end));
        if timeline_end <= timeline_start
            || !note_overlaps_selection(timeline_start, timeline_end, selection)
        {
            continue;
        }
        let start_frame = ((timeline_start * 1000.0) / frame_period_ms)
            .floor()
            .max(0.0) as usize;
        let end_frame = ((timeline_end * 1000.0) / frame_period_ms)
            .ceil()
            .max(0.0) as usize;
        let start_frame = start_frame.min(entry.pitch_orig.len());
        let end_frame = end_frame.min(entry.pitch_orig.len());
        if end_frame <= start_frame {
            continue;
        }

        if settings.reset {
            for index in start_frame..end_frame {
                entry.pitch_edit[index] = entry.pitch_orig[index];
                affected_frames += 1;
            }
            affected_notes += 1;
            continue;
        }

        let original = &entry.pitch_orig[start_frame..end_frame];
        let mut voiced: Vec<f32> = original
            .iter()
            .copied()
            .filter(|value| value.is_finite() && *value > 0.0)
            .collect();
        if voiced.is_empty() {
            continue;
        }
        let center = median(&mut voiced);
        let target = nearest_scale_midi(center, &pitch_classes);
        let note_frames = end_frame - start_frame;
        for local_index in 0..note_frames {
            let original_pitch = original[local_index];
            if !(original_pitch.is_finite() && original_pitch > 0.0) {
                continue;
            }
            let slow = moving_average(original, local_index, slow_radius, center);
            let slow_deviation = slow - center;
            let fast_deviation = original_pitch - slow;
            let correction = center_strength * (target - center)
                - drift_strength * slow_deviation
                - modulation_strength * fast_deviation;
            let edge_distance = local_index.min(note_frames.saturating_sub(local_index + 1));
            let linear_blend = (edge_distance as f32 / transition_frames as f32).clamp(0.0, 1.0);
            let smooth_blend = linear_blend * linear_blend * (3.0 - 2.0 * linear_blend);
            entry.pitch_edit[start_frame + local_index] =
                (original_pitch + correction * smooth_blend).clamp(1.0, 127.0);
            affected_frames += 1;
        }
        affected_notes += 1;
    }

    if affected_notes == 0 {
        return Err("no detected note overlaps the requested correction range".to_string());
    }
    entry.pitch_edit_user_modified = entry
        .pitch_orig
        .iter()
        .zip(entry.pitch_edit.iter())
        .any(|(original, edited)| {
            edited.is_finite()
                && *edited > 0.0
                && (!(original.is_finite() && *original > 0.0)
                    || (*edited - *original).abs() > 1e-3)
        });

    Ok(MelodyneCorrectionSummary {
        root_track_id: root,
        affected_notes,
        affected_frames,
        frame_period_ms,
        detector: analysis.note_detector,
    })
}

#[cfg(test)]
mod tests {
    use super::nearest_scale_midi;

    #[test]
    fn nearest_scale_respects_pitch_classes() {
        assert_eq!(nearest_scale_midi(61.2, &[0, 2, 4, 5, 7, 9, 11]), 60.0);
        assert_eq!(nearest_scale_midi(61.8, &[0, 2, 4, 5, 7, 9, 11]), 62.0);
    }
}
