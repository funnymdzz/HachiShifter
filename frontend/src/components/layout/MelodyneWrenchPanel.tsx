import React from "react";
import { Button, Flex, Select, Text, TextField } from "@radix-ui/themes";
import type { StretchAlgorithmOption } from "../../services/api/settings";
import type {
    ClipSampleAnnotationsResult,
    SampleRegionAnnotation,
} from "../../services/api/timeline";
import { useI18n } from "../../i18n/I18nProvider";

type Analysis = Extract<ClipSampleAnnotationsResult, { ok: true }>;
type NumericField = Exclude<keyof SampleRegionAnnotation, "name" | "melodyne_project_data">;

export const MelodyneWrenchPanel: React.FC<{
    analysis: Analysis | null;
    sourceLabel?: string | null;
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
    sourceLabel,
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
    const [lane, setLane] = React.useState<"samples" | "events">("samples");
    const [expandedRegionIndex, setExpandedRegionIndex] = React.useState<number | null>(null);

    React.useEffect(() => {
        if (!analysis) {
            setExpandedRegionIndex(null);
            return;
        }
        setExpandedRegionIndex(analysis.active_annotation_index);
    }, [analysis?.audio_path]);

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

    const selectMergedRegion = (index: number, expand = false) => {
        if (!analysis) return;
        onSelectRegion(index);
        if (expand) setExpandedRegionIndex(index);
        const row = analysis.annotations[index];
        if (!row || analysis.pitch_notes.length === 0) return;
        let nearestIndex = 0;
        let nearestDistance = Number.POSITIVE_INFINITY;
        analysis.pitch_notes.forEach((note, noteIndex) => {
            const distance =
                row.note_alignment_sec < note.start_sec
                    ? note.start_sec - row.note_alignment_sec
                    : row.note_alignment_sec > note.end_sec
                      ? row.note_alignment_sec - note.end_sec
                      : 0;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = noteIndex;
            }
        });
        onSelectNote(nearestIndex);
    };

    const expandedRegion =
        analysis && expandedRegionIndex != null
            ? analysis.annotations[expandedRegionIndex] ?? null
            : null;

    return (
        <div className="border-b border-qt-border bg-qt-base px-2 py-2 text-qt-text">
            <Flex align="center" gap="2" wrap="wrap">
                <Text size="1" weight="bold">
                    {tAny("melodyne_wrench_title")}
                </Text>
                <Button
                    size="1"
                    variant={lane === "samples" ? "solid" : "soft"}
                    color={lane === "samples" ? "blue" : "gray"}
                    onClick={() => setLane("samples")}
                >
                    {tAny("melodyne_sample_note_lane")}
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
                {sourceLabel ? (
                    <Text size="1" color="gray" title={sourceLabel} className="max-w-52 truncate">
                        {tAny("melodyne_source_file")}: {sourceLabel}
                    </Text>
                ) : null}
                <Text size="1" color="gray">
                    {tAny("melodyne_source_original_mode")}
                </Text>
                <div className="h-5 w-px bg-qt-border" />
                <Text size="1" color="gray">
                    {tAny("stretch_algorithm")}
                </Text>
                <Select.Root
                    disabled
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
                {analysis?.note_detector !== "melodyne" ? (
                    <Button size="1" variant="soft" disabled={busy} onClick={onRedetect}>
                        {tAny("sample_timing_redetect")}
                    </Button>
                ) : null}
                <Button size="1" variant="soft" disabled={busy} onClick={onSave}>
                    {tAny("sample_timing_save")}
                </Button>
            </Flex>

            {!analysis ? (
                <Text size="1" color="gray" as="div" mt="2">
                    {tAny("sample_timing_analyzing")}
                </Text>
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
                <div className="mt-2">
                    <Flex gap="1" align="center" className="overflow-x-auto pb-1">
                        {analysis.annotations.map((row, index) => {
                            const note = analysis.pitch_notes[index];
                            return (
                                <Button
                                    key={`${row.region_start_sec}-${index}`}
                                    size="1"
                                    className="shrink-0"
                                    variant={index === analysis.active_annotation_index ? "solid" : "soft"}
                                    color={index === analysis.active_annotation_index ? "blue" : "gray"}
                                    onClick={() => selectMergedRegion(index, true)}
                                    title={`${tAny("melodyne_edit_sample_details")} · ${row.region_start_sec.toFixed(3)}–${row.region_end_sec.toFixed(3)} s`}
                                >
                                    {`${index + 1} · ${row.name}${note ? ` · MIDI ${note.midi_note.toFixed(1)}` : ""}`}
                                </Button>
                            );
                        })}
                        <div className="h-5 w-px shrink-0 bg-qt-border" />
                        <Button size="1" variant="soft" disabled={activeNoteIndex == null} onClick={onConnect}>
                            {tAny("melodyne_connect_pitch")}
                        </Button>
                        <Button size="1" variant="soft" disabled={activeNoteIndex == null} onClick={onDisconnect}>
                            {tAny("melodyne_disconnect_pitch")}
                        </Button>
                    </Flex>
                    {expandedRegion && expandedRegionIndex != null ? (
                        <div className="mt-1 rounded border border-qt-border bg-qt-window/60 p-1">
                          <div className="grid grid-cols-[minmax(150px,1.4fr)_repeat(4,minmax(108px,1fr))] gap-1">
                            <label className="grid gap-0.5">
                                <Text size="1" color="gray">{tAny("sample_timing_name")}</Text>
                                <TextField.Root
                                    size="1"
                                    value={expandedRegion.name}
                                    onChange={(event) => updateRegion(expandedRegionIndex, "name", event.target.value)}
                                />
                            </label>
                            {(
                                [
                                    ["region_start_sec", "sample_timing_start"],
                                    ["region_end_sec", "sample_timing_end"],
                                    ["note_alignment_sec", "sample_timing_alignment"],
                                    ["fixed_duration_sec", "sample_timing_fixed_duration"],
                                ] as Array<[NumericField, string]>
                            ).map(([field, label]) => (
                                <label key={field} className="grid gap-0.5">
                                    <Text size="1" color="gray">{tAny(label)}</Text>
                                    <TextField.Root
                                        size="1"
                                        type="number"
                                        step="0.001"
                                        min="0"
                                        value={expandedRegion[field]}
                                        onChange={(event) => updateRegion(expandedRegionIndex, field, event.target.value)}
                                    />
                                </label>
                            ))}
                          </div>
                          {expandedRegion.melodyne_project_data ? (
                            <div className="mt-2 grid grid-cols-2 gap-x-3 gap-y-2 border-t border-qt-border pt-2 md:grid-cols-4 xl:grid-cols-8">
                              {(
                                [
                                  ["melodyne_pitch_center_cents", "melodyne_pitch_center", 0, 12700, 1],
                                  ["melodyne_pitch_drift_factor", "melodyne_pitch_drift", 0, 2, 0.01],
                                  ["melodyne_pitch_modulation_factor", "melodyne_pitch_modulation", 0, 2, 0.01],
                                  ["melodyne_transition_sec", "melodyne_transition", 0, 1, 0.001],
                                  ["melodyne_formant_offset_cents", "melodyne_formant", -1200, 1200, 1],
                                  ["melodyne_amplitude_factor", "melodyne_amplitude", 0, 2, 0.01],
                                  ["melodyne_sibilant_balance", "melodyne_sibilant", -1, 1, 0.01],
                                  ["melodyne_attack_duration_sec", "melodyne_consonant_length", 0, 1, 0.001],
                                ] as Array<[NumericField, string, number, number, number]>
                              ).map(([field, label, min, max, step]) => (
                                <label key={field} className="grid min-w-0 gap-0.5">
                                  <Flex align="center" justify="between" gap="1">
                                    <Text size="1" color="gray" className="truncate">{tAny(label)}</Text>
                                    <Text size="1" className="tabular-nums">{Number(expandedRegion[field]).toFixed(step < 0.01 ? 3 : step < 1 ? 2 : 0)}</Text>
                                  </Flex>
                                  <input
                                    className="h-4 w-full accent-orange-500"
                                    type="range"
                                    min={min}
                                    max={max}
                                    step={step}
                                    value={Number(expandedRegion[field])}
                                    onChange={(event) => updateRegion(expandedRegionIndex, field, event.target.value)}
                                  />
                                </label>
                              ))}
                            </div>
                          ) : null}
                        </div>
                    ) : null}
                    <Flex gap="3" mt="1" wrap="wrap">
                        <Text size="1" color="gray">{tAny("melodyne_edge_drag_hint")}</Text>
                        <Text size="1" color="gray">{tAny("melodyne_game_alignment_hint")}</Text>
                        <Text size="1" color="gray">{tAny("melodyne_connection_hint")}</Text>
                    </Flex>
                </div>
            )}
        </div>
    );
};
