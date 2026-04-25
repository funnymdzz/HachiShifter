/**
 * TrackLane - 时间轴单轨道视图，负责布局轨道波形、剪辑项与拖拽中的 ghost 预览。
 */
import React from "react";

import type { ClipInfo, TrackInfo } from "../../../features/session/sessionTypes";
import type { GhostDragInfo } from "./hooks/useClipDrag";
import { ClipItem } from "./ClipItem";
import { ClipHotspot } from "./ClipHotspot";
import { CLIP_HEADER_HEIGHT, CLIP_BODY_PADDING_Y } from "./constants";
import { WaveformTrackCanvas } from "../../waveform/WaveformTrackCanvas";
import { useAppTheme } from "../../../theme/AppThemeProvider";
import { getWaveformColors } from "../../../theme/waveformColors";

function compareClipRenderOrder(a: ClipInfo, b: ClipInfo): number {
    const d = (a.startSec ?? 0) - (b.startSec ?? 0);
    if (Math.abs(d) > 1e-9) return d;
    return String(a.id).localeCompare(String(b.id));
}

function sameStringArray(a: string[] | undefined, b: string[] | undefined): boolean {
    if (a === b) return true;
    if (!a || !b) return !a && !b;
    if (a.length !== b.length) return false;
    for (let index = 0; index < a.length; index += 1) {
        if (a[index] !== b[index]) return false;
    }
    return true;
}

/**
 * 计算每个 clip 在“自身左侧前导区”的重叠时长（秒）。
 *
 * 该前导重叠区对应“该 clip 在当前渲染顺序中位于上层”的区域，
 * 用于在重叠区做等权可视化混合，避免后绘制 clip 完全盖住前一个 clip。
 */
export function computeLeadingOverlapSecByClipId(clips: ClipInfo[]): Record<string, number> {
    const sorted = [...clips].sort(compareClipRenderOrder);
    const leadingOverlapSecByClipId: Record<string, number> = {};

    for (let i = 0; i < sorted.length; i += 1) {
        const clip = sorted[i];
        const clipStart = clip.startSec;
        const clipEnd = clip.startSec + clip.lengthSec;
        let leadingOverlapEnd = clipStart;

        for (let j = 0; j < i; j += 1) {
            const other = sorted[j];
            const otherEnd = other.startSec + other.lengthSec;
            const overlapEnd = Math.min(clipEnd, otherEnd);
            if (overlapEnd <= clipStart + 1e-9) continue;
            if (overlapEnd > leadingOverlapEnd) {
                leadingOverlapEnd = overlapEnd;
            }
        }

        leadingOverlapSecByClipId[clip.id] = Math.max(0, leadingOverlapEnd - clipStart);
    }

    return leadingOverlapSecByClipId;
}

