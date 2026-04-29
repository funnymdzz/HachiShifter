import { useRef } from "react";
import { batch } from "react-redux";
import type { AppDispatch } from "../../../../app/store";
import type { SessionState } from "../../../../features/session/sessionSlice";
import {
    checkpointHistory,
    moveClipStart,
    setClipFades,
    setClipGain,
    setClipLength,
    setClipPlaybackRate,
    setClipStateRemote,
    setClipsStateBulkRemote,
    setClipSourceRange,
    beginInteraction,
    endInteraction,
} from "../../../../features/session/sessionSlice";
import { applyAutoCrossfade } from "./autoCrossfade";
import { clamp } from "../math";
import { isModifierActive } from "../../../../features/keybindings/keybindingsSlice";
import type { Keybinding } from "../../../../features/keybindings/types";
import { paramsApi } from "../../../../services/api";
import { webApi } from "../../../../services/webviewApi";
import {
    buildStretchGroupState,
    computeStretchGroupUpdate,
    scaleClipFadesForStretch,
    type StretchGroupState,
} from "./stretchGroup";
import {
    applyBulkFadeValue,
    applyBulkGainDeltaDb,
    getBulkEditableClipIds,
} from "./bulkClipEdit";
import { buildBulkClipStateUpdates } from "./bulkClipRemotePayloads";

export function resolveStretchParamTypes(
    pitchEditUserModified: boolean | null | undefined,
): Array<"pitch" | "tension"> {
    // 未手动编辑的 pitch 曲线由后端根据 clip 几何自动重建，
    // 前端若再次映射会造成“二次拉伸”。
    if (pitchEditUserModified === false) {
        return ["tension"];
    }
    return ["pitch", "tension"];
}

/**
 * 拉伸后对参数线进行时域映射（拉伸或压缩）。
 * 将旧范围 [oldStartSec, oldStartSec+oldLengthSec] 内的参数值，
 * 线性重映射到新范围 [newStartSec, newStartSec+newLengthSec]，
 * 并将不再被音频块覆盖的旧帧恢复为原始值。
 */
async function stretchLinkedParams(
    trackId: string,
    oldStartSec: number,
    oldLengthSec: number,
    newStartSec: number,
    newLengthSec: number,
): Promise<void> {
    if (
        Math.abs(oldLengthSec - newLengthSec) < 1e-6 &&
        Math.abs(oldStartSec - newStartSec) < 1e-6
    ) {
        return;
    }

    // 获取帧周期（通过最小量探针请求）。
    // 同时读取 pitch_edit_user_modified 以决定是否应手动映射 pitch。
    const probe = await paramsApi.getParamFrames(trackId, "pitch", 0, 1, 1);
    if (!probe?.ok) return;
    const fp = Math.max(1, Number(probe.frame_period_ms) || 5);
    const stretchParams = resolveStretchParamTypes(probe.pitch_edit_user_modified);

    const oldStartFrame = Math.round((oldStartSec * 1000) / fp);
    const oldEndFrame = Math.round(((oldStartSec + oldLengthSec) * 1000) / fp);
    const oldFrameCount = Math.max(1, oldEndFrame - oldStartFrame);

    const newStartFrame = Math.round((newStartSec * 1000) / fp);
    const newEndFrame = Math.round(((newStartSec + newLengthSec) * 1000) / fp);
    const newFrameCount = Math.max(1, newEndFrame - newStartFrame);

    for (const paramType of stretchParams) {
        const res = await paramsApi.getParamFrames(
            trackId,
            paramType,
            oldStartFrame,
            oldFrameCount,
            1,
        );
        if (!res?.ok) continue;
        const oldValues = (res.edit ?? []).map((v) => Number(v) || 0);
        if (oldValues.length === 0) continue;

        // 线性插值时域映射：用旧帧值填充新帧
        const newValues = new Array<number>(newFrameCount);
        const oldMaxIdx = oldValues.length - 1;
        const newMaxIdx = newFrameCount > 1 ? newFrameCount - 1 : 1;
        const ratio = oldMaxIdx / newMaxIdx;

        for (let i = 0; i < newFrameCount; i++) {
            const oldIdxF = i * ratio;
            const lo = oldIdxF | 0;
            const hi = lo < oldMaxIdx ? lo + 1 : oldMaxIdx;
            const frac = oldIdxF - lo;
            const loVal = oldValues[lo] ?? 0;
            const hiVal = oldValues[hi] ?? 0;
            if (paramType === "pitch") {
                // pitch=0 表示无效（无声）帧，保留 0
                if (loVal === 0 && hiVal === 0) {
                    newValues[i] = 0;
                } else if (loVal === 0) {
                    newValues[i] = 0;
                } else if (hiVal === 0) {
                    newValues[i] = frac < 0.5 ? loVal : 0;
                } else {
                    newValues[i] = loVal + (hiVal - loVal) * frac;
                }
            } else {
                newValues[i] = loVal + (hiVal - loVal) * frac;
            }
        }

        // 将重映射后的值写入新范围
        await paramsApi.setParamFrames(trackId, paramType, newStartFrame, newValues, false);

        // 恢复旧范围中不再被新音频块覆盖的帧（还原到原始值）
        const newRangeMax = newStartFrame + newFrameCount - 1;
        const oldRangeMax = oldStartFrame + oldFrameCount - 1;

        if (oldStartFrame < newStartFrame) {
            const clearLen = newStartFrame - oldStartFrame;
            void paramsApi.restoreParamFrames(trackId, paramType, oldStartFrame, clearLen, false);
        }
        if (oldRangeMax > newRangeMax) {
            const clearFrom = newRangeMax + 1;
            const clearLen = oldRangeMax - newRangeMax;
            void paramsApi.restoreParamFrames(trackId, paramType, clearFrom, clearLen, false);
        }
    }
}

