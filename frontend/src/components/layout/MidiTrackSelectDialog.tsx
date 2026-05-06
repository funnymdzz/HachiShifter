import React, { useCallback, useEffect, useState } from "react";
import { Dialog, Flex, Text, Button, ScrollArea, RadioGroup } from "@radix-ui/themes";
import { useI18n } from "../../i18n/I18nProvider";
import { paramsApi } from "../../services/api/params";

/** MIDI 轨道信息（与后端返回结构对齐） */
interface MidiTrackInfo {
    index: number;
    name: string;
    note_count: number;
    min_note: number;
    max_note: number;
}

interface MidiTrackSelectDialogProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    /** MIDI 文件路径（由文件对话框选定） */
    midiPath: string | null;
    /** 选区起始帧（与 paste_reaper_clipboard 一致） */
    selectionStartFrame?: number;
    /** 选区最大帧数（与 paste_reaper_clipboard 一致） */
    selectionMaxFrames?: number;
    /** 导入完成后的回调 */
    onImported?: (result: { notes_imported: number; frames_touched: number }) => void;
    /** 导入模式：pitchEdit（默认，写入 pitch_edit）或 clip（创建 MIDI clip）或 replaceMidi（替换已有 MIDI clip 数据） */
    mode?: "pitchEdit" | "clip" | "replaceMidi";
    /** clip 模式下的确认回调 */
    onImportAsClip?: (result: {
        trackIndices: number[];
        notesCount: number;
        midiPath: string;
        fillGaps: boolean;
        multiTrackMerge?: boolean;
        noteBpmMode?: string;
        specifiedBpm?: number;
        importBpmAsProject?: boolean;
    }) => void;
    /** 多轨合并选项（仅 clip 模式下生效） */
    multiTrackMerge?: boolean;
    /** 多轨合并选项变更回调 */
    onMultiTrackMergeChange?: (v: boolean) => void;
    /** 导入位置模式：projectStart / playhead / selection */
    importPosition?: string;
    /** 导入位置变更回调（用于持久化） */
    onImportPositionChange?: (position: string) => void;
    /** selection 模式是否可用（有选区且当前为选择工具） */
    selectionAvailable?: boolean;
    /** 是否填补音符之间的空隙 */
    fillGaps?: boolean;
    /** 填补空隙选项变更回调（用于持久化） */
    onFillGapsChange?: (fillGaps: boolean) => void;
    /** 当前工程 BPM */
    projectBpm?: number;
    /** 是否将 MIDI BPM 导入为工程 BPM */
    importBpmAsProject?: boolean;
    /** 导入为工程 BPM 选项变更回调（用于持久化） */
    onImportBpmAsProjectChange?: (v: boolean) => void;
    /** 音符 BPM 模式："midi" | "project" | "specified" */
    noteBpmMode?: string;
    /** 音符 BPM 模式变更回调（用于持久化） */
    onNoteBpmModeChange?: (v: string) => void;
    /** 指定 BPM 数值 */
    specifiedBpm?: number;
    /** 指定 BPM 数值变更回调（用于持久化） */
    onSpecifiedBpmChange?: (v: number) => void;
}

/** MIDI note number → 音名 */
function noteToName(note: number): string {
    const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    const octave = Math.floor(note / 12) - 1;
    return `${names[note % 12]}${octave}`;
}

/**
 * MIDI 轨道选择弹窗
 *
 * 当 MIDI 文件包含多个有音符的轨道时，弹出此对话框让用户选择要导入的轨道。
 */
