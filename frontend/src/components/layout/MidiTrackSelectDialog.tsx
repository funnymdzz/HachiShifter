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
    /** 导入模式：pitchEdit（默认，写入 pitch_edit）或 clip（创建 MIDI clip） */
    mode?: "pitchEdit" | "clip";
    /** clip 模式下的确认回调 */
    onImportAsClip?: (result: {
        trackIndex?: number;
        notesCount: number;
        midiPath: string;
        fillGaps: boolean;
    }) => void;
    /** 导入位置模���：projectStart / playhead / selection */
    importPosition?: string;
    /** 导入位置变更回调（用于持久化） */
    onImportPositionChange?: (position: string) => void;
    /** selection 模式是否可用（有选区且当前为选择工具） */
    selectionAvailable?: boolean;
    /** 是否填补音符之间的空隙 */
    fillGaps?: boolean;
    /** 填补空隙选项变更回调（用于持久化） */
    onFillGapsChange?: (fillGaps: boolean) => void;
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
}) => {
    const { t } = useI18n();
    const tAny = t as (key: string) => string;

    const [tracks, setTracks] = useState<MidiTrackInfo[]>([]);
    const [loading, setLoading] = useState(false);
    const [importing, setImporting] = useState(false);
    const [error, setError] = useState<string | null>(null);
    // "all" 表示合并所有轨道，否则为轨道索引字符串
    const [selectedTrack, setSelectedTrack] = useState<string>("all");

    // 当弹窗打开且有 midiPath 时，加载轨道列表
    useEffect(() => {
        if (!open || !midiPath) {
            setTracks([]);
            setError(null);
            setSelectedTrack("all");
            return;
        }

        console.info("[midi_import_ui] load_tracks:start", {
            midiPath,
        });

        setLoading(true);
        setError(null);

        paramsApi
            .getMidiTracks(midiPath)
            .then((res) => {
                console.info("[midi_import_ui] load_tracks:response", res);
                if (res.ok && res.tracks) {
                    setTracks(res.tracks);
                    if (res.tracks.length === 1) {
                        setSelectedTrack(String(res.tracks[0].index));
                    } else {
                        setSelectedTrack("all");
                    }
                } else {
                    setError(res.error ?? tAny("midi_import_failed"));
                    setTracks([]);
                }
            })
            .catch((err) => {
                console.error("[midi_import_ui] load_tracks:error", err);
                setError(tAny("midi_import_failed"));
                setTracks([]);
            })
            .finally(() => setLoading(false));
    }, [open, midiPath]);

    const handleImport = useCallback(async () => {
        if (!midiPath) return;

        setImporting(true);
        try {
            const trackIndex = selectedTrack === "all" ? undefined : parseInt(selectedTrack, 10);

            if (mode === "clip") {
                const notesCount =
                    selectedTrack === "all"
                        ? tracks.reduce((sum, t) => sum + t.note_count, 0)
                        : (tracks.find((t) => t.index === trackIndex)?.note_count ?? 0);
                onImportAsClip?.({ trackIndex, notesCount, midiPath, fillGaps });
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
                midiPath,
                trackIndex,
                importPosition,
                effectivePosition,
                startFrame,
                maxFrames,
                selectedTrack,
            });
            const res = await paramsApi.importMidiToPitch(
                midiPath,
                trackIndex,
                startFrame,
                maxFrames,
                fillGaps || undefined,
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
        midiPath,
        selectionStartFrame,
        selectionMaxFrames,
        selectedTrack,
        onImported,
        onImportAsClip,
        onOpenChange,
        tAny,
        mode,
        tracks,
        importPosition,
        selectionAvailable,
        fillGaps,
    ]);

    return (
        <Dialog.Root open={open} onOpenChange={onOpenChange}>
            <Dialog.Content maxWidth="480px">
                <Dialog.Title>
                    {mode === "clip" ? tAny("midi_import_clip_title") : tAny("midi_import_title")}
                </Dialog.Title>
                <Dialog.Description size="2" color="gray">
                    {mode === "clip" ? tAny("midi_import_clip_desc") : tAny("midi_import_desc")}
                </Dialog.Description>

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

                {!loading && !error && tracks.length === 0 && (
                    <Flex py="4" justify="center">
                        <Text size="2" color="gray">
                            {tAny("midi_no_tracks")}
                        </Text>
                    </Flex>
                )}

                {!loading && tracks.length > 0 && (
                    <ScrollArea
                        style={{ maxHeight: 300 }}
                        className="mt-3 rounded border border-qt-border"
                    >
                        <RadioGroup.Root value={selectedTrack} onValueChange={setSelectedTrack}>
                            <Flex direction="column" gap="0">
                                {/* 合并所有轨道选项（仅多轨时显示） */}
                                {tracks.length > 1 && (
                                    <label className="flex items-center gap-2 px-3 py-2 hover:bg-qt-highlight cursor-pointer border-b border-qt-border">
                                        <RadioGroup.Item value="all" />
                                        <Flex direction="column" gap="0">
                                            <Text size="2" weight="medium">
                                                {tAny("midi_all_tracks")}
                                            </Text>
                                            <Text size="1" color="gray">
                                                {tAny("midi_track_notes").replace(
                                                    "{count}",
                                                    String(
                                                        tracks.reduce(
                                                            (sum, t) => sum + t.note_count,
                                                            0,
                                                        ),
                                                    ),
                                                )}
                                            </Text>
                                        </Flex>
                                    </label>
                                )}

                                {/* 各个轨道选项 */}
                                {tracks.map((track) => (
                                    <label
                                        key={track.index}
                                        className="flex items-center gap-2 px-3 py-2 hover:bg-qt-highlight cursor-pointer border-b border-qt-border last:border-b-0"
                                    >
                                        <RadioGroup.Item value={String(track.index)} />
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
                        </RadioGroup.Root>
                    </ScrollArea>
                )}

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
                                    <Text size="1">{tAny("midi_import_position_start")}</Text>
                                </label>
                                <label className="flex items-center gap-1 cursor-pointer">
                                    <RadioGroup.Item value="playhead" />
                                    <Text size="1">{tAny("midi_import_position_playhead")}</Text>
                                </label>
                                <label className="flex items-center gap-1 cursor-pointer">
                                    <RadioGroup.Item
                                        value="selection"
                                        disabled={!selectionAvailable}
                                    />
                                    <Text size="1" color={selectionAvailable ? undefined : "gray"}>
                                        {tAny("midi_import_position_selection")}
                                    </Text>
                                </label>
                            </Flex>
                        </RadioGroup.Root>
                    </Flex>
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
                        disabled={importing || loading || tracks.length === 0 || !!error}
                    >
                        {importing
                            ? tAny("midi_importing")
                            : mode === "clip"
                              ? tAny("midi_create_clip")
                              : tAny("midi_import")}
                    </Button>
                </Flex>
            </Dialog.Content>
        </Dialog.Root>
    );
};
