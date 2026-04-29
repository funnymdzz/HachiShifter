import { createSlice, type PayloadAction } from "@reduxjs/toolkit";

export type NotebookMode = "edit" | "preview";

type NotebookState = {
    visible: boolean;
    mode: NotebookMode;
};

const initialState: NotebookState = {
    visible: false,
    mode: "edit",
};

const notebookSlice = createSlice({
    name: "notebook",
    initialState,
    reducers: {
        toggleNotebookVisible(state) {
            state.visible = !state.visible;
        },
        openNotebook(state) {
            state.visible = true;
        },
        closeNotebook(state) {
            state.visible = false;
        },
        setNotebookMode(state, action: PayloadAction<NotebookMode>) {
            state.mode = action.payload;
        },
    },
});

export const { toggleNotebookVisible, openNotebook, closeNotebook, setNotebookMode } =
    notebookSlice.actions;

export default notebookSlice.reducer;
