import type { TimelineResult, TrackSummaryResult } from "../../types/api";
import type { LinkedParamCurves } from "../../features/session/sessionTypes";

import { invoke } from "../invoke";
import type { ClipTemplate } from "../../features/session/sessionTypes";

export interface SampleRegionAnnotation {
    name: string;
    region_start_sec: number;
    region_end_sec: number;
    note_alignment_sec: number;
    fixed_duration_sec: number;
}

export interface SamplePitchNote {
    start_sec: number;
    end_sec: number;
    midi_note: number;
    confidence: number;
}

export type ClipSampleAnnotationsResult =
    | {
          ok: true;
          clip_id: string;
          audio_path: string;
          sidecar_path: string;
          annotations: SampleRegionAnnotation[];
          pitch_notes: SamplePitchNote[];
          active_annotation_index: number;
      }
    | { ok: false; error: string };

export type SaveClipSampleAnnotationsResult =
    | {
          ok: true;
          clip_id: string;
          sidecar_path: string;
          annotations: SampleRegionAnnotation[];
          active_annotation_index: number;
      }
    | { ok: false; error: string };

export interface OtoConversionResult {
    oto_files: number;
    converted_samples: Array<{
        audio_path: string;
        sidecar_path: string;
        annotation_count: number;
    }>;
    warnings: string[];
}

