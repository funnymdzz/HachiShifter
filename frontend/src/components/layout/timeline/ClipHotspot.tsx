import React from "react";

import type { ClipInfo } from "../../../features/session/sessionTypes";
import { CLIP_BODY_PADDING_Y } from "./constants";

export const ClipHotspot = React.memo(function ClipHotspot({
    clip,
    rowHeight,
    pxPerSec,
    altPressed = false,
    multiSelectedCount,
    isInMultiSelectedSet,
    ensureSelected,
    selectClipRemote,
    openContextMenu,
    seekFromClientX,
    startClipDrag,
    startEditDrag,
    onCtrlToggleSelect,
    onShiftRangeSelect,
    rangeSelectAnchorClipId,
    clearContextMenu,
    onHoverChange,
}: {
    clip: ClipInfo;
    rowHeight: number;
    pxPerSec: number;
    altPressed?: boolean;
    multiSelectedCount: number;
    isInMultiSelectedSet: boolean;
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
        type: "trim_left" | "trim_right" | "stretch_left" | "stretch_right",
    ) => void;
    onCtrlToggleSelect: (clipId: string) => void;
    onShiftRangeSelect: (clipId: string, anchorClipIdOverride?: string | null) => void;
    rangeSelectAnchorClipId: string | null;
    clearContextMenu: () => void;
    onHoverChange?: (clipId: string | null) => void;
}) {
    const left = Math.max(0, Math.round(clip.startSec * pxPerSec));
    const width = Math.max(1, Math.round(clip.lengthSec * pxPerSec));

    const primeSelection = React.useCallback(
        (shouldPrimeSelection: boolean) => {
            if (!shouldPrimeSelection) {
                return;
            }
            if (multiSelectedCount === 0 || !isInMultiSelectedSet) {
                ensureSelected(clip.id);
            }
            selectClipRemote(clip.id);
        },
        [
            clip.id,
            ensureSelected,
            isInMultiSelectedSet,
            multiSelectedCount,
            selectClipRemote,
        ],
    );

    const beginEdgeInteraction = React.useCallback(
        (
            event: React.PointerEvent<HTMLDivElement>,
            edge: "left" | "right",
        ) => {
            if (event.button !== 0) return;

            const alt = Boolean(
                altPressed || event.altKey || event.nativeEvent.getModifierState?.("Alt"),
            );
            const ctrlOrMeta = event.ctrlKey || event.metaKey;
            const doShiftRangeSelect = event.shiftKey && !alt && !ctrlOrMeta;
            const shiftRangeAnchorClipId = doShiftRangeSelect ? rangeSelectAnchorClipId : null;
            const doCtrlToggleOnly = ctrlOrMeta && !event.shiftKey && !alt;
            const shouldPrimeSelection = !doCtrlToggleOnly && !doShiftRangeSelect;

            event.preventDefault();
            event.stopPropagation();
            clearContextMenu();
            primeSelection(shouldPrimeSelection);

            const startX = event.clientX;
            const startY = event.clientY;
            const pointerId = event.pointerId;
            const targetEl = event.currentTarget as HTMLElement;
            const mode =
                edge === "left"
                    ? alt
                        ? "stretch_left"
                        : "trim_left"
                    : alt
                      ? "stretch_right"
                      : "trim_right";
            let dragStarted = false;

            const onMove = (ev: PointerEvent) => {
                if (ev.pointerId !== pointerId || dragStarted) return;
                const dx = ev.clientX - startX;
                const dy = ev.clientY - startY;
                if (dx * dx + dy * dy < 9) return;
                dragStarted = true;
                startEditDrag(
                    {
                        button: 0,
                        pointerId,
                        currentTarget: targetEl,
                    } as unknown as React.PointerEvent,
                    clip.id,
                    mode,
                );
            };

            const onEnd = (ev: PointerEvent) => {
                if (ev.pointerId !== pointerId) return;
                window.removeEventListener("pointermove", onMove, true);
                window.removeEventListener("pointerup", onEnd, true);
                window.removeEventListener("pointercancel", onEnd, true);
                if (!dragStarted) {
                    if (doCtrlToggleOnly) {
                        onCtrlToggleSelect(clip.id);
                        return;
                    }
                    if (doShiftRangeSelect) {
                        onShiftRangeSelect(clip.id, shiftRangeAnchorClipId);
                        return;
                    }
                    seekFromClientX(ev.clientX, true);
                }
            };

            window.addEventListener("pointermove", onMove, true);
            window.addEventListener("pointerup", onEnd, true);
            window.addEventListener("pointercancel", onEnd, true);
        },
        [
            altPressed,
            clip.id,
            onCtrlToggleSelect,
            onShiftRangeSelect,
            primeSelection,
            rangeSelectAnchorClipId,
            seekFromClientX,
            startEditDrag,
        ],
    );

    return (
        <div
            data-hs-clip-hotspot="1"
            className="absolute cursor-pointer overflow-visible"
            style={{
                left,
                width,
                top: 0,
                height: rowHeight - CLIP_BODY_PADDING_Y,
                zIndex: 20,
            }}
            title={clip.sourcePath ?? clip.name}
            onContextMenu={(event) => {
                event.preventDefault();
                event.stopPropagation();
                if (multiSelectedCount <= 1) {
                    ensureSelected(clip.id);
                    selectClipRemote(clip.id);
                }
                openContextMenu(clip.id, event.clientX, event.clientY);
            }}
            onPointerEnter={() => {
                onHoverChange?.(clip.id);
            }}
            onPointerDown={(event) => {
                if (event.button !== 0) return;

                const alt = Boolean(
                    altPressed || event.altKey || event.nativeEvent.getModifierState?.("Alt"),
                );
                const ctrlOrMeta = event.ctrlKey || event.metaKey;
                const doShiftRangeSelect = event.shiftKey && !alt && !ctrlOrMeta;
                const shiftRangeAnchorClipId = doShiftRangeSelect ? rangeSelectAnchorClipId : null;
                const doCtrlToggleOnly = ctrlOrMeta && !event.shiftKey && !alt;
                const allowSeek = !alt && !ctrlOrMeta && !event.shiftKey;
                const shouldPrimeSelection = !doCtrlToggleOnly && !doShiftRangeSelect;
                const startX = event.clientX;
                const startY = event.clientY;
                let moved = false;

                event.preventDefault();
                event.stopPropagation();
                clearContextMenu();

                const onMove = (ev: PointerEvent) => {
                    if (ev.pointerId !== event.pointerId) return;
                    const dx = ev.clientX - startX;
                    const dy = ev.clientY - startY;
                    if (dx * dx + dy * dy >= 9) moved = true;
                };

                window.addEventListener("pointermove", onMove, true);

                const onUp = (ev: PointerEvent) => {
                    if (ev.pointerId !== event.pointerId) return;
                    window.removeEventListener("pointermove", onMove, true);
                    window.removeEventListener("pointerup", onUp, true);
                    window.removeEventListener("pointercancel", onUp, true);
                    if (doShiftRangeSelect && !moved) {
                        onShiftRangeSelect(clip.id, shiftRangeAnchorClipId);
                    } else if (!moved && allowSeek) {
                        seekFromClientX(ev.clientX, true);
                    }
                };

                window.addEventListener("pointerup", onUp, true);
                window.addEventListener("pointercancel", onUp, true);

                primeSelection(shouldPrimeSelection);
                startClipDrag(event, clip.id, clip.startSec, alt);
            }}
        >
            <div
                className="absolute left-0 top-0 bottom-0 w-[10px]"
                style={{ cursor: altPressed ? "col-resize" : "ew-resize" }}
                onPointerDown={(event) => {
                    beginEdgeInteraction(event, "left");
                }}
            />
            <div
                className="absolute right-0 top-0 bottom-0 w-[10px]"
                style={{ cursor: altPressed ? "col-resize" : "ew-resize" }}
                onPointerDown={(event) => {
                    beginEdgeInteraction(event, "right");
                }}
            />
        </div>
    );
});
