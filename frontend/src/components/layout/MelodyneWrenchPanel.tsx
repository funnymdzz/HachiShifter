import React from "react";
import { Button, Flex, Select, Text, TextField } from "@radix-ui/themes";
import type { StretchAlgorithmOption } from "../../services/api/settings";
import type {
    ClipSampleAnnotationsResult,
    SampleRegionAnnotation,
} from "../../services/api/timeline";
import { useI18n } from "../../i18n/I18nProvider";

type Analysis = Extract<ClipSampleAnnotationsResult, { ok: true }>;
type NumericField = Exclude<keyof SampleRegionAnnotation, "name">;

export const MelodyneWrenchPanel: React.FC<{
    analysis: Analysis | null;
    activeNoteIndex: number | null;
    stretchAlgorithm: StretchAlgorithmOption;
    showHifiganMelHop?: boolean;
    busy?: boolean;
    onSelectNote: (index: number) => void;
    onSelectRegion: (index: number) => void;
    onChangeRegions: (rows: SampleRegionAnnotation[]) => void;
    onSave: () => void;
    onRedetect: () => void;
    onConnect: () => void;
    onDisconnect: () => void;
    onStretchAlgorithmChange: (value: StretchAlgorithmOption) => void;
}> = ({
    analysis,
    activeNoteIndex,
    stretchAlgorithm,
    showHifiganMelHop = false,
    busy = false,
    onSelectNote,
    onSelectRegion,
    onChangeRegions,
    onSave,
    onRedetect,
    onConnect,
    onDisconnect,
    onStretchAlgorithmChange,
}) => {
    const { t } = useI18n();
    const tAny = t as (key: string) => string;
    const [lane, setLane] = React.useState<"notes" | "segments" | "events">("notes");

    const updateRegion = (
        index: number,
        field: keyof SampleRegionAnnotation,
        raw: string,
    ) => {
        if (!analysis) return;
        const value = field === "name" ? raw : Number(raw);
        if (field !== "name" && !Number.isFinite(value)) return;
        onChangeRegions(
            analysis.annotations.map((row, rowIndex) =>
                rowIndex === index ? { ...row, [field]: value } : row,
            ),
        );
    };

    return (
        <div className="border-b border-qt-border bg-qt-base px-2 py-2 text-qt-text">
            <Flex align="center" gap="2" wrap="wrap">
                <Text size="1" weight="bold">
                    {tAny("melodyne_wrench_title")}
                </Text>
                <Button
                    size="1"
                    variant={lane === "notes" ? "solid" : "soft"}
                    color={lane === "notes" ? "blue" : "gray"}
                    onClick={() => setLane("notes")}
                >
                    {tAny("melodyne_note_lane")}
                </Button>
                <Button
                    size="1"
                    variant={lane === "segments" ? "solid" : "soft"}
                    color={lane === "segments" ? "blue" : "gray"}
                    onClick={() => setLane("segments")}
                >
                    {tAny("melodyne_segment_lane")}
                </Button>
                <Button
                    size="1"
                    variant={lane === "events" ? "solid" : "soft"}
                    color={lane === "events" ? "blue" : "gray"}
                    onClick={() => setLane("events")}
                >
                    {tAny("melodyne_event_lane")}
                </Button>
                <div className="h-5 w-px bg-qt-border" />
                <Text size="1" color="gray">
                    {tAny("stretch_algorithm")}
                </Text>
                <Select.Root
                    value={
                        stretchAlgorithm === "hifigan_mel_hop" && !showHifiganMelHop
                            ? "signalsmith"
                            : stretchAlgorithm
                    }
                    onValueChange={(value) =>
                        onStretchAlgorithmChange(value as StretchAlgorithmOption)
                    }
                >
                    <Select.Trigger className="min-w-[150px]" />
                    <Select.Content>
                        <Select.Item value="melodyne_hybrid">
                            {tAny("stretch_option_melodyne_hybrid")}
                        </Select.Item>
                        <Select.Item value="loop">{tAny("stretch_option_loop")}</Select.Item>
                        {showHifiganMelHop ? (
                            <Select.Item value="hifigan_mel_hop">
                                {tAny("stretch_option_hifigan_mel_hop")}
                            </Select.Item>
                        ) : null}
                        <Select.Item value="signalsmith">
                            {tAny("stretch_option_signalsmith")}
                        </Select.Item>
                        <Select.Item value="soundtouch">
                            {tAny("stretch_option_soundtouch")}
                        </Select.Item>
                        <Select.Item value="linear">
                            {tAny("stretch_option_linear")}
                        </Select.Item>
                    </Select.Content>
                </Select.Root>
                <Button size="1" variant="soft" disabled={busy} onClick={onRedetect}>
                    {tAny("sample_timing_redetect")}
                </Button>
                <Button size="1" variant="soft" disabled={busy} onClick={onSave}>
                    {tAny("sample_timing_save")}
                </Button>
            </Flex>

            {!analysis ? (
                <Text size="1" color="gray" as="div" mt="2">
                    {tAny("sample_timing_analyzing")}
                </Text>
            ) : lane === "notes" ? (
                <Flex gap="1" mt="2" align="center" className="overflow-x-auto pb-1">
                    {analysis.pitch_notes.map((note, index) => (
                        <Button
                            key={`${note.start_sec}-${index}`}
                            size="1"
                            variant={activeNoteIndex === index ? "solid" : "soft"}
                            color={activeNoteIndex === index ? "blue" : "gray"}
                            onClick={() => onSelectNote(index)}
                            title={`${note.start_sec.toFixed(3)}–${note.end_sec.toFixed(3)} s`}
                        >
                            {`${index + 1} · MIDI ${note.midi_note.toFixed(1)}`}
                        </Button>
                    ))}
                    <div className="h-5 w-px shrink-0 bg-qt-border" />
                    <Button size="1" variant="soft" disabled={activeNoteIndex == null} onClick={onConnect}>
                        {tAny("melodyne_connect_pitch")}
                    </Button>
                    <Button size="1" variant="soft" disabled={activeNoteIndex == null} onClick={onDisconnect}>
                        {tAny("melodyne_disconnect_pitch")}
                    </Button>
                    <Text size="1" color="gray" className="shrink-0">
                        {tAny("melodyne_edge_drag_hint")}
                    </Text>
                </Flex>
            ) : lane === "events" ? (
                <Flex gap="1" mt="2" align="center" className="overflow-x-auto pb-1">
                    {(analysis.audio_events ?? []).map((event, index) => (
                        <Button
                            key={`${event.kind}-${event.start_sec}-${index}`}
                            size="1"
                            variant="soft"
                            color={event.kind === "breath" ? "amber" : "gray"}
                            title={`${event.start_sec.toFixed(3)}–${event.end_sec.toFixed(3)} s · ${(event.confidence * 100).toFixed(0)}%`}
                        >
                            {event.kind === "breath"
                                ? tAny("audio_event_breath")
                                : tAny("audio_event_silence")}
                            {` ${event.start_sec.toFixed(2)}–${event.end_sec.toFixed(2)}s`}
                        </Button>
                    ))}
                    {(analysis.audio_events ?? []).length === 0 ? (
                        <Text size="1" color="gray">
                            {tAny("audio_event_none")}
                        </Text>
                    ) : null}
                </Flex>
            ) : (
                <div className="mt-2 max-h-28 overflow-auto">
                    {analysis.annotations.map((row, index) => (
                        <div
                            key={index}
                            className={`mb-1 grid grid-cols-[22px_1.3fr_repeat(4,minmax(76px,1fr))] gap-1 rounded p-1 ${
                                index === analysis.active_annotation_index
                                    ? "bg-qt-highlight/15"
                                    : ""
                            }`}
                            onClick={() => onSelectRegion(index)}
                        >
                            <input
                                type="radio"
                                checked={index === analysis.active_annotation_index}
                                onChange={() => onSelectRegion(index)}
                            />
                            <TextField.Root
                                size="1"
                                value={row.name}
                                onChange={(event) => updateRegion(index, "name", event.target.value)}
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
                                    size="1"
                                    type="number"
                                    step="0.001"
                                    min="0"
                                    value={row[field]}
                                    title={tAny(`sample_timing_${field}`)}
                                    onChange={(event) =>
                                        updateRegion(index, field, event.target.value)
                                    }
                                />
                            ))}
                        </div>
                    ))}
                </div>
            )}
        </div>
    );
};