export const MidiTrackSelectDialog: React.FC<MidiTrackSelectDialogProps> = ({
    open,
    onOpenChange,
    midiPath,
    selectionStartFrame,
    selectionMaxFrames,
    onImported,
    mode = "pitchEdit",
    onImportAsClip,
    importPosition = "playhead",
    onImportPositionChange,
    selectionAvailable = false,
    fillGaps = false,
    onFillGapsChange,
    multiTrackMerge,
    onMultiTrackMergeChange,
    projectBpm,
    importBpmAsProject = false,
    onImportBpmAsProjectChange,
    noteBpmMode = "midi",
    onNoteBpmModeChange,
    specifiedBpm = 120,
    onSpecifiedBpmChange,
}) => {
    const { t } = useI18n();
    const tAny = t as (key: string) => string;

    const [tracks, setTracks] = useState<MidiTrackInfo[]>([]);
    const [loading, setLoading] = useState(false);
    const [importing, setImporting] = useState(false);
    const [error, setError] = useState<string | null>(null);
    // 多选轨道：存储被选中的轨道 index 数组
    const [selectedTracks, setSelectedTracks] = useState<number[]>([]);

    // 内部状态：用户通过 Browse / Clipboard 选择的路径
    const [localMidiPath, setLocalMidiPath] = useState<string | null>(null);
    const [initialBpm, setInitialBpm] = useState<number | null>(null);
    const [readingClipboard, setReadingClipboard] = useState(false);

    // 当前有效的 MIDI 路径（内部选择优先）
    const effectivePath = localMidiPath ?? midiPath;

    // 加载轨道列表的函数
    const loadTracks = useCallback(
        (path: string) => {
            console.info("[midi_import_ui] load_tracks:start", { midiPath: path });

            setLoading(true);
            setError(null);

            paramsApi
                .getMidiTracks(path)
                .then((res) => {
                    console.info("[midi_import_ui] load_tracks:response", res);
                    if (res.ok && res.tracks) {
                        setTracks(res.tracks);
                        setInitialBpm(res.initial_bpm ?? null);
                        // 默认全选
                        setSelectedTracks(res.tracks.map((t) => t.index));
                    } else {
                        setError(res.error ?? tAny("midi_import_failed"));
                        setTracks([]);
                        setInitialBpm(null);
                    }
                })
                .catch((err) => {
                    console.error("[midi_import_ui] load_tracks:error", err);
                    setError(tAny("midi_import_failed"));
                    setTracks([]);
                    setInitialBpm(null);
                })
                .finally(() => setLoading(false));
        },
        [tAny],
    );

    // 当弹窗打开时有 effectivePath，加载轨道列表
    useEffect(() => {
        if (!open || !effectivePath) {
            setTracks([]);
            setError(null);
            setSelectedTracks([]);
            setInitialBpm(null);
            return;
        }

        loadTracks(effectivePath);
    }, [open, effectivePath, loadTracks]);

    // 弹窗关闭时重置内部状态
    useEffect(() => {
        if (!open) {
            setLocalMidiPath(null);
            setInitialBpm(null);
            setReadingClipboard(false);
        }
    }, [open]);

    // Browse 按钮：打开原生文件对话框
    const handleBrowse = useCallback(async () => {
        try {
            const coreApi = (await import("../../services/api/core")).coreApi;
            const picked = await coreApi.openMidiDialog();
            if (!(picked as { ok?: boolean }).ok) return;
            if ((picked as { canceled?: boolean }).canceled || !(picked as { path?: string }).path)
                return;
            setLocalMidiPath((picked as { path: string }).path);
            setError(null);
        } catch {
            // 静默忽略
        }
    }, []);

    // Read from Clipboard 按钮
    const handleReadClipboard = useCallback(async () => {
        setReadingClipboard(true);
        setError(null);
        try {
            const res = await paramsApi.readMidiClipboardAsTemp();
            if (res.ok && res.temp_path) {
                setLocalMidiPath(res.temp_path);
                // useEffect 会在 effectivePath 变化时自动加载轨道
            } else {
                const errorKey = res.error ?? "midi_clipboard_read_failed";
                setError(tAny(errorKey));
            }
        } catch {
            setError(tAny("midi_clipboard_read_failed"));
        } finally {
            setReadingClipboard(false);
        }
    }, [tAny]);

    const handleImport = useCallback(async () => {
        if (!effectivePath || selectedTracks.length === 0) return;

        setImporting(true);
        try {
            const trackIndices = selectedTracks;

            if (mode === "clip" || mode === "replaceMidi") {
                const notesCount = tracks
                    .filter((t) => selectedTracks.includes(t.index))
                    .reduce((sum, t) => sum + t.note_count, 0);
                onImportAsClip?.({
                    trackIndices,
                    notesCount,
                    midiPath: effectivePath,
                    fillGaps,
                    multiTrackMerge,
                    noteBpmMode,
                    specifiedBpm: noteBpmMode === "specified" ? specifiedBpm : undefined,
                    importBpmAsProject: importBpmAsProject || undefined,
                });
                onOpenChange(false);
                return;
            }

            // 根据导入位置模式计算帧偏移
            let effectivePosition = importPosition;
            if (effectivePosition === "selection") {
                if (selectionStartFrame == null || !selectionAvailable) {
                    effectivePosition = "playhead"; // 回退
                }
            }
            const startFrame =
                effectivePosition === "projectStart"
                    ? 0
                    : effectivePosition === "selection"
                      ? selectionStartFrame
                      : undefined;
            const maxFrames = effectivePosition === "selection" ? selectionMaxFrames : undefined;

            console.info("[midi_import_ui] import:start", {
                midiPath: effectivePath,
                trackIndices,
                importPosition,
                effectivePosition,
                startFrame,
                maxFrames,
                noteBpmMode,
                specifiedBpm,
                importBpmAsProject,
            });
            const res = await paramsApi.importMidiToPitch(
                effectivePath,
                trackIndices,
                startFrame,
                maxFrames,
                fillGaps || undefined,
                noteBpmMode,
                noteBpmMode === "specified" ? specifiedBpm : undefined,
                importBpmAsProject || undefined,
            );
            console.info("[midi_import_ui] import:response", res);
            if (res.ok) {
                onImported?.({
                    notes_imported: res.notes_imported ?? 0,
                    frames_touched: res.frames_touched ?? 0,
                });
                onOpenChange(false);
            } else {
                const errKey = res.error ?? "midi_import_failed";
                // 尝试翻译已知的错误键
                const knownErrors: Record<string, string> = {
                    file_not_found: tAny("midi_file_not_found"),
                    no_notes_in_track: tAny("midi_no_notes"),
                    no_frames_touched: tAny("midi_no_frames_touched"),
                    no_pitch_line_selected: tAny("vs_paste_no_pitch_line"),
                    pitch_requires_compose: tAny("pitch_requires_compose"),
                    pitch_requires_algo: tAny("pitch_requires_algo"),
                };
                setError(knownErrors[errKey] ?? errKey);
            }
        } catch (err) {
            console.error("[midi_import_ui] import:error", err);
            setError(tAny("midi_import_failed"));
        } finally {
            setImporting(false);
        }
    }, [
        effectivePath,
        selectionStartFrame,
        selectionMaxFrames,
        selectedTracks,
        onImported,
        onImportAsClip,
        onOpenChange,
        tAny,
        mode,
        tracks,
        importPosition,
        selectionAvailable,
        fillGaps,
        multiTrackMerge,
        noteBpmMode,
        specifiedBpm,
        importBpmAsProject,
    ]);

    return (
        <Dialog.Root open={open} onOpenChange={onOpenChange}>
            <Dialog.Content maxWidth="520px">
                <Dialog.Title>
                    {mode === "replaceMidi"
                        ? tAny("midi_replace_title")
                        : mode === "clip"
                          ? tAny("midi_import_clip_title")
                          : tAny("midi_import_title")}
                </Dialog.Title>
                <Dialog.Description size="2" color="gray">
                    {mode === "replaceMidi"
                        ? tAny("midi_replace_desc")
                        : mode === "clip"
                          ? tAny("midi_import_clip_desc")
                          : tAny("midi_import_desc")}
                </Dialog.Description>

                {/* ── 文件选择区域（始终显示） ── */}
                <Flex direction="column" gap="1" mt="3">
                    <Text size="1" weight="medium">
                        {tAny("midi_file_path")}
                    </Text>
                    <Flex gap="2" align="center">
                        <input
                            type="text"
                            className="flex-1 px-2 py-1 text-xs rounded border border-qt-border bg-qt-base text-qt-text"
                            readOnly
                            value={effectivePath ? effectivePath : tAny("midi_no_file_selected")}
                            style={{
                                color: effectivePath ? undefined : "#888",
                                cursor: "default",
                                minWidth: 0,
                            }}
                        />
                        <Button variant="soft" size="1" onClick={handleBrowse} disabled={importing}>
                            {tAny("midi_browse")}
                        </Button>
                        <Button
                            variant="soft"
                            size="1"
                            onClick={handleReadClipboard}
                            disabled={importing || readingClipboard}
                        >
                            {readingClipboard
                                ? tAny("midi_importing")
                                : tAny("midi_read_clipboard")}
                        </Button>
                    </Flex>
                </Flex>

                {loading && (
                    <Flex justify="center" py="4">
                        <Text size="2" color="gray">
                            {tAny("loading")}
                        </Text>
                    </Flex>
                )}

                {error && (
                    <Flex py="2">
                        <Text size="2" color="red">
                            {error}
                        </Text>
                    </Flex>
                )}

                {effectivePath && !loading && !error && tracks.length === 0 && (
                    <Flex py="4" justify="center">
                        <Text size="2" color="gray">
                            {tAny("midi_no_tracks")}
                        </Text>
                    </Flex>
                )}

                {!loading && tracks.length > 0 && (
                    <>
                        {/* 全选 / 全不选 快捷按钮 */}
                        <Flex gap="2" mt="3">
                            <Button
                                variant="soft"
                                color="gray"
                                size="1"
                                onClick={() => setSelectedTracks(tracks.map((t) => t.index))}
                            >
                                {tAny("midi_select_all")}
                            </Button>
                            <Button
                                variant="soft"
                                color="gray"
                                size="1"
                                onClick={() => setSelectedTracks([])}
                            >
                                {tAny("midi_deselect_all")}
                            </Button>
                            {initialBpm != null && (
                                <Text size="1" color="gray" className="ml-auto self-center">
                                    {tAny("midi_midi_bpm_label").replace(
                                        "{bpm}",
                                        initialBpm.toFixed(2),
                                    )}
                                </Text>
                            )}
                        </Flex>

                        <ScrollArea
                            style={{ maxHeight: 200 }}
                            className="mt-2 rounded border border-qt-border"
                        >
                            <Flex direction="column" gap="0">
                                {/* 各个轨道选项（多选） */}
                                {tracks.map((track) => (
                                    <label
                                        key={track.index}
                                        className="flex items-center gap-2 px-3 py-2 hover:bg-qt-highlight cursor-pointer border-b border-qt-border last:border-b-0"
                                    >
                                        <input
                                            type="checkbox"
                                            className="w-4 h-4"
                                            checked={selectedTracks.includes(track.index)}
                                            onChange={(e) => {
                                                if (e.target.checked) {
                                                    setSelectedTracks([
                                                        ...selectedTracks,
                                                        track.index,
                                                    ]);
                                                } else {
                                                    setSelectedTracks(
                                                        selectedTracks.filter(
                                                            (i) => i !== track.index,
                                                        ),
                                                    );
                                                }
                                            }}
                                        />
                                        <Flex direction="column" gap="0" className="flex-1 min-w-0">
                                            <Text size="2" weight="medium" className="truncate">
                                                {track.name || `Track ${track.index + 1}`}
                                            </Text>
                                            <Flex gap="2">
                                                <Text size="1" color="gray">
                                                    {tAny("midi_track_notes").replace(
                                                        "{count}",
                                                        String(track.note_count),
                                                    )}
                                                </Text>
                                                <Text size="1" color="gray">
                                                    {tAny("midi_track_range")
                                                        .replace(
                                                            "{min}",
                                                            noteToName(track.min_note),
                                                        )
                                                        .replace(
                                                            "{max}",
                                                            noteToName(track.max_note),
                                                        )}
                                                </Text>
                                            </Flex>
                                        </Flex>
                                    </label>
                                ))}
                            </Flex>
                        </ScrollArea>

                        {/* ── BPM 选项（在多轨合并和填补空隙上方） ── */}
                        {/* 将 MIDI BPM 导入为工程 BPM */}
                        <label className="flex items-center gap-2 mt-3 cursor-pointer">
                            <input
                                type="checkbox"
                                checked={importBpmAsProject}
                                onChange={(e) => onImportBpmAsProjectChange?.(e.target.checked)}
                                className="w-4 h-4"
                            />
                            <Text size="1">{tAny("midi_import_bpm_as_project")}</Text>
                        </label>

                        {/* 音符 BPM 设置 */}
                        <Flex direction="column" gap="1" mt="2">
                            <Text size="2" weight="medium">
                                {tAny("midi_note_bpm")}
                            </Text>
                            <RadioGroup.Root
                                value={noteBpmMode}
                                onValueChange={(v) => onNoteBpmModeChange?.(v)}
                            >
                                <Flex direction="column" gap="1">
                                    <label className="flex items-center gap-1 cursor-pointer">
                                        <RadioGroup.Item value="midi" />
                                        <Text size="1">{tAny("midi_note_bpm_midi")}</Text>
                                    </label>
                                    <label className="flex items-center gap-1 cursor-pointer">
                                        <RadioGroup.Item value="project" />
                                        <Text size="1">
                                            {tAny("midi_note_bpm_project")}
                                            {projectBpm != null
                                                ? ` (${projectBpm.toFixed(2)} BPM)`
                                                : ""}
                                        </Text>
                                    </label>
                                    <label className="flex items-center gap-1 cursor-pointer">
                                        <RadioGroup.Item value="specified" />
                                        <Text size="1">{tAny("midi_note_bpm_specified")}</Text>
                                    </label>
                                    {noteBpmMode === "specified" && (
                                        <Flex gap="2" align="center" className="ml-5 mt-1">
                                            <input
                                                type="number"
                                                className="w-20 px-2 py-1 text-xs rounded border border-qt-border bg-qt-base text-qt-text"
                                                value={specifiedBpm}
                                                min={1}
                                                max={999}
                                                step={1}
                                                onKeyDown={(e) => {
                                                    if (e.key === "ArrowUp") {
                                                        e.preventDefault();
                                                        const next = specifiedBpm + 1;
                                                        if (next <= 999)
                                                            onSpecifiedBpmChange?.(next);
                                                    } else if (e.key === "ArrowDown") {
                                                        e.preventDefault();
                                                        const next = specifiedBpm - 1;
                                                        if (next >= 1) onSpecifiedBpmChange?.(next);
                                                    }
                                                }}
                                                onWheel={(e) => {
                                                    e.preventDefault();
                                                    const dir = e.deltaY < 0 ? 1 : -1;
                                                    const next = specifiedBpm + dir;
                                                    if (next >= 1 && next <= 999)
                                                        onSpecifiedBpmChange?.(next);
                                                }}
                                                onChange={(e) => {
                                                    const v = parseFloat(e.target.value);
                                                    if (!isNaN(v) && v > 0) {
                                                        onSpecifiedBpmChange?.(v);
                                                    }
                                                }}
                                            />
                                            <Text size="1" color="gray">
                                                {tAny("midi_specified_bpm_placeholder")}
                                            </Text>
                                        </Flex>
                                    )}
                                </Flex>
                            </RadioGroup.Root>
                        </Flex>
                    </>
                )}

                {effectivePath && (
                    <>
                        {/* 导入位置选项（仅在 pitchEdit 模式下显示） */}
                        {mode === "pitchEdit" && (
                            <Flex direction="column" gap="1" mt="3">
                                <Text size="2" weight="medium">
                                    {tAny("midi_import_position")}
                                </Text>
                                <RadioGroup.Root
                                    value={
                                        importPosition === "selection" && !selectionAvailable
                                            ? "playhead"
                                            : importPosition
                                    }
                                    onValueChange={(v) => onImportPositionChange?.(v)}
                                >
                                    <Flex gap="3">
                                        <label className="flex items-center gap-1 cursor-pointer">
                                            <RadioGroup.Item value="projectStart" />
                                            <Text size="1">
                                                {tAny("midi_import_position_start")}
                                            </Text>
                                        </label>
                                        <label className="flex items-center gap-1 cursor-pointer">
                                            <RadioGroup.Item value="playhead" />
                                            <Text size="1">
                                                {tAny("midi_import_position_playhead")}
                                            </Text>
                                        </label>
                                        <label className="flex items-center gap-1 cursor-pointer">
                                            <RadioGroup.Item
                                                value="selection"
                                                disabled={!selectionAvailable}
                                            />
                                            <Text
                                                size="1"
                                                color={selectionAvailable ? undefined : "gray"}
                                            >
                                                {tAny("midi_import_position_selection")}
                                            </Text>
                                        </label>
                                    </Flex>
                                </RadioGroup.Root>
                            </Flex>
                        )}

                        {/* 多轨合并选项（仅在 clip / replaceMidi 模式下显示） */}
                        {(mode === "clip" || mode === "replaceMidi") && (
                            <label className="flex items-center gap-2 mt-3 cursor-pointer">
                                <input
                                    type="checkbox"
                                    checked={multiTrackMerge ?? true}
                                    onChange={(e) => onMultiTrackMergeChange?.(e.target.checked)}
                                    className="w-4 h-4"
                                />
                                <Text size="1">{tAny("midi_multi_track_merge")}</Text>
                            </label>
                        )}

                        {/* 填补空隙选项 */}
                        <label className="flex items-center gap-2 mt-3 cursor-pointer">
                            <input
                                type="checkbox"
                                checked={fillGaps}
                                onChange={(e) => onFillGapsChange?.(e.target.checked)}
                                className="w-4 h-4"
                            />
                            <Text size="1">{tAny("midi_fill_gaps")}</Text>
                        </label>

                        <Flex justify="end" gap="2" mt="4">
                            <Button
                                variant="soft"
                                color="gray"
                                onClick={() => onOpenChange(false)}
                                disabled={importing}
                            >
                                {tAny("kb_close")}
                            </Button>
                            <Button
                                onClick={handleImport}
                                disabled={
                                    importing ||
                                    loading ||
                                    tracks.length === 0 ||
                                    selectedTracks.length === 0 ||
                                    !!error
                                }
                            >
                                {importing
                                    ? tAny("midi_importing")
                                    : mode === "replaceMidi"
                                      ? tAny("midi_replace_button")
                                      : mode === "clip"
                                        ? tAny("midi_create_clip")
                                        : tAny("midi_import")}
                            </Button>
                        </Flex>
                    </>
                )}
            </Dialog.Content>
        </Dialog.Root>
    );
};
