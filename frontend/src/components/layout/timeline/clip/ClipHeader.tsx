import React, { useRef, useState } from "react";
import type { ClipFormantMorph, ClipInfo } from "../../../../features/session/sessionTypes";
import { CLIP_HEADER_HEIGHT } from "../constants";
import { gainToDb } from "../math";
import { useI18n } from "../../../../i18n/I18nProvider";
import { useAppTheme } from "../../../../theme/AppThemeProvider";
import { resolveTimelineClipHeaderVisibility } from "../runtime/timelineClipHeaderVisibility";
import { buildTimelineClipVisualStyle } from "../runtime/timelineCanvasStyle";
import { ClipFormantButton } from "./ClipFormantButton";

const CLIP_GAIN_WHEEL_STEP_DB = 0.5;

export const ClipHeader: React.FC<{
    clip: ClipInfo;
    clipWidthPx: number;
    trackColor?: string;
    transparentVisuals?: boolean;
    ensureSelected: (clipId: string) => void;
    selectClipRemote: (clipId: string) => void;
    startEditDrag: (e: React.PointerEvent, clipId: string, type: "gain") => void;
    toggleClipMuted: (clipId: string, nextMuted: boolean) => void;
    isInMultiSelectedSet: boolean;
    multiSelectedCount: number;
    /** 触发内联重命名（由 ClipContextMenu 的"重命名"菜单项调用） */
    triggerRename?: boolean;
    onRenameCommit?: (clipId: string, newName: string) => void;
    onRenameDone?: () => void;
    /** 增益双击输入框提交（dB 值，已 clamp 到 -24~+12） */
    onGainCommit?: (clipId: string, db: number) => void;
    onFormantMorphCommit?: (clipId: string, value: ClipFormantMorph, checkpoint: boolean) => void;
}> = ({
    clip,
    clipWidthPx,
    trackColor,
    transparentVisuals = false,
    ensureSelected,
    selectClipRemote,
    startEditDrag,
    toggleClipMuted,
    isInMultiSelectedSet,
    multiSelectedCount,
    triggerRename = false,
    onRenameCommit,
    onRenameDone,
    onGainCommit,
}) => {
    const { t } = useI18n();
    const { mode, fontFamily } = useAppTheme();
    const isDark = mode === "dark";
    const gainDb = gainToDb(clip.gain);
    const clampedGainDb = Math.min(12, Math.max(-12, gainDb));
    const [wheelGainDb, setWheelGainDb] = useState<number | null>(null);
    const wheelTimerRef = useRef<number | null>(null);
    const pendingGainDbRef = useRef<number | null>(null);
    const pendingClipIdRef = useRef<string | null>(null);
    const activeGainDb = wheelGainDb !== null ? wheelGainDb : clampedGainDb;
    const gainKnobDeg = (activeGainDb / 12) * 135;

    // 监听 clip.gain 的变化，当 Redux 状态更新为期望值时清除 wheelGainDb
    // 这样可以避免在 onGainCommit 异步完成和 Redux 更新之间出现闪烁
    React.useEffect(() => {
        if (pendingGainDbRef.current !== null && pendingClipIdRef.current === clip.id) {
            const expectedGain = Math.pow(10, pendingGainDbRef.current / 20);
            if (Math.abs(clip.gain - expectedGain) < 1e-6) {
                setWheelGainDb(null);
                pendingGainDbRef.current = null;
                pendingClipIdRef.current = null;
            }
        }
    }, [clip.gain, clip.id]);

    // 根据 clip 像素宽度决定显示哪些元素（从右往左依次隐藏）
    // >= 152px: 全显示 | 116-152: 隐藏名称 | 96-116: 隐藏播放速率 | 68-96: 隐藏增益值+F | 52-68: 隐藏F | 32-52: 只留增益旋钮 | < 32px: 全隐藏
    const {
        showAny,
        showMute,
        showFormant,
        showGainKnob,
        showPlaybackRate,
        showGainLabel: showGainVal,
        showName,
    } = resolveTimelineClipHeaderVisibility(clipWidthPx);
    const visualStyle = buildTimelineClipVisualStyle({
        widthPx: clipWidthPx,
        trackColor,
        selected: false,
        muted: Boolean(clip.muted),
        gain: clip.gain,
        playbackRate: clip.playbackRate,
        name: clip.name,
        fontFamily,
    });

    // ── 增益双击输入框 ──────────────────────────────────────────────────────
    const [gainEditing, setGainEditing] = useState(false);
    const [gainInputVal, setGainInputVal] = useState("");
    const gainInputRef = useRef<HTMLInputElement>(null);

    function commitGainEdit() {
        const parsed = parseFloat(gainInputVal);
        if (!isNaN(parsed)) {
            // clamp 到 -12 ~ +12 dB
            const clamped = Math.min(12, Math.max(-12, parsed));
            onGainCommit?.(clip.id, clamped);
        }
        setGainEditing(false);
    }

    function cancelGainEdit() {
        setGainEditing(false);
    }

    // ── 名称内联编辑 ────────────────────────────────────────────────────────
    const [nameEditing, setNameEditing] = useState(false);
    const [nameInputVal, setNameInputVal] = useState("");
    const nameInputRef = useRef<HTMLInputElement>(null);

    // 外部触发重命名（来自右键菜单）
    React.useEffect(() => {
        if (triggerRename && !nameEditing) {
            setNameInputVal(clip.name);
            setNameEditing(true);
            setTimeout(() => {
                nameInputRef.current?.select();
            }, 0);
        }
    }, [triggerRename]);

    function commitNameEdit() {
        const trimmed = nameInputVal.trim();
        const finalName = trimmed.length > 0 ? trimmed : clip.name;
        onRenameCommit?.(clip.id, finalName);
        setNameEditing(false);
        onRenameDone?.();
    }

    function cancelNameEdit() {
        setNameEditing(false);
        onRenameDone?.();
    }

    if (!showAny) return null;
    const hideVisuals = transparentVisuals && !nameEditing && !gainEditing;

    return (
        <div
            className="absolute left-1 right-1 flex items-center gap-1 z-50 select-none"
            style={{
                top: 1,
                height: CLIP_HEADER_HEIGHT,
            }}
        >
            {/* 增益拖拽把手 */}
            {showGainKnob && (
                <div
                    title={t("clip_gain_drag_hint")}
                    style={{ cursor: "ns-resize", opacity: hideVisuals ? 0 : 1 }}
                    data-clip-gain-knob
                    onPointerDown={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        if (multiSelectedCount === 0 || !isInMultiSelectedSet) {
                            ensureSelected(clip.id);
                        }
                        selectClipRemote(clip.id);
                        startEditDrag(e, clip.id, "gain");
                    }}
                    onDoubleClick={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        onGainCommit?.(clip.id, 0);
                    }}
                    onWheel={(e) => {
                        if (!onGainCommit) return;
                        const rawDelta =
                            Math.abs(e.deltaY) >= Math.abs(e.deltaX) ? e.deltaY : e.deltaX;
                        if (!Number.isFinite(rawDelta) || Math.abs(rawDelta) < 0.01) {
                            return;
                        }

                        const direction = rawDelta < 0 ? 1 : -1;
                        const notches = Math.max(1, Math.round(Math.abs(rawDelta) / 100));
                        const nextDb = Math.min(
                            12,
                            Math.max(
                                -12,
                                activeGainDb + direction * CLIP_GAIN_WHEEL_STEP_DB * notches,
                            ),
                        );

                        e.preventDefault();
                        e.stopPropagation();

                        // 立即更新本地 UI，但延迟 200ms 才提交给后端
                        setWheelGainDb(nextDb);
                        pendingGainDbRef.current = nextDb;
                        pendingClipIdRef.current = clip.id;
                        if (wheelTimerRef.current !== null) {
                            window.clearTimeout(wheelTimerRef.current);
                        }
                        wheelTimerRef.current = window.setTimeout(() => {
                            onGainCommit(clip.id, nextDb);
                            // 不再立即清除 wheelGainDb，而是通过 useEffect 监听 clip.gain 变化来清除
                            wheelTimerRef.current = null;
                        }, 200);
                    }}
                >
                    <div
                        className="relative rounded-full border"
                        style={{
                            width: visualStyle.gainKnobRadius * 2 + 4,
                            height: visualStyle.gainKnobRadius * 2 + 4,
                            borderColor: visualStyle.gainKnobStroke,
                            backgroundColor: visualStyle.gainKnobFill,
                        }}
                    >
                        <span
                            className="absolute left-1/2 top-1/2 w-[2px] h-[7px] -translate-x-1/2 -translate-y-full rounded-full"
                            style={{
                                backgroundColor: visualStyle.gainKnobIndicator,
                                transform: `translate(-50%, -100%) rotate(${gainKnobDeg}deg)`,
                                transformOrigin: "50% 100%",
                            }}
                        />
                        <span
                            className="absolute left-1/2 top-1/2 h-[4px] w-[4px] -translate-x-1/2 -translate-y-1/2 rounded-full"
                            style={{ backgroundColor: visualStyle.gainKnobCoreFill }}
                        />
                    </div>
                </div>
            )}

            {/* 静音按钮 */}
            {showMute && (
                <button
                    className="rounded flex items-center justify-center border transition-all text-[10px] font-bold"
                    onPointerDown={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                    }}
                    onClick={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        toggleClipMuted(clip.id, !Boolean(clip.muted));
                    }}
                    title={clip.muted ? t("clip_unmute") : t("clip_mute")}
                    style={{
                        opacity: hideVisuals ? 0 : 1,
                        width: visualStyle.muteBadgeWidth,
                        height: visualStyle.muteBadgeHeight,
                        backgroundColor: visualStyle.muteBadgeFill,
                        borderColor: visualStyle.muteBadgeStroke,
                        color: visualStyle.muteBadgeTextFill,
                    }}
                >
                    M
                </button>
            )}

            <ClipFormantButton
                clip={clip}
                hidden={!showFormant}
                opacity={hideVisuals ? 0 : 1}
                width={visualStyle.muteBadgeWidth}
                height={visualStyle.muteBadgeHeight}
                baseBackgroundColor={visualStyle.muteBadgeFill}
                baseBorderColor={visualStyle.muteBadgeStroke}
                baseTextColor={visualStyle.muteBadgeTextFill}
            />

            {/* Clip 名称区域 */}
            {showName && (
                <div className="flex-1 min-w-0">
                    {nameEditing ? (
                        <input
                            ref={nameInputRef}
                            className="w-full text-xs font-medium rounded px-1 outline-none"
                            style={{
                                color: isDark ? "rgba(255,255,255,0.95)" : "rgba(0,0,0,0.88)",
                                backgroundColor: isDark
                                    ? "rgba(0,0,0,0.45)"
                                    : "rgba(255,255,255,0.70)",
                                border: `1px solid ${isDark ? "rgba(255,255,255,0.40)" : "rgba(0,0,0,0.35)"}`,
                            }}
                            value={nameInputVal}
                            onChange={(e) => setNameInputVal(e.target.value)}
                            onKeyDown={(e) => {
                                e.stopPropagation();
                                if (e.key === "Enter") commitNameEdit();
                                if (e.key === "Escape") cancelNameEdit();
                            }}
                            onBlur={commitNameEdit}
                            onPointerDown={(e) => e.stopPropagation()}
                            onDoubleClick={(e) => e.stopPropagation()}
                        />
                    ) : (
                        <div
                            className="text-xs font-medium drop-shadow-md truncate cursor-text"
                            style={{
                                color: visualStyle.textFill,
                                opacity: hideVisuals ? 0 : 1,
                            }}
                            onDoubleClick={(e) => {
                                e.preventDefault();
                                e.stopPropagation();
                                setNameInputVal(clip.name);
                                setNameEditing(true);
                                setTimeout(() => nameInputRef.current?.select(), 0);
                            }}
                        >
                            {clip.name}
                        </div>
                    )}
                </div>
            )}

            {/* 播放倍率 / 增益数值显示 */}
            {showGainVal && (
                <div className="ml-auto flex items-center gap-2 min-w-0">
                    {showPlaybackRate && (
                        <div
                            className="text-[10px] tracking-wide"
                            style={{
                                color: "rgba(208, 216, 223, 0.76)",
                                opacity: hideVisuals ? 0 : 1,
                            }}
                        >
                            {visualStyle.playbackRateLabel}
                        </div>
                    )}
                    {gainEditing ? (
                        <input
                            ref={gainInputRef}
                            className="w-14 text-xs rounded px-1 outline-none text-right"
                            style={{
                                color: isDark ? "rgba(255,255,255,0.94)" : "rgba(0,0,0,0.88)",
                                backgroundColor: isDark
                                    ? "rgba(0,0,0,0.45)"
                                    : "rgba(255,255,255,0.70)",
                                border: `1px solid ${isDark ? "rgba(255,255,255,0.40)" : "rgba(0,0,0,0.35)"}`,
                            }}
                            value={gainInputVal}
                            onChange={(e) => setGainInputVal(e.target.value)}
                            onKeyDown={(e) => {
                                e.stopPropagation();
                                if (e.key === "Enter") commitGainEdit();
                                if (e.key === "Escape") cancelGainEdit();
                            }}
                            onBlur={commitGainEdit}
                            onPointerDown={(e) => e.stopPropagation()}
                        />
                    ) : (
                        <div
                            className="text-xs drop-shadow-md cursor-ns-resize"
                            style={{
                                color: "rgba(233, 239, 244, 0.82)",
                                opacity: hideVisuals ? 0 : 1,
                            }}
                            title={t("clip_gain_drag_hint")}
                            onDoubleClick={(e) => {
                                e.preventDefault();
                                e.stopPropagation();
                                onGainCommit?.(clip.id, 0);
                            }}
                        >
                            {activeGainDb >= 0 ? "+" : ""}
                            {activeGainDb.toFixed(1)}dB
                        </div>
                    )}
                </div>
            )}
        </div>
    );
};
