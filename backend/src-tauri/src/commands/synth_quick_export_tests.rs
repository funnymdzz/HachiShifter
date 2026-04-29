use crate::commands::synth::build_quick_export_timeline_and_range;
use crate::state::TimelineState;

#[test]
fn quick_export_filters_to_selected_clips_and_preserves_range() {
    let mut timeline = TimelineState::default();
    let track_a = timeline.add_track(Some("Lead".to_string()), None, None);
    let track_b = timeline.add_track(Some("Harmony".to_string()), None, None);

    let clip_a = timeline.add_clip(
        Some(track_a.clone()),
        Some("A".into()),
        Some(1.0),
        Some(2.0),
        None,
    );
    let _clip_b = timeline.add_clip(Some(track_a), Some("B".into()), Some(5.0), Some(1.0), None);
    let clip_c = timeline.add_clip(Some(track_b), Some("C".into()), Some(9.0), Some(0.5), None);

    let (export_timeline, start_sec, end_sec) =
        build_quick_export_timeline_and_range(&timeline, &[clip_a.clone(), clip_c.clone()])
            .expect("quick export timeline");

    let kept_ids: Vec<String> = export_timeline
        .clips
        .iter()
        .map(|clip| clip.id.clone())
        .collect();
    assert_eq!(kept_ids, vec![clip_a, clip_c]);
    assert!((start_sec - 1.0).abs() < 1e-6);
    assert!((end_sec - 9.5).abs() < 1e-6);
}

#[test]
fn quick_export_rejects_empty_selection() {
    let timeline = TimelineState::default();
    let error = build_quick_export_timeline_and_range(&timeline, &[])
        .expect_err("empty selection should fail");
    assert!(error.contains("clip"));
}