type TrackLaneProps = {
    track: TrackInfo;
    allTracks: TrackInfo[];
    trackClips: ClipInfo[];

    rowHeight: number;
    pxPerSec: number;
    bpm: number;
    viewportWidthPx: number;
    viewportStartSec: number;
    viewportEndSec: number;
    overlayClipIds?: string[];

    altPressed: boolean;

    selectedClipId: string | null;
    multiSelectedClipIds: string[];
    multiSelectedSet: Set<string>;

    /** 轨道主题色，用于 Clip 背景色和选中边框�?*/
    trackColor?: string;

    ensureSelected: (clipId: string) => void;
    selectClipRemote: (clipId: string) => void;
    openContextMenu: (clipId: string, clientX: number, clientY: number) => void;

    seekFromClientX: (clientX: number, commit: boolean) => void;
    startClipDrag: (
        e: React.PointerEvent<HTMLDivElement>,
        clipId: string,
        clipstartSec: number,
        altPressedHint?: boolean,
    ) => void;
    startEditDrag: (
        e: React.PointerEvent,
        clipId: string,
        type:
            | "trim_left"
            | "trim_right"
            | "stretch_left"
            | "stretch_right"
            | "fade_in"
            | "fade_out"
            | "gain",
    ) => void;
    toggleClipMuted: (clipId: string, nextMuted: boolean) => void;
    /** Ctrl+左键选择切换（会更新主选中 clip） */
    onCtrlToggleSelect: (clipId: string) => void;
    /** Ctrl+左键多选切换 */
    toggleMultiSelect: (clipId: string) => void;
    /** Shift+点击范围选择 */
    onShiftRangeSelect: (clipId: string, anchorClipIdOverride?: string | null) => void;
    /** Shift 范围选择锚点（点击前快照） */
    rangeSelectAnchorClipId: string | null;

    clearContextMenu: () => void;

    /** 当前正在重命名的 clipId（来自右键菜单触发） */
    renamingClipId?: string | null;
    onRenameCommit?: (clipId: string, newName: string) => void;
    onRenameDone?: () => void;
    onGainCommit?: (clipId: string, db: number) => void;

    /** Ctrl+拖动复制时的 ghost 预览信息 */
    ghostDrag?: GhostDragInfo | null;
    /** 所有 clip 数据（用于跨轨道 ghost 查找） */
    allClips?: ClipInfo[];
};

