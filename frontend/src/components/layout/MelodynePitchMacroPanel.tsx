import React from "react";
import { Badge, Button, Flex, Text } from "@radix-ui/themes";
import { useI18n } from "../../i18n/I18nProvider";
import type { NoteDetectorKind } from "../../services/api/timeline";

export interface MelodyneMacroValues {
    centerStrength: number;
    driftStrength: number;
    modulationStrength: number;
    transitionMs: number;
    performanceMode: boolean;
}

const initialValues: MelodyneMacroValues = {
    centerStrength: 1,
    driftStrength: 0.5,
    modulationStrength: 0.2,
    transitionMs: 80,
    performanceMode: false,
};

const MacroSlider: React.FC<{
    label: string;
    value: number;
    min: number;
    max: number;
    step: number;
    suffix: string;
    onChange: (value: number) => void;
}> = ({ label, value, min, max, step, suffix, onChange }) => (
    <label className="melodyne-macro-control">
        <span className="melodyne-macro-label">
            <span>{label}</span>
            <strong>
                {Math.round(value)}
                {suffix}
            </strong>
        </span>
        <input
            type="range"
            min={min}
            max={max}
            step={step}
            value={value}
            onChange={(event) => onChange(Number(event.currentTarget.value))}
        />
    </label>
);

export const MelodynePitchMacroPanel: React.FC<{
    detector: NoteDetectorKind | null;
    detectorMessage?: string | null;
    hasSelection: boolean;
    disabled?: boolean;
    onApply: (values: MelodyneMacroValues, reset: boolean) => Promise<string>;
}> = ({ detector, detectorMessage, hasSelection, disabled = false, onApply }) => {
    const { t } = useI18n();
    const [values, setValues] = React.useState(initialValues);
    const [busy, setBusy] = React.useState(false);
    const [status, setStatus] = React.useState("");

    const update = <K extends keyof MelodyneMacroValues,>(
        key: K,
        value: MelodyneMacroValues[K],
    ) => {
        setValues((current) => ({ ...current, [key]: value }));
    };

    const run = async (reset: boolean) => {
        setBusy(true);
        setStatus("");
        try {
            setStatus(await onApply(values, reset));
        } catch (error) {
            setStatus(error instanceof Error ? error.message : String(error));
        } finally {
            setBusy(false);
        }
    };

    return (
        <div className="melodyne-macro-panel">
            <Flex align="center" justify="between" gap="3" className="melodyne-macro-heading">
                <Flex align="center" gap="2">
                    <span className="melodyne-tool-orb" aria-hidden />
                    <div>
                        <Text size="2" weight="bold">
                            {t("melodyne_macro_title")}
                        </Text>
                        <Text as="div" size="1" color="gray">
                            {hasSelection
                                ? t("melodyne_macro_selected_scope")
                                : t("melodyne_macro_clip_scope")}
                        </Text>
                    </div>
                </Flex>
                <Badge color={detector === "game" ? "blue" : "gray"} variant="soft">
                    {detector === "game" ? "GAME" : "YIN"}
                </Badge>
            </Flex>

            <div className="melodyne-macro-grid">
                <MacroSlider
                    label={t("melodyne_pitch_center")}
                    value={values.centerStrength * 100}
                    min={0}
                    max={100}
                    step={1}
                    suffix="%"
                    onChange={(value) => update("centerStrength", value / 100)}
                />
                <MacroSlider
                    label={t("melodyne_pitch_drift")}
                    value={values.driftStrength * 100}
                    min={0}
                    max={100}
                    step={1}
                    suffix="%"
                    onChange={(value) => update("driftStrength", value / 100)}
                />
                <MacroSlider
                    label={t("melodyne_pitch_modulation")}
                    value={values.modulationStrength * 100}
                    min={0}
                    max={100}
                    step={1}
                    suffix="%"
                    onChange={(value) => update("modulationStrength", value / 100)}
                />
                <MacroSlider
                    label={t("melodyne_transition")}
                    value={values.transitionMs}
                    min={0}
                    max={300}
                    step={5}
                    suffix=" ms"
                    onChange={(value) => update("transitionMs", value)}
                />
            </div>

            <Flex align="center" justify="between" gap="3" mt="3" className="melodyne-model-mode">
                <div>
                    <Text as="div" size="1" weight="bold">
                        {t("melodyne_game_model")}
                    </Text>
                    <Text as="div" size="1" color="gray">
                        {values.performanceMode
                            ? t("melodyne_game_small_hint")
                            : t("melodyne_game_large_hint")}
                    </Text>
                </div>
                <Flex gap="1" className="shrink-0">
                    <Button
                        size="1"
                        variant={!values.performanceMode ? "solid" : "soft"}
                        color="blue"
                        disabled={disabled || busy}
                        onClick={() => update("performanceMode", false)}
                    >
                        {t("melodyne_game_large")}
                    </Button>
                    <Button
                        size="1"
                        variant={values.performanceMode ? "solid" : "soft"}
                        color="gray"
                        disabled={disabled || busy}
                        onClick={() => update("performanceMode", true)}
                    >
                        {t("melodyne_game_small")}
                    </Button>
                </Flex>
            </Flex>

            <Flex align="center" justify="between" gap="3" mt="3">
                <Text size="1" color={detectorMessage ? "amber" : "gray"} className="min-w-0">
                    {status || detectorMessage || t("melodyne_macro_hint")}
                </Text>
                <Flex gap="2" className="shrink-0">
                    <Button
                        size="1"
                        variant="soft"
                        color="gray"
                        disabled={disabled || busy}
                        onClick={() => void run(true)}
                    >
                        {t("melodyne_reset")}
                    </Button>
                    <Button
                        size="1"
                        color="blue"
                        disabled={disabled || busy}
                        onClick={() => void run(false)}
                    >
                        {busy ? t("melodyne_applying") : t("melodyne_apply")}
                    </Button>
                </Flex>
            </Flex>
        </div>
    );
};