export const timelineApi = {
    // Undo/Redo (backend-authoritative)
    undoTimeline: () => invoke<TimelineResult>("undo_timeline"),
    redoTimeline: () => invoke<TimelineResult>("redo_timeline"),

    // Undo grouping: all commands between begin/end share a single undo entry
    beginUndoGroup: () => invoke<TimelineResult>("begin_undo_group"),
    endUndoGroup: () => invoke<{ ok: boolean }>("end_undo_group"),

    getTimelineState: () => invoke<TimelineResult>("get_timeline_state"),

    // Transport
    setTransport: (payload: { playheadSec?: number; bpm?: number }) =>
        invoke<{ ok: boolean; playhead_sec?: number; bpm?: number }>(
            "set_transport",
            payload.playheadSec,
            payload.bpm,
        ),

    setProjectLength: (projectSec: number) =>
        invoke<TimelineResult>("set_project_length", projectSec),

    // Import
    importAudioItem: (audioPath: string, trackId?: string | null, startSec?: number) =>
        invoke<TimelineResult>("import_audio_item", audioPath, trackId, startSec),

    importAudioBytes: (
        fileName: string,
        base64Data: string,
        trackId?: string | null,
        startSec?: number,
    ) => invoke<TimelineResult>("import_audio_bytes", fileName, base64Data, trackId, startSec),

    getClipSampleAnnotations: (clipId: string) =>
        invoke<ClipSampleAnnotationsResult>("get_clip_sample_annotations", clipId),

    saveClipSampleAnnotations: (
        clipId: string,
        annotations: SampleRegionAnnotation[],
        activeAnnotationIndex: number,
    ) =>
        invoke<SaveClipSampleAnnotationsResult>(
            "save_clip_sample_annotations",
            clipId,
            annotations,
            activeAnnotationIndex,
        ),

    redetectClipSampleAnnotations: (clipId: string) =>
        invoke<ClipSampleAnnotationsResult>("redetect_clip_sample_annotations", clipId),

    openOtoDialog: () =>
        invoke<{ ok: boolean; canceled?: boolean; path?: string; error?: string }>(
            "open_oto_dialog",
        ),

    convertOtoToAnnotations: (otoPath: string) =>
        invoke<{ ok: boolean; result?: OtoConversionResult; error?: string }>(
            "convert_oto_to_annotations",
            otoPath,
        ),

    convertOtoAndRefreshClip: (clipId: string, otoPath: string) =>
        invoke<{
            ok: boolean;
            conversion?: OtoConversionResult;
            clip?: ClipSampleAnnotationsResult;
            error?: string;
        }>("convert_oto_and_refresh_clip", clipId, otoPath),

    // Tracks
    addTrack: (name?: string) => invoke<TimelineResult>("add_track", name),

    addTrackNested: (payload: { name?: string; parentTrackId?: string | null; index?: number }) =>
        invoke<TimelineResult>(
            "add_track",
            payload.name,
            payload.parentTrackId ?? null,
            payload.index,
        ),

    removeTrack: (trackId: string) => invoke<TimelineResult>("remove_track", trackId),

    duplicateTrack: (trackId: string) => invoke<TimelineResult>("duplicate_track", trackId),

    moveTrack: (payload: { trackId: string; targetIndex: number; parentTrackId?: string | null }) =>
        invoke<TimelineResult>(
            "move_track",
            payload.trackId,
            payload.targetIndex,
            payload.parentTrackId ?? null,
        ),

    setTrackState: (payload: {
        trackId: string;
        muted?: boolean;
        solo?: boolean;
        volume?: number;
        composeEnabled?: boolean;
        pitchAnalysisAlgo?: string;
        color?: string;
        name?: string;
    }) =>
        invoke<TimelineResult>(
            "set_track_state",
            payload.trackId,
            payload.muted,
            payload.solo,
            payload.volume,
            payload.composeEnabled,
            payload.pitchAnalysisAlgo,
            payload.color,
            payload.name,
        ),

    selectTrack: (trackId: string) => invoke<TimelineResult>("select_track", trackId),

    getTrackSummary: (trackId?: string) => invoke<TrackSummaryResult>("get_track_summary", trackId),

    // Clips
    addClip: (payload: {
        trackId?: string;
        name?: string;
        startSec?: number;
        lengthSec?: number;
        sourcePath?: string;
    }) =>
        invoke<TimelineResult>(
            "add_clip",
            payload.trackId,
            payload.name,
            payload.startSec,
            payload.lengthSec,
            payload.sourcePath,
        ),

    createClipsBulk: (payload: { templates: ClipTemplate[]; selectCreatedClips?: boolean }) =>
        invoke<TimelineResult>("create_clips_bulk", payload),

    removeClip: (clipId: string) => invoke<TimelineResult>("remove_clip", clipId),

    removeClips: (clipIds: string[]) => invoke<TimelineResult>("remove_clips", clipIds),

    moveClip: (payload: {
        clipId: string;
        startSec: number;
        trackId?: string;
        moveLinkedParams?: boolean;
    }) =>
        invoke<TimelineResult>(
            "move_clip",
            payload.clipId,
            payload.startSec,
            payload.trackId,
            payload.moveLinkedParams,
        ),

    moveClips: (payload: {
        moves: Array<{
            clipId: string;
            startSec: number;
            trackId?: string;
        }>;
        moveLinkedParams?: boolean;
    }) => invoke<TimelineResult>("move_clips", payload.moves, payload.moveLinkedParams),

    getClipLinkedParams: (clipId: string) =>
        invoke<{ ok: boolean; linkedParams?: LinkedParamCurves }>("get_clip_linked_params", clipId),

    applyClipLinkedParams: (payload: { clipId: string; linkedParams: LinkedParamCurves }) =>
        invoke<TimelineResult>("apply_clip_linked_params", payload.clipId, payload.linkedParams),

    setClipState: (payload: {
        clipId: string;
        name?: string;
        startSec?: number;
        lengthSec?: number;
        gain?: number;
        muted?: boolean;
        sourceStartSec?: number;
        sourceEndSec?: number;
        playbackRate?: number;
        reversed?: boolean;
        fadeInSec?: number;
        fadeOutSec?: number;
        fadeInCurve?: string;
        fadeOutCurve?: string;
        color?: string;
        formantMorph?: {
            enabled: boolean;
            targetF1Hz: number;
            targetF2Hz: number;
            strength: number;
        };
        /** 是否创建 undo checkpoint，默认为 true */
        checkpoint?: boolean;
    }) =>
        invoke<TimelineResult>(
            "set_clip_state",
            payload.clipId,
            payload.name,
            payload.startSec,
            payload.lengthSec,
            payload.gain,
            payload.muted,
            payload.sourceStartSec,
            payload.sourceEndSec,
            payload.playbackRate,
            payload.reversed,
            payload.fadeInSec,
            payload.fadeOutSec,
            payload.fadeInCurve,
            payload.fadeOutCurve,
            payload.color,
            payload.formantMorph,
            payload.checkpoint,
        ),

    setClipsStateBulk: (payload: {
        updates: Array<{
            clipId: string;
            gain?: number;
            muted?: boolean;
            fadeInSec?: number;
            fadeOutSec?: number;
        }>;
        checkpoint?: boolean;
    }) => invoke<TimelineResult>("set_clips_state_bulk", payload.updates, payload.checkpoint),

    duplicateClipsBulk: (payload: {
        sourceClipIds: string[];
        deltaSec: number;
        trackMode: Record<string, unknown>;
        copyLinkedParams?: boolean;
        selectCreatedClips?: boolean;
        applyAutoCrossfade?: boolean;
        placeOnSelectedTrack?: boolean;
        renameCopies?: boolean;
    }) => invoke<TimelineResult>("duplicate_clips_bulk", payload),

    replaceClipSource: (payload: {
        clipIds: string[];
        newSourcePath: string;
        replaceSameSource?: boolean;
    }) =>
        invoke<TimelineResult>(
            "replace_clip_source",
            payload.clipIds,
            payload.newSourcePath,
            payload.replaceSameSource,
        ),

    splitClip: (clipId: string, splitSec: number) =>
        invoke<TimelineResult>("split_clip", clipId, splitSec),

    splitClipsAt: (clipIds: string[], splitSec: number) =>
        invoke<TimelineResult>("split_clips_at", clipIds, splitSec),

    glueClips: (clipIds: string[]) => invoke<TimelineResult>("glue_clips", clipIds),

    groupClips: (clipIds: string[]) => invoke<TimelineResult>("group_clips", clipIds),

    ungroupClips: (clipIds: string[]) => invoke<TimelineResult>("ungroup_clips", clipIds),

    toggleGroupDisabled: (groupId: string) =>
        invoke<TimelineResult>("toggle_group_disabled", groupId),

    convertClipsToPitchReference: (clipIds: string[]) =>
        invoke<TimelineResult>("convert_clips_to_pitch_reference", clipIds),

    updatePitchReference: (clipIds: string[]) =>
        invoke<TimelineResult>("update_pitch_reference", clipIds),

    selectClip: (clipId: string | null) => invoke<TimelineResult>("select_clip", clipId),
};