export const TrackLane = React.memo(function TrackLane(props: TrackLaneProps) {
    const {
        track,
        allTracks,
        trackClips,
        rowHeight,
        pxPerSec,
        viewportWidthPx,
        viewportStartSec,
        viewportEndSec,
        overlayClipIds = [],
        altPressed,
        selectedClipId,
        multiSelectedClipIds,
        multiSelectedSet,
        trackColor,
        ensureSelected,
        selectClipRemote,
        openContextMenu,
        seekFromClientX,
        startClipDrag,
        startEditDrag,
        toggleClipMuted,
        onCtrlToggleSelect,
        toggleMultiSelect,
        onShiftRangeSelect,
        rangeSelectAnchorClipId,
        clearContextMenu,
        renamingClipId,
        onRenameCommit,
        onRenameDone,
        onGainCommit,
        ghostDrag,
        allClips,
    } = props;

    // 获取波形颜色配置
    const { mode: themeMode } = useAppTheme();
    const waveformColors = React.useMemo(
        () => getWaveformColors(themeMode, "timeline"),
        [themeMode],
    );

    // 波形区域高度计算（与 ClipItem 一致）
    const waveformHeight = Math.max(1, rowHeight - CLIP_BODY_PADDING_Y - CLIP_HEADER_HEIGHT);
    const [hoveredClipId, setHoveredClipId] = React.useState<string | null>(null);

    // 计算当前轨道上需要渲染的 ghost clip 列表
    const ghostClips = React.useMemo(() => {
        if (!ghostDrag) return [];
        const result: { clip: ClipInfo; ghostStartSec: number }[] = [];
        const orderedTrackIds = allTracks.map((t) => t.id);
        const trackIndexById = Object.fromEntries(
            orderedTrackIds.map((id, idx) => [id, idx]),
        ) as Record<string, number>;
        const clipById = new Map((allClips ?? []).map((clip) => [clip.id, clip] as const));
        for (const clipId of ghostDrag.clipIds) {
            const initial = ghostDrag.initialById[clipId];
            if (!initial) continue;
            // 判断 ghost 是否应出现在当前轨道上
            let ghostTrackId = initial.trackId;
            if (ghostDrag.allowTrackMove) {
                if (ghostDrag.targetTrackId == null) {
                    continue;
                } else {
                    const sourceIndex = trackIndexById[initial.trackId];
                    const targetIndex = sourceIndex + ghostDrag.targetTrackOffset;
                    ghostTrackId = orderedTrackIds[targetIndex] ?? initial.trackId;
                }
            }
            if (ghostTrackId !== track.id) continue;
            const clip = clipById.get(clipId);
            if (!clip) continue;
            result.push({
                clip,
                ghostStartSec: Math.max(0, initial.startSec + ghostDrag.deltaSec),
            });
        }
        return result;
    }, [ghostDrag, track.id, trackClips, allClips, allTracks]);

    const leadingOverlapSecByClipId = React.useMemo(
        () => computeLeadingOverlapSecByClipId(trackClips),
        [trackClips],
    );
    const overlayClipIdSet = React.useMemo(() => {
        const next = new Set(overlayClipIds);
        if (hoveredClipId) {
            next.add(hoveredClipId);
        }
        return next;
    }, [hoveredClipId, overlayClipIds]);
    const overlayTrackClips = React.useMemo(
        () => trackClips.filter((clip) => overlayClipIdSet.has(clip.id)),
        [overlayClipIdSet, trackClips],
    );
    const hotspotTrackClips = React.useMemo(
        () => trackClips.filter((clip) => !overlayClipIdSet.has(clip.id)),
        [overlayClipIdSet, trackClips],
    );

    return (
        <div
            key={track.id}
            className="border-b border-qt-border relative"
            style={{ height: rowHeight }}
            onPointerLeave={() => {
                setHoveredClipId(null);
            }}
        >
            {/* 轨道级波形 Canvas：一个 Canvas 绘制该轨道所有可见 clip 的波形 */}
            <WaveformTrackCanvas
                clips={trackClips}
                leadingOverlapSecByClipId={leadingOverlapSecByClipId}
                trackHeight={rowHeight}
                waveformTop={CLIP_HEADER_HEIGHT}
                waveformHeight={waveformHeight}
                pxPerSec={pxPerSec}
                viewportWidthPx={viewportWidthPx}
                viewportStartSec={viewportStartSec}
                viewportEndSec={viewportEndSec}
                strokeColor={waveformColors.stroke}
                strokeWidth={1}
            />
            {hotspotTrackClips.map((clip) => (
                <ClipHotspot
                    key={`hotspot-${clip.id}`}
                    clip={clip}
                    rowHeight={rowHeight}
                    pxPerSec={pxPerSec}
                    altPressed={altPressed}
                    multiSelectedCount={multiSelectedClipIds.length}
                    isInMultiSelectedSet={multiSelectedSet.has(clip.id)}
                    ensureSelected={ensureSelected}
                    selectClipRemote={selectClipRemote}
                    openContextMenu={openContextMenu}
                    seekFromClientX={seekFromClientX}
                    startClipDrag={startClipDrag}
                    startEditDrag={startEditDrag}
                    onCtrlToggleSelect={onCtrlToggleSelect}
                    onShiftRangeSelect={onShiftRangeSelect}
                    rangeSelectAnchorClipId={rangeSelectAnchorClipId}
                    clearContextMenu={clearContextMenu}
                    onHoverChange={setHoveredClipId}
                />
            ))}
            {overlayTrackClips.map((clip) => {
                const selected =
                    multiSelectedClipIds.length > 0
                        ? multiSelectedSet.has(clip.id)
                        : selectedClipId === clip.id;

                return (
                    <ClipItem
                        key={clip.id}
                        clip={clip}
                        rowHeight={rowHeight}
                        pxPerSec={pxPerSec}
                        altPressed={altPressed}
                        selected={selected}
                        leadingOverlapSec={leadingOverlapSecByClipId[clip.id] ?? 0}
                        isInMultiSelectedSet={multiSelectedSet.has(clip.id)}
                        multiSelectedCount={multiSelectedClipIds.length}
                        trackColor={trackColor}
                        ensureSelected={ensureSelected}
                        selectClipRemote={selectClipRemote}
                        openContextMenu={openContextMenu}
                        seekFromClientX={seekFromClientX}
                        startClipDrag={startClipDrag}
                        startEditDrag={startEditDrag}
                        toggleClipMuted={toggleClipMuted}
                        onCtrlToggleSelect={onCtrlToggleSelect}
                        toggleMultiSelect={toggleMultiSelect}
                        onShiftRangeSelect={onShiftRangeSelect}
                        rangeSelectAnchorClipId={rangeSelectAnchorClipId}
                        clearContextMenu={clearContextMenu}
                        triggerRename={renamingClipId === clip.id}
                        onRenameCommit={onRenameCommit}
                        onRenameDone={onRenameDone}
                        onGainCommit={onGainCommit}
                    />
                );
            })}
            {/* Ghost clip 预览：Ctrl+拖动复制时显示半透明副本 */}
            {ghostClips.map(({ clip, ghostStartSec }) => {
                const ghostLeft = Math.max(0, ghostStartSec * pxPerSec);
                const ghostWidth = Math.max(1, clip.lengthSec * pxPerSec);
                return (
                    <div
                        key={`ghost-${clip.id}`}
                        className="absolute pointer-events-none opacity-50"
                        style={{
                            left: ghostLeft,
                            width: ghostWidth,
                            top: 0,
                            height: rowHeight - CLIP_BODY_PADDING_Y,
                        }}
                    >
                        {/* Ghost header 条 */}
                        <div
                            className="absolute left-0 right-0 top-0"
                            style={{
                                height: CLIP_HEADER_HEIGHT,
                                backgroundColor: trackColor
                                    ? `color-mix(in oklab, var(--qt-clip-bg) 56%, ${trackColor} 44%)`
                                    : "var(--qt-clip-bg)",
                            }}
                        />
                        {/* Ghost body 区域 */}
                        <div
                            className="absolute left-0 right-0 bottom-0 border border-dashed border-white/60"
                            style={{
                                top: CLIP_HEADER_HEIGHT,
                                backgroundColor: trackColor
                                    ? `color-mix(in oklab, var(--qt-clip-bg) 60%, ${trackColor} 40%)`
                                    : "var(--qt-clip-bg)",
                            }}
                        />
                    </div>
                );
            })}
        </div>
    );
}, (prev, next) => {
    return (
        prev.track === next.track &&
        prev.allTracks === next.allTracks &&
        prev.trackClips === next.trackClips &&
        prev.rowHeight === next.rowHeight &&
        prev.pxPerSec === next.pxPerSec &&
        prev.bpm === next.bpm &&
        prev.viewportWidthPx === next.viewportWidthPx &&
        prev.altPressed === next.altPressed &&
        prev.selectedClipId === next.selectedClipId &&
        prev.multiSelectedClipIds === next.multiSelectedClipIds &&
        prev.multiSelectedSet === next.multiSelectedSet &&
        prev.trackColor === next.trackColor &&
        prev.ensureSelected === next.ensureSelected &&
        prev.selectClipRemote === next.selectClipRemote &&
        prev.openContextMenu === next.openContextMenu &&
        prev.seekFromClientX === next.seekFromClientX &&
        prev.startClipDrag === next.startClipDrag &&
        prev.startEditDrag === next.startEditDrag &&
        prev.toggleClipMuted === next.toggleClipMuted &&
        prev.onCtrlToggleSelect === next.onCtrlToggleSelect &&
        prev.toggleMultiSelect === next.toggleMultiSelect &&
        prev.onShiftRangeSelect === next.onShiftRangeSelect &&
        prev.rangeSelectAnchorClipId === next.rangeSelectAnchorClipId &&
        prev.clearContextMenu === next.clearContextMenu &&
        prev.renamingClipId === next.renamingClipId &&
        prev.onRenameCommit === next.onRenameCommit &&
        prev.onRenameDone === next.onRenameDone &&
        prev.onGainCommit === next.onGainCommit &&
        prev.ghostDrag === next.ghostDrag &&
        prev.allClips === next.allClips &&
        sameStringArray(prev.overlayClipIds, next.overlayClipIds)
        // viewportStartSec / viewportEndSec are consumed by WaveformTrackCanvas via the viewport bus
        // after mount, so pure horizontal scroll should not force a TrackLane rerender.
    );
});