export type EditDragType =
    | "trim_left"
    | "trim_right"
    | "stretch_left"
    | "stretch_right"
    | "fade_in"
    | "fade_out"
    | "gain";

export type EditDragState = {
    type: EditDragType;
    pointerId: number;
    clipId: string;
    basestartSec: number;
    baselengthSec: number;
    basePlaybackRate: number;
    baseSourceStartSec: number;
    baseSourceEndSec: number;
    basefadeInSec: number;
    basefadeOutSec: number;
    baseGain: number;
    sourceBeats: number | null;
    rightEdgeBeat: number;
    baseReversed: boolean;
    baseDurationFrames: number | null;
    baseSourceSampleRate: number | null;
    baseDurationSec: number | null;
    stretchGroup: StretchGroupState | null;
    selectedClipIds: string[];
    baseGainById: Record<string, number>;
};

export function useEditDrag(deps: {
    scrollRef: React.RefObject<HTMLDivElement | null>;
    sessionRef: React.RefObject<SessionState>;
    dispatch: AppDispatch;
    multiSelectedClipIds: string[];
    multiSelectedSet: Set<string>;
    snapBeat: (beat: number) => number;
    beatFromClientX: (clientX: number, bounds: DOMRect, xScroll: number) => number;
    /** modifier.clipNoSnap 绑定 */
    noSnapKb: Keybinding;
    /** 网格吸附全局开关 */
    gridSnapEnabled: boolean;
}) {
    const {
        scrollRef,
        sessionRef,
        dispatch,
        multiSelectedClipIds,
        multiSelectedSet,
        snapBeat,
        beatFromClientX,
        noSnapKb,
        gridSnapEnabled,
    } = deps;

    const editDragRef = useRef<EditDragState | null>(null);
    // 用于节流向后端发送 clip 状态更新，避免拖动时频繁覆盖与后端同步引起闪烁
    const lastRemoteSentRef = useRef<Record<string, number>>({});

    function startEditDrag(e: React.PointerEvent, clipId: string, type: EditDragType) {
        if (e.button !== 0) return;
        const clip = sessionRef.current.clips.find((c) => c.id === clipId);
        if (!clip) return;
        const scroller = scrollRef.current;
        if (!scroller) return;
        const rightEdgeBeat = clip.startSec + clip.lengthSec;

        const selectedClipIds = getBulkEditableClipIds({
            activeClipId: clipId,
            multiSelectedClipIds,
            multiSelectedSet,
        });
        const baseGainById = Object.fromEntries(
            selectedClipIds.map((id) => {
                const selectedClip = sessionRef.current.clips.find((entry) => entry.id === id);
                return [id, Number(selectedClip?.gain ?? 1) || 1];
            }),
        ) as Record<string, number>;
        const stretchGroup =
            type === "stretch_left" || type === "stretch_right"
                ? buildStretchGroupState({
                      clips: sessionRef.current.clips,
                      selectedClipIds,
                      anchorClipId: clipId,
                      edge: type,
                  })
                : null;

        // 对于 gain 类型的拖动，使用 beginUndoGroup 将整个拖动操作包装为一个原子 undo entry
        if (type === "gain") {
            void webApi.beginUndoGroup();
        }

        dispatch(checkpointHistory());
        dispatch(beginInteraction());

        editDragRef.current = {
            type,
            pointerId: e.pointerId,
            clipId,
            basestartSec: clip.startSec,
            baselengthSec: clip.lengthSec,
            basePlaybackRate: Number(clip.playbackRate ?? 1) || 1,
            baseSourceStartSec: clip.sourceStartSec,
            baseSourceEndSec: clip.sourceEndSec,
            basefadeInSec: clip.fadeInSec,
            basefadeOutSec: clip.fadeOutSec,
            baseGain: clip.gain,
            sourceBeats: null,
            rightEdgeBeat,
            baseReversed: !!clip.reversed,
            baseDurationFrames: clip.durationFrames ?? null,
            baseSourceSampleRate: clip.sourceSampleRate ?? null,
            baseDurationSec: clip.durationSec ?? null,
            stretchGroup,
            selectedClipIds,
            baseGainById,
        };

        (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);

        let ticking = false;
        let latestEvent: PointerEvent | null = null;
        let accumulatedGainDeltaDb = 0;

        function onMove(ev: PointerEvent) {
            latestEvent = ev;
            if (ticking) return;
            ticking = true;

            requestAnimationFrame(() => {
                ticking = false;
                if (!latestEvent) return;
                const currentEv = latestEvent;

                const drag = editDragRef.current;
                const el = scrollRef.current;
                if (!drag || drag.pointerId !== e.pointerId || !el) return;
                const b = el.getBoundingClientRect();
                let beat = beatFromClientX(currentEv.clientX, b, el.scrollLeft);
                const shouldSnap =
                    drag.type === "trim_left" ||
                    drag.type === "trim_right" ||
                    drag.type === "stretch_left" ||
                    drag.type === "stretch_right";
                const noSnapActive = isModifierActive(noSnapKb, currentEv);
                const effectiveSnap = gridSnapEnabled ? !noSnapActive : noSnapActive;
                if (shouldSnap && effectiveSnap) {
                    beat = snapBeat(beat);
                }

                const minLen = 0.0;
                if (drag.type === "fade_in") {
                    const raw = beat - drag.basestartSec;
                    const next = clamp(raw, 0, Math.max(0, drag.baselengthSec));
                    const fadeUpdates = applyBulkFadeValue({
                        clipIds: drag.selectedClipIds,
                        clipsById: new Map(
                            sessionRef.current.clips.map((clip) => [clip.id, clip] as const),
                        ),
                        target: "fadeInSec",
                        nextValue: next,
                    });
                    batch(() => {
                        for (const update of fadeUpdates) {
                            dispatch(setClipFades(update));
                        }
                    });
                    try {
                        if (drag.selectedClipIds.length === 1) {
                            const now = Date.now();
                            const last = lastRemoteSentRef.current[drag.clipId] || 0;
                            if (now - last > 200) {
                                lastRemoteSentRef.current[drag.clipId] = now;
                                void dispatch(
                                    setClipStateRemote({ clipId: drag.clipId, fadeInSec: next }),
                                );
                            }
                        }
                    } catch {}
                    return;
                }
                if (drag.type === "fade_out") {
                    const raw = drag.rightEdgeBeat - beat;
                    const next = clamp(raw, 0, Math.max(0, drag.baselengthSec));
                    const fadeUpdates = applyBulkFadeValue({
                        clipIds: drag.selectedClipIds,
                        clipsById: new Map(
                            sessionRef.current.clips.map((clip) => [clip.id, clip] as const),
                        ),
                        target: "fadeOutSec",
                        nextValue: next,
                    });
                    batch(() => {
                        for (const update of fadeUpdates) {
                            dispatch(setClipFades(update));
                        }
                    });
                    try {
                        if (drag.selectedClipIds.length === 1) {
                            const now = Date.now();
                            const last = lastRemoteSentRef.current[drag.clipId] || 0;
                            if (now - last > 200) {
                                lastRemoteSentRef.current[drag.clipId] = now;
                                void dispatch(
                                    setClipStateRemote({ clipId: drag.clipId, fadeOutSec: next }),
                                );
                            }
                        }
                    } catch {}
                    return;
                }
                if (drag.type === "gain") {
                    const movementY = (currentEv.movementY ?? 0) as number;
                    const deltaDb = -movementY * 0.25;
                    accumulatedGainDeltaDb += deltaDb;
                    const gainUpdates = applyBulkGainDeltaDb({
                        clipIds: drag.selectedClipIds,
                        clipsById: new Map(
                            drag.selectedClipIds.map((id) => [
                                id,
                                { gain: drag.baseGainById[id] ?? 1 },
                            ]),
                        ),
                        deltaDb: accumulatedGainDeltaDb,
                        minDb: -12,
                        maxDb: 12,
                    });
                    batch(() => {
                        for (const update of gainUpdates) {
                            dispatch(setClipGain(update));
                        }
                    });
                    try {
                        if (drag.selectedClipIds.length === 1) {
                            const now = Date.now();
                            const last = lastRemoteSentRef.current[drag.clipId] || 0;
                            const nextGain = gainUpdates[0]?.gain;
                            if (nextGain != null && now - last > 200) {
                                lastRemoteSentRef.current[drag.clipId] = now;
                                void webApi.setClipState({
                                    clipId: drag.clipId,
                                    gain: nextGain,
                                    checkpoint: false,
                                });
                            }
                        }
                    } catch {}
                    return;
                }

                if (
                    drag.stretchGroup &&
                    (drag.type === "stretch_left" || drag.type === "stretch_right")
                ) {
                    const stretchGroup = drag.stretchGroup;
                    const update = computeStretchGroupUpdate({
                        group: stretchGroup,
                        edge: drag.type,
                        pointerSec: beat,
                    });
                    batch(() => {
                        for (const clipId of stretchGroup.clipIds) {
                            const next = update.byId[clipId];
                            if (!next) continue;
                            dispatch(
                                moveClipStart({
                                    clipId,
                                    startSec: next.startSec,
                                }),
                            );
                            dispatch(
                                setClipLength({
                                    clipId,
                                    lengthSec: next.lengthSec,
                                }),
                            );
                            dispatch(
                                setClipPlaybackRate({
                                    clipId,
                                    playbackRate: next.playbackRate,
                                }),
                            );
                            dispatch(
                                setClipFades({
                                    clipId,
                                    fadeInSec: next.fadeInSec,
                                    fadeOutSec: next.fadeOutSec,
                                }),
                            );
                        }
                    });
                    return;
                }

                if (drag.type === "trim_left") {
                    const desiredStart = clamp(beat, 0, drag.rightEdgeBeat - minLen);
                    const desiredDelta = desiredStart - drag.basestartSec;
                    const rate = drag.basePlaybackRate > 0 ? drag.basePlaybackRate : 1;
                    if (drag.baseReversed) {
                        const sourceDuration = (() => {
                            if (
                                drag.baseDurationFrames &&
                                drag.baseSourceSampleRate &&
                                drag.baseSourceSampleRate > 0
                            ) {
                                return drag.baseDurationFrames / drag.baseSourceSampleRate;
                            }
                            return drag.baseDurationSec || 0;
                        })();
                        let nextTrimEnd = drag.baseSourceEndSec - desiredDelta * rate;
                        nextTrimEnd = Math.max(drag.baseSourceStartSec, nextTrimEnd);
                        if (sourceDuration > 0) {
                            nextTrimEnd = Math.min(nextTrimEnd, sourceDuration);
                        }
                        const actualDeltaTrim = drag.baseSourceEndSec - nextTrimEnd;
                        const actualDeltaTimeline = actualDeltaTrim / rate;
                        const nextStart = drag.basestartSec + actualDeltaTimeline;
                        const nextLen = clamp(
                            drag.baselengthSec - actualDeltaTimeline,
                            minLen,
                            10_000,
                        );
                        dispatch(moveClipStart({ clipId: drag.clipId, startSec: nextStart }));
                        dispatch(setClipLength({ clipId: drag.clipId, lengthSec: nextLen }));
                        dispatch(
                            setClipSourceRange({ clipId: drag.clipId, sourceEndSec: nextTrimEnd }),
                        );
                        return;
                    }

                    let nextTrimStart = drag.baseSourceStartSec + desiredDelta * rate;
                    nextTrimStart = Math.max(0, nextTrimStart);
                    const actualDeltaTrim = nextTrimStart - drag.baseSourceStartSec;
                    const actualDeltaTimeline = actualDeltaTrim / rate;
                    const nextStart = drag.basestartSec + actualDeltaTimeline;
                    const nextLen = clamp(drag.baselengthSec - actualDeltaTimeline, minLen, 10_000);
                    dispatch(moveClipStart({ clipId: drag.clipId, startSec: nextStart }));
                    dispatch(setClipLength({ clipId: drag.clipId, lengthSec: nextLen }));
                    dispatch(
                        setClipSourceRange({ clipId: drag.clipId, sourceStartSec: nextTrimStart }),
                    );
                    return;
                }

                if (drag.type === "stretch_left") {
                    const desiredStart = clamp(beat, 0, drag.rightEdgeBeat - minLen);
                    const rawLen = clamp(drag.rightEdgeBeat - desiredStart, minLen, 10_000);
                    const baseLen = Math.max(1e-6, Number(drag.baselengthSec) || 0);
                    const baseRate =
                        drag.basePlaybackRate > 0 && Number.isFinite(drag.basePlaybackRate)
                            ? drag.basePlaybackRate
                            : 1;
                    const nextRate = clamp((baseRate * baseLen) / Math.max(1e-6, rawLen), 0.1, 10);
                    const correctedLen = (baseRate * baseLen) / nextRate;
                    const nextStart = drag.rightEdgeBeat - correctedLen;
                    const scaledFades = scaleClipFadesForStretch({
                        baseFadeInSec: drag.basefadeInSec,
                        baseFadeOutSec: drag.basefadeOutSec,
                        baseLengthSec: drag.baselengthSec,
                        nextLengthSec: correctedLen,
                    });
                    dispatch(moveClipStart({ clipId: drag.clipId, startSec: nextStart }));
                    dispatch(setClipLength({ clipId: drag.clipId, lengthSec: correctedLen }));
                    dispatch(setClipPlaybackRate({ clipId: drag.clipId, playbackRate: nextRate }));
                    dispatch(
                        setClipFades({
                            clipId: drag.clipId,
                            fadeInSec: scaledFades.fadeInSec,
                            fadeOutSec: scaledFades.fadeOutSec,
                        }),
                    );
                    return;
                }

                if (drag.type === "trim_right") {
                    const desiredRight = clamp(beat, drag.basestartSec + minLen, 10_000);
                    const rate = drag.basePlaybackRate > 0 ? drag.basePlaybackRate : 1;
                    const sourceDuration = (() => {
                        if (
                            drag.baseDurationFrames &&
                            drag.baseSourceSampleRate &&
                            drag.baseSourceSampleRate > 0
                        ) {
                            return drag.baseDurationFrames / drag.baseSourceSampleRate;
                        }
                        return drag.baseDurationSec || 0;
                    })();
                    const desiredLen = desiredRight - drag.basestartSec;
                    const nextLen = clamp(desiredLen, minLen, 10_000);
                    const usedDeltaTimeline = nextLen - drag.baselengthSec;
                    if (drag.baseReversed) {
                        let nextTrimStart = drag.baseSourceStartSec - usedDeltaTimeline * rate;
                        nextTrimStart = Math.max(0, nextTrimStart);
                        nextTrimStart = Math.min(nextTrimStart, drag.baseSourceEndSec);
                        const actualSourceLen = drag.baseSourceEndSec - nextTrimStart;
                        const maxTimelineLen = actualSourceLen / rate;
                        const finalLen =
                            maxTimelineLen > 0 ? Math.min(nextLen, maxTimelineLen) : nextLen;
                        dispatch(setClipLength({ clipId: drag.clipId, lengthSec: finalLen }));
                        dispatch(
                            setClipSourceRange({
                                clipId: drag.clipId,
                                sourceStartSec: nextTrimStart,
                            }),
                        );
                        return;
                    }

                    let nextTrimEnd = drag.baseSourceEndSec + usedDeltaTimeline * rate;
                    nextTrimEnd = Math.max(0, nextTrimEnd);
                    if (sourceDuration > 0) {
                        nextTrimEnd = Math.min(nextTrimEnd, sourceDuration);
                    }
                    const actualSourceLen = nextTrimEnd - drag.baseSourceStartSec;
                    const maxTimelineLen = actualSourceLen / rate;
                    const finalLen =
                        maxTimelineLen > 0 ? Math.min(nextLen, maxTimelineLen) : nextLen;
                    dispatch(setClipLength({ clipId: drag.clipId, lengthSec: finalLen }));
                    dispatch(
                        setClipSourceRange({ clipId: drag.clipId, sourceEndSec: nextTrimEnd }),
                    );
                    return;
                }

                if (drag.type === "stretch_right") {
                    const desiredRight = clamp(beat, drag.basestartSec + minLen, 10_000);
                    const rawLen = clamp(desiredRight - drag.basestartSec, minLen, 10_000);
                    const baseLen = Math.max(1e-6, Number(drag.baselengthSec) || 0);
                    const baseRate =
                        drag.basePlaybackRate > 0 && Number.isFinite(drag.basePlaybackRate)
                            ? drag.basePlaybackRate
                            : 1;
                    const nextRate = clamp((baseRate * baseLen) / Math.max(1e-6, rawLen), 0.1, 10);
                    const correctedLen = (baseRate * baseLen) / nextRate;
                    const scaledFades = scaleClipFadesForStretch({
                        baseFadeInSec: drag.basefadeInSec,
                        baseFadeOutSec: drag.basefadeOutSec,
                        baseLengthSec: drag.baselengthSec,
                        nextLengthSec: correctedLen,
                    });
                    dispatch(setClipLength({ clipId: drag.clipId, lengthSec: correctedLen }));
                    dispatch(setClipPlaybackRate({ clipId: drag.clipId, playbackRate: nextRate }));
                    dispatch(
                        setClipFades({
                            clipId: drag.clipId,
                            fadeInSec: scaledFades.fadeInSec,
                            fadeOutSec: scaledFades.fadeOutSec,
                        }),
                    );
                }
            });
        }

        function end() {
            const drag = editDragRef.current;
            if (!drag || drag.pointerId !== e.pointerId) return;
            editDragRef.current = null;

            // 如果是 gain 类型的拖动，确保 undo group 被正确结束
            const wasGainType = drag.type === "gain";

            const isGroupStretch =
                drag.stretchGroup != null &&
                (drag.type === "stretch_left" || drag.type === "stretch_right");

            const clipNow = sessionRef.current.clips.find((c) => c.id === drag.clipId);
            if (!isGroupStretch && !clipNow) {
                // 即使 clip 不存在也要结束 undo group
                if (wasGainType) {
                    void webApi.endUndoGroup();
                }
                dispatch(endInteraction());
                return;
            }
            const singleClipNow = clipNow ?? null;

            const autoCrossfadeClipIds =
                isGroupStretch && drag.stretchGroup ? drag.stretchGroup.clipIds : [drag.clipId];
            const shouldApplyAutoCrossfade =
                sessionRef.current.autoCrossfadeEnabled &&
                (drag.type === "trim_left" ||
                    drag.type === "trim_right" ||
                    drag.type === "stretch_left" ||
                    drag.type === "stretch_right");

            const runInsideUndoGroup = async (task: () => Promise<void>): Promise<void> => {
                await webApi.beginUndoGroup();
                try {
                    await task();
                } finally {
                    await webApi.endUndoGroup();
                }
            };

            const runWithOptionalAutoCrossfade = async (
                task: () => Promise<void>,
            ): Promise<void> => {
                if (!shouldApplyAutoCrossfade) {
                    await task();
                    return;
                }

                await runInsideUndoGroup(async () => {
                    await task();
                    await applyAutoCrossfade(sessionRef.current, autoCrossfadeClipIds, dispatch);
                });
            };

            // 交互锁在最终持久化请求完成后才释放，
            // 避免 endInteraction() 到 fulfilled 之间的窗口内，
            // 其他 in-flight thunk 的旧快照覆盖前端乐观更新导致闪烁。

            let persistPromise: Promise<unknown> | null = null;
            if (isGroupStretch && drag.stretchGroup) {
                const stretchPatches = drag.stretchGroup.clipIds
                    .map((id) => {
                        const now = sessionRef.current.clips.find((c) => c.id === id);
                        if (!now) return null;
                        return {
                            clipId: id,
                            startSec: now.startSec,
                            lengthSec: now.lengthSec,
                            playbackRate: now.playbackRate,
                            fadeInSec: now.fadeInSec,
                            fadeOutSec: now.fadeOutSec,
                        };
                    })
                    .filter(
                        (
                            patch,
                        ): patch is {
                            clipId: string;
                            startSec: number;
                            lengthSec: number;
                            playbackRate: number;
                            fadeInSec: number;
                            fadeOutSec: number;
                        } => patch != null,
                    );

                if (stretchPatches.length > 0) {
                    persistPromise = runInsideUndoGroup(async () => {
                        const stretchPersistPromises = stretchPatches.map((patch) =>
                            dispatch(
                                setClipStateRemote({
                                    clipId: patch.clipId,
                                    startSec: patch.startSec,
                                    lengthSec: patch.lengthSec,
                                    playbackRate: patch.playbackRate,
                                    fadeInSec: patch.fadeInSec,
                                    fadeOutSec: patch.fadeOutSec,
                                    checkpoint: false,
                                }),
                            ).unwrap(),
                        );
                        await Promise.allSettled(stretchPersistPromises);

                        if (shouldApplyAutoCrossfade) {
                            await applyAutoCrossfade(
                                sessionRef.current,
                                autoCrossfadeClipIds,
                                dispatch,
                            );
                        }
                    });
                }
            } else if (drag.type === "trim_left" && singleClipNow) {
                const sourceRangePatch = singleClipNow.reversed
                    ? { sourceEndSec: singleClipNow.sourceEndSec }
                    : { sourceStartSec: singleClipNow.sourceStartSec };
                if (shouldApplyAutoCrossfade) {
                    persistPromise = runWithOptionalAutoCrossfade(async () => {
                        await dispatch(
                            setClipStateRemote({
                                clipId: drag.clipId,
                                startSec: singleClipNow.startSec,
                                lengthSec: singleClipNow.lengthSec,
                                ...sourceRangePatch,
                                checkpoint: false,
                            }),
                        ).unwrap();
                    });
                } else {
                    persistPromise = dispatch(
                        setClipStateRemote({
                            clipId: drag.clipId,
                            startSec: singleClipNow.startSec,
                            lengthSec: singleClipNow.lengthSec,
                            ...sourceRangePatch,
                        }),
                    ).unwrap();
                }
            } else if (drag.type === "trim_right" && singleClipNow) {
                const sourceRangePatch = singleClipNow.reversed
                    ? { sourceStartSec: singleClipNow.sourceStartSec }
                    : { sourceEndSec: singleClipNow.sourceEndSec };
                if (shouldApplyAutoCrossfade) {
                    persistPromise = runWithOptionalAutoCrossfade(async () => {
                        await dispatch(
                            setClipStateRemote({
                                clipId: drag.clipId,
                                lengthSec: singleClipNow.lengthSec,
                                ...sourceRangePatch,
                                checkpoint: false,
                            }),
                        ).unwrap();
                    });
                } else {
                    persistPromise = dispatch(
                        setClipStateRemote({
                            clipId: drag.clipId,
                            lengthSec: singleClipNow.lengthSec,
                            ...sourceRangePatch,
                        }),
                    ).unwrap();
                }
            } else if (drag.type === "stretch_left" && singleClipNow) {
                if (shouldApplyAutoCrossfade) {
                    persistPromise = runWithOptionalAutoCrossfade(async () => {
                        await dispatch(
                            setClipStateRemote({
                                clipId: drag.clipId,
                                startSec: singleClipNow.startSec,
                                lengthSec: singleClipNow.lengthSec,
                                playbackRate: singleClipNow.playbackRate,
                                fadeInSec: singleClipNow.fadeInSec,
                                fadeOutSec: singleClipNow.fadeOutSec,
                                checkpoint: false,
                            }),
                        ).unwrap();
                    });
                } else {
                    persistPromise = dispatch(
                        setClipStateRemote({
                            clipId: drag.clipId,
                            startSec: singleClipNow.startSec,
                            lengthSec: singleClipNow.lengthSec,
                            playbackRate: singleClipNow.playbackRate,
                            fadeInSec: singleClipNow.fadeInSec,
                            fadeOutSec: singleClipNow.fadeOutSec,
                        }),
                    ).unwrap();
                }
            } else if (drag.type === "stretch_right" && singleClipNow) {
                if (shouldApplyAutoCrossfade) {
                    persistPromise = runWithOptionalAutoCrossfade(async () => {
                        await dispatch(
                            setClipStateRemote({
                                clipId: drag.clipId,
                                lengthSec: singleClipNow.lengthSec,
                                playbackRate: singleClipNow.playbackRate,
                                fadeInSec: singleClipNow.fadeInSec,
                                fadeOutSec: singleClipNow.fadeOutSec,
                                checkpoint: false,
                            }),
                        ).unwrap();
                    });
                } else {
                    persistPromise = dispatch(
                        setClipStateRemote({
                            clipId: drag.clipId,
                            lengthSec: singleClipNow.lengthSec,
                            playbackRate: singleClipNow.playbackRate,
                            fadeInSec: singleClipNow.fadeInSec,
                            fadeOutSec: singleClipNow.fadeOutSec,
                        }),
                    ).unwrap();
                }
            } else if (drag.type === "fade_in" && singleClipNow) {
                const changesById = new Map(
                    drag.selectedClipIds.map((clipId) => {
                        const nextClip = sessionRef.current.clips.find((c) => c.id === clipId);
                        return [clipId, { fadeInSec: nextClip?.fadeInSec ?? 0 }] as const;
                    }),
                );
                persistPromise = dispatch(
                    setClipsStateBulkRemote({
                        updates: buildBulkClipStateUpdates({
                            clipIds: drag.selectedClipIds,
                            changesById,
                        }),
                    }),
                ).unwrap();
            } else if (drag.type === "fade_out" && singleClipNow) {
                const changesById = new Map(
                    drag.selectedClipIds.map((clipId) => {
                        const nextClip = sessionRef.current.clips.find((c) => c.id === clipId);
                        return [clipId, { fadeOutSec: nextClip?.fadeOutSec ?? 0 }] as const;
                    }),
                );
                persistPromise = dispatch(
                    setClipsStateBulkRemote({
                        updates: buildBulkClipStateUpdates({
                            clipIds: drag.selectedClipIds,
                            changesById,
                        }),
                    }),
                ).unwrap();
            } else if (drag.type === "gain" && singleClipNow) {
                const changesById = new Map(
                    drag.selectedClipIds.map((clipId) => {
                        const nextClip = sessionRef.current.clips.find((c) => c.id === clipId);
                        return [clipId, { gain: nextClip?.gain ?? 1 }] as const;
                    }),
                );
                persistPromise = dispatch(
                    setClipsStateBulkRemote({
                        updates: buildBulkClipStateUpdates({
                            clipIds: drag.selectedClipIds,
                            changesById,
                        }),
                    }),
                ).unwrap();
                // 结束 gain 拖动时调用 endUndoGroup，结束 undo group
                void Promise.resolve(persistPromise).finally(() => {
                    void webApi.endUndoGroup();
                });
            }

            // 拉伸后同步参数线：当"锁定参数线"启用时，将旧范围内的参数值时域映射到新范围
            const isStretch = drag.type === "stretch_left" || drag.type === "stretch_right";
            if (isStretch && sessionRef.current.lockParamLinesEnabled && drag.stretchGroup) {
                const stretchTasks = drag.stretchGroup.clipIds.map((id) => {
                    const initial = drag.stretchGroup?.initialById[id];
                    const now = sessionRef.current.clips.find((c) => c.id === id);
                    if (!initial || !now?.trackId) {
                        return Promise.resolve();
                    }
                    return stretchLinkedParams(
                        now.trackId,
                        initial.startSec,
                        initial.lengthSec,
                        now.startSec,
                        now.lengthSec,
                    );
                });
                void Promise.resolve(persistPromise).then(() => Promise.allSettled(stretchTasks));
            } else if (
                isStretch &&
                sessionRef.current.lockParamLinesEnabled &&
                singleClipNow?.trackId
            ) {
                const stretchTrackId = singleClipNow.trackId;
                const oldStartSec = drag.basestartSec;
                const oldLengthSec = drag.baselengthSec;
                const newStartSec = singleClipNow.startSec;
                const newLengthSec = singleClipNow.lengthSec;
                void Promise.resolve(persistPromise).then(() =>
                    stretchLinkedParams(
                        stretchTrackId,
                        oldStartSec,
                        oldLengthSec,
                        newStartSec,
                        newLengthSec,
                    ),
                );
            }

            // 在所有持久化请求完成后释放交互锁
            void Promise.resolve(persistPromise).finally(() => {
                dispatch(endInteraction());
            });

            window.removeEventListener("pointermove", onMove);
            window.removeEventListener("pointerup", end);
            window.removeEventListener("pointercancel", end);
        }

        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", end);
        window.addEventListener("pointercancel", end);
    }

    return { editDragRef, startEditDrag };
}
