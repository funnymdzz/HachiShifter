import React from "react";
import { Button, Dialog, Flex, ScrollArea, Text, TextField } from "@radix-ui/themes";
import { useI18n } from "../../i18n/I18nProvider";
import { webApi } from "../../services/webviewApi";
import { fileBrowserApi } from "../../services/api/fileBrowser";
import type {
    ClipSampleAnnotationsResult,
    SampleRegionAnnotation,
} from "../../services/api/timeline";

type NumericField = Exclude<keyof SampleRegionAnnotation, "name">;

function errorText(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

export const SampleAnnotationDialog: React.FC<{
    open: boolean;
    clipId: string | null;
    onOpenChange: (open: boolean) => void;
}> = ({ open, clipId, onOpenChange }) => {
    const { t } = useI18n();
    const [rows, setRows] = React.useState<SampleRegionAnnotation[]>([]);
    const [activeIndex, setActiveIndex] = React.useState(0);
    const [sidecarPath, setSidecarPath] = React.useState("");
    const [noteCount, setNoteCount] = React.useState(0);
    const [busy, setBusy] = React.useState(false);
    const [status, setStatus] = React.useState("");

    const applyPayload = React.useCallback((payload: ClipSampleAnnotationsResult) => {
        if (!payload.ok) throw new Error(payload.error);
        setRows(payload.annotations);
        setActiveIndex(
            Math.min(payload.active_annotation_index, payload.annotations.length - 1),
        );
        setSidecarPath(payload.sidecar_path);
        setNoteCount(payload.pitch_notes.length);
    }, []);

    const load = React.useCallback(async () => {
        if (!clipId) return;
        setBusy(true);
        setStatus(t("sample_timing_analyzing"));
        try {
            applyPayload(await webApi.getClipSampleAnnotations(clipId));
            setStatus("");
        } catch (error) {
            setStatus(errorText(error));
        } finally {
            setBusy(false);
        }
    }, [applyPayload, clipId, t]);

    React.useEffect(() => {
        if (open) void load();
    }, [load, open]);

    const updateRow = React.useCallback(
        (index: number, patch: Partial<SampleRegionAnnotation>) => {
            setRows((current) =>
                current.map((row, rowIndex) =>
                    rowIndex === index ? { ...row, ...patch } : row,
                ),
            );
        },
        [],
    );

    const updateNumber = (index: number, field: NumericField, raw: string) => {
        const value = Number(raw);
        if (Number.isFinite(value)) updateRow(index, { [field]: value });
    };

    const selected = rows[activeIndex] ?? null;
    const rangeMax = Math.max(
        0.1,
        ...rows.map((row) => row.region_end_sec),
        selected?.region_end_sec ?? 0,
    );
    const fixedEnd = selected
        ? selected.region_start_sec + selected.fixed_duration_sec
        : 0;

    const moveBoundary = (
        boundary: "start" | "fixed" | "alignment" | "end",
        value: number,
    ) => {
        if (!selected) return;
        const minGap = 0.001;
        if (boundary === "start") {
            const start = Math.min(value, selected.region_end_sec - minGap);
            updateRow(activeIndex, {
                region_start_sec: start,
                fixed_duration_sec: Math.max(0, fixedEnd - start),
                note_alignment_sec: Math.max(start, selected.note_alignment_sec),
            });
        } else if (boundary === "fixed") {
            updateRow(activeIndex, {
                fixed_duration_sec:
                    Math.min(selected.region_end_sec, Math.max(selected.region_start_sec, value)) -
                    selected.region_start_sec,
            });
        } else if (boundary === "alignment") {
            updateRow(activeIndex, {
                note_alignment_sec: Math.min(
                    selected.region_end_sec,
                    Math.max(selected.region_start_sec, value),
                ),
            });
        } else {
            const end = Math.max(selected.region_start_sec + minGap, value);
            updateRow(activeIndex, {
                region_end_sec: end,
                fixed_duration_sec: Math.min(
                    selected.fixed_duration_sec,
                    end - selected.region_start_sec,
                ),
                note_alignment_sec: Math.min(end, selected.note_alignment_sec),
            });
        }
    };

    const save = async () => {
        if (!clipId) return;
        setBusy(true);
        setStatus("");
        try {
            const result = await webApi.saveClipSampleAnnotations(
                clipId,
                rows,
                activeIndex,
            );
            if (!result.ok) throw new Error(result.error);
            setRows(result.annotations);
            setActiveIndex(result.active_annotation_index);
            setSidecarPath(result.sidecar_path);
            window.dispatchEvent(
                new CustomEvent("hachi-sample-annotations-changed", {
                    detail: { clipId },
                }),
            );
            setStatus(t("sample_timing_saved"));
        } catch (error) {
            setStatus(errorText(error));
        } finally {
            setBusy(false);
        }
    };

    const redetect = async () => {
        if (!clipId) return;
        setBusy(true);
        setStatus(t("sample_timing_analyzing"));
        try {
            applyPayload(await webApi.redetectClipSampleAnnotations(clipId));
            setStatus(t("sample_timing_detected"));
        } catch (error) {
            setStatus(errorText(error));
        } finally {
            setBusy(false);
        }
    };

    const importOtoPath = async (path: string) => {
        if (!clipId) return;
        setBusy(true);
        setStatus(t("sample_timing_converting_oto"));
        try {
            const result = await webApi.convertOtoAndRefreshClip(clipId, path);
            if (!result.ok) throw new Error(result.error ?? "oto conversion failed");
            if (result.clip) applyPayload(result.clip);
            const converted = result.conversion?.converted_samples.length ?? 0;
            const warnings = result.conversion?.warnings.length ?? 0;
            setStatus(
                t("sample_timing_oto_result")
                    .replace("{n}", String(converted))
                    .replace("{w}", String(warnings)),
            );
            window.dispatchEvent(
                new CustomEvent("hachi-sample-annotations-changed", {
                    detail: { clipId },
                }),
            );
        } catch (error) {
            setStatus(errorText(error));
        } finally {
            setBusy(false);
        }
    };

    const pickOto = async () => {
        const result = await webApi.openOtoDialog();
        if (result.ok && !result.canceled && result.path) await importOtoPath(result.path);
    };

    const pickVoicebank = async () => {
        const result = await fileBrowserApi.pickDirectory();
        if (result.ok && !result.canceled && result.path) await importOtoPath(result.path);
    };

    return (
        <Dialog.Root open={open} onOpenChange={onOpenChange}>
            <Dialog.Content
                style={{ maxWidth: 900 }}
                onKeyDown={(event) => event.stopPropagation()}
            >
                <Dialog.Title>{t("sample_timing_title")}</Dialog.Title>
                <Dialog.Description>
                    {t("sample_timing_description")}
                </Dialog.Description>

                <Flex gap="2" mt="3" wrap="wrap">
                    <Button variant="soft" disabled={busy} onClick={() => void redetect()}>
                        {t("sample_timing_redetect")}
                    </Button>
                    <Button variant="soft" disabled={busy} onClick={() => void pickOto()}>
                        {t("sample_timing_import_oto")}
                    </Button>
                    <Button variant="soft" disabled={busy} onClick={() => void pickVoicebank()}>
                        {t("sample_timing_import_voicebank")}
                    </Button>
                    <Button
                        variant="soft"
                        disabled={busy}
                        onClick={() => {
                            const previous = rows.at(-1);
                            const start = previous?.region_end_sec ?? 0;
                            setRows((current) => [
                                ...current,
                                {
                                    name: `${t("sample_timing_region")} ${current.length + 1}`,
                                    region_start_sec: start,
                                    region_end_sec: start + 0.5,
                                    note_alignment_sec: start,
                                    fixed_duration_sec: 0,
                                    relative_pitch_cents: 0,
                                },
                            ]);
                            setActiveIndex(rows.length);
                        }}
                    >
                        {t("sample_timing_add_region")}
                    </Button>
                </Flex>

                {selected && (
                    <div className="mt-4 rounded border border-qt-border bg-black/20 p-3">
                        <Flex justify="between" mb="2">
                            <Text size="2" weight="bold">
                                {selected.name}
                            </Text>
                            <Text size="1" color="gray">
                                {t("sample_timing_note_count").replace("{n}", String(noteCount))}
                            </Text>
                        </Flex>
                        <div className="relative h-9 rounded bg-black/40 overflow-hidden">
                            <div
                                className="absolute top-0 bottom-0 bg-amber-400/55"
                                style={{
                                    left: `${(selected.region_start_sec / rangeMax) * 100}%`,
                                    width: `${(selected.fixed_duration_sec / rangeMax) * 100}%`,
                                }}
                                title={t("sample_timing_fixed_segment")}
                            />
                            <div
                                className="absolute top-0 bottom-0 bg-cyan-400/45"
                                style={{
                                    left: `${(fixedEnd / rangeMax) * 100}%`,
                                    width: `${
                                        ((selected.region_end_sec - fixedEnd) / rangeMax) * 100
                                    }%`,
                                }}
                                title={t("sample_timing_stretch_segment")}
                            />
                            <div
                                className="absolute top-0 bottom-0 w-0.5 bg-pink-400"
                                style={{
                                    left: `${(selected.note_alignment_sec / rangeMax) * 100}%`,
                                }}
                                title={t("sample_timing_alignment")}
                            />
                        </div>
                        {(
                            [
                                ["start", selected.region_start_sec, "sample_timing_start"],
                                ["fixed", fixedEnd, "sample_timing_fixed_end"],
                                [
                                    "alignment",
                                    selected.note_alignment_sec,
                                    "sample_timing_alignment",
                                ],
                                ["end", selected.region_end_sec, "sample_timing_end"],
                            ] as const
                        ).map(([boundary, value, label]) => (
                            <label key={boundary} className="mt-2 grid grid-cols-[120px_1fr_75px] gap-2">
                                <Text size="1">{t(label)}</Text>
                                <input
                                    type="range"
                                    min={0}
                                    max={rangeMax}
                                    step={0.001}
                                    value={value}
                                    onChange={(event) =>
                                        moveBoundary(boundary, Number(event.target.value))
                                    }
                                />
                                <Text size="1" align="right">
                                    {value.toFixed(3)} s
                                </Text>
                            </label>
                        ))}
                    </div>
                )}

                <ScrollArea type="auto" scrollbars="both" style={{ maxHeight: 300 }}>
                    <div className="mt-4 min-w-[780px]">
                        <div className="grid grid-cols-[28px_1.4fr_repeat(4,1fr)_56px] gap-2 px-1 pb-1 text-[11px] text-qt-text/60">
                            <span />
                            <span>{t("sample_timing_name")}</span>
                            <span>{t("sample_timing_start")}</span>
                            <span>{t("sample_timing_end")}</span>
                            <span>{t("sample_timing_alignment")}</span>
                            <span>{t("sample_timing_fixed_duration")}</span>
                            <span />
                        </div>
                        {rows.map((row, index) => (
                            <div
                                key={index}
                                className={`grid grid-cols-[28px_1.4fr_repeat(4,1fr)_56px] gap-2 p-1 rounded ${
                                    index === activeIndex ? "bg-qt-highlight/15" : ""
                                }`}
                            >
                                <input
                                    type="radio"
                                    checked={index === activeIndex}
                                    onChange={() => setActiveIndex(index)}
                                    title={t("sample_timing_active_region")}
                                />
                                <TextField.Root
                                    value={row.name}
                                    onChange={(event) =>
                                        updateRow(index, { name: event.target.value })
                                    }
                                />
                                {(
                                    [
                                        "region_start_sec",
                                        "region_end_sec",
                                        "note_alignment_sec",
                                        "fixed_duration_sec",
                                    ] as NumericField[]
                                ).map((field) => (
                                    <TextField.Root
                                        key={field}
                                        type="number"
                                        step="0.001"
                                        min="0"
                                        value={row[field]}
                                        onChange={(event) =>
                                            updateNumber(index, field, event.target.value)
                                        }
                                    />
                                ))}
                                <Button
                                    size="1"
                                    color="red"
                                    variant="soft"
                                    disabled={rows.length <= 1}
                                    onClick={() => {
                                        setRows((current) =>
                                            current.filter((_, rowIndex) => rowIndex !== index),
                                        );
                                        setActiveIndex((current) =>
                                            Math.max(0, Math.min(current, rows.length - 2)),
                                        );
                                    }}
                                >
                                    {t("ctx_delete")}
                                </Button>
                            </div>
                        ))}
                    </div>
                </ScrollArea>

                <Text as="div" size="1" color="gray" mt="3" className="break-all">
                    CSV: {sidecarPath || "—"}
                </Text>
                {status && (
                    <Text as="div" size="2" mt="2" color={status.includes("failed") ? "red" : undefined}>
                        {status}
                    </Text>
                )}
                <Flex justify="end" gap="2" mt="4">
                    <Dialog.Close>
                        <Button variant="soft" color="gray">
                            {t("cancel")}
                        </Button>
                    </Dialog.Close>
                    <Button disabled={busy || rows.length === 0} onClick={() => void save()}>
                        {t("menu_save_project")}
                    </Button>
                </Flex>
            </Dialog.Content>
        </Dialog.Root>
    );
};
