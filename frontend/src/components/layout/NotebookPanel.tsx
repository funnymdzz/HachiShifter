import { Box, Button, Flex, Text } from "@radix-ui/themes";
import { useMemo } from "react";
import { useAppDispatch, useAppSelector } from "../../app/hooks";
import { closeNotebook, setNotebookMode } from "../../features/notebook/notebookSlice";
import { setProjectNotesMarkdown } from "../../features/session/sessionSlice";
import { useI18n } from "../../i18n/I18nProvider";
import { renderMarkdownPreview } from "./notebook/markdownPreview";

export function NotebookPanel() {
    const dispatch = useAppDispatch();
    const { t } = useI18n();
    const mode = useAppSelector((state) => state.notebook.mode);
    const markdown = useAppSelector((state) => state.session.project.notesMarkdown);

    const previewHtml = useMemo(() => renderMarkdownPreview(markdown), [markdown]);

    return (
        <Flex className="h-full min-h-0 flex-col bg-qt-window">
            <Flex
                align="center"
                justify="between"
                className="shrink-0 border-b border-qt-border px-2 py-1.5"
            >
                <Text size="2" weight="medium">
                    {t("notebook")}
                </Text>
                <Flex align="center" gap="1">
                    <Button
                        size="1"
                        variant={mode === "edit" ? "solid" : "soft"}
                        color={mode === "edit" ? "blue" : "gray"}
                        onClick={() => dispatch(setNotebookMode("edit"))}
                    >
                        {t("notebook_edit")}
                    </Button>
                    <Button
                        size="1"
                        variant={mode === "preview" ? "solid" : "soft"}
                        color={mode === "preview" ? "blue" : "gray"}
                        onClick={() => dispatch(setNotebookMode("preview"))}
                    >
                        {t("notebook_preview")}
                    </Button>
                    <Button
                        size="1"
                        variant="ghost"
                        color="gray"
                        onClick={() => dispatch(closeNotebook())}
                    >
                        {t("close")}
                    </Button>
                </Flex>
            </Flex>

            <Box className="min-h-0 flex-1">
                {mode === "edit" ? (
                    <textarea
                        value={markdown}
                        onChange={(event) => dispatch(setProjectNotesMarkdown(event.target.value))}
                        placeholder={t("notebook_placeholder")}
                        className="h-full w-full resize-none border-0 bg-qt-base px-3 py-3 text-sm text-qt-text outline-none"
                        spellCheck={false}
                    />
                ) : (
                    <div className="h-full overflow-auto bg-qt-base px-4 py-3 text-sm text-qt-text">
                        <div
                            className="prose prose-sm max-w-none prose-headings:text-qt-text prose-p:text-qt-text prose-li:text-qt-text prose-strong:text-qt-text prose-code:text-qt-text prose-pre:bg-black/20"
                            dangerouslySetInnerHTML={{ __html: previewHtml }}
                        />
                    </div>
                )}
            </Box>
        </Flex>
    );
}
