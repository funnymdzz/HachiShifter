# File Browser (文件资源管理器) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 实现一个右侧边栏文件资源管理器，支持目录浏览、搜索过滤、音频文件预览播放、拖拽导入轨道，以及预览音量调节。

**Architecture:** 混合方案（方案 C）—— 后端负责目录扫描和音频元信息/PCM 读取（复用现有 symphonia/hound 解码能力），前端通过 Web Audio API 播放预览。拖拽导入复用现有的 `importAudioAtPosition` thunk。

**Tech Stack:** Rust (Tauri commands, std::fs, hound, symphonia) + React (Redux Toolkit, Web Audio API, HTML5 Drag & Drop, Radix UI)

---

## Task 1: 后端 — 新建 `commands/file_browser.rs`

**Files:**
- Create: `backend/src-tauri/src/commands/file_browser.rs`

实现 3 个内部函数，由 `commands.rs` 门面层转发调用：

```rust
// commands/file_browser.rs

use crate::state::AppState;
use tauri::State;
use std::path::Path;

/// 目录条目
#[derive(serde::Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct FileEntry {
    pub name: String,
    pub path: String,
    pub is_dir: bool,
    pub size: Option<u64>,
    pub extension: Option<String>,
}

/// 音频文件元信息
#[derive(serde::Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct AudioFileInfo {
    pub sample_rate: u32,
    pub channels: u16,
    pub duration_sec: f64,
    pub total_frames: u64,
}

/// 预览 PCM 数据
#[derive(serde::Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct AudioPreviewData {
    pub sample_rate: u32,
    pub channels: u16,
    pub pcm_base64: String,
}

/// 支持的音频扩展名
const AUDIO_EXTENSIONS: &[&str] = &["wav", "mp3", "flac", "ogg", "aac", "aif", "aiff", "m4a"];

fn is_audio_extension(ext: &str) -> bool {
    AUDIO_EXTENSIONS.iter().any(|&e| e.eq_ignore_ascii_case(ext))
}

/// 列出指定目录下的文件和子目录
pub(crate) fn list_directory(dir_path: String) -> Result<Vec<FileEntry>, String> {
    let path = Path::new(&dir_path);
    if !path.is_dir() {
        return Err(format!("Not a directory: {}", dir_path));
    }

    let mut entries = Vec::new();
    let read_dir = std::fs::read_dir(path).map_err(|e| e.to_string())?;

    for entry in read_dir {
        let entry = entry.map_err(|e| e.to_string())?;
        let metadata = entry.metadata().map_err(|e| e.to_string())?;
        let name = entry.file_name().to_string_lossy().into_owned();

        // 跳过隐藏文件（以.开头）
        if name.starts_with('.') {
            continue;
        }

        let is_dir = metadata.is_dir();
        let size = if is_dir { None } else { Some(metadata.len()) };
        let extension = if is_dir {
            None
        } else {
            entry
                .path()
                .extension()
                .and_then(|e| e.to_str())
                .map(|e| e.to_lowercase())
        };

        entries.push(FileEntry {
            name,
            path: entry.path().to_string_lossy().into_owned(),
            is_dir,
            size,
            extension,
        });
    }

    // 目录在前，文件在后；各自按名称排序
    entries.sort_by(|a, b| {
        match (a.is_dir, b.is_dir) {
            (true, false) => std::cmp::Ordering::Less,
            (false, true) => std::cmp::Ordering::Greater,
            _ => a.name.to_lowercase().cmp(&b.name.to_lowercase()),
        }
    });

    Ok(entries)
}

/// 获取音频文件元信息
pub(crate) fn get_audio_file_info(file_path: String) -> Result<AudioFileInfo, String> {
    let path = Path::new(&file_path);
    if !path.is_file() {
        return Err(format!("Not a file: {}", file_path));
    }

    let info = crate::audio_utils::try_read_wav_info(path, 0)
        .ok_or_else(|| format!("Failed to read audio info: {}", file_path))?;

    Ok(AudioFileInfo {
        sample_rate: info.sample_rate,
        channels: 2, // try_read_wav_info 不直接返回声道数，这里需要从解码获取
        duration_sec: info.duration_sec,
        total_frames: info.total_frames,
    })
}

/// 读取音频预览 PCM（限制最大帧数，降采样到可接受水平）
pub(crate) fn read_audio_preview(
    file_path: String,
    max_frames: Option<u32>,
) -> Result<AudioPreviewData, String> {
    let path = Path::new(&file_path);
    if !path.is_file() {
        return Err(format!("Not a file: {}", file_path));
    }

    let max = max_frames.unwrap_or(480_000) as usize; // 默认最多 ~10秒 @48kHz

    let (sample_rate, channels, samples) =
        crate::audio_utils::decode_audio_f32_interleaved(path)?;

    let total_frames = samples.len() / channels as usize;
    let frames_to_use = total_frames.min(max);
    let samples_to_use = frames_to_use * channels as usize;

    // 将 f32 PCM 转为 base64
    let bytes: Vec<u8> = samples[..samples_to_use]
        .iter()
        .flat_map(|&f| f.to_le_bytes())
        .collect();

    let pcm_base64 = base64::Engine::encode(
        &base64::engine::general_purpose::STANDARD,
        &bytes,
    );

    Ok(AudioPreviewData {
        sample_rate,
        channels,
        pcm_base64,
    })
}
```

> **注意**: `get_audio_file_info` 中声道数获取需要优化——可以改为调用 `decode_audio_f32_interleaved` 只取元信息，或者在 `audio_utils` 中新增一个轻量函数。实际实现时可使用 symphonia 直接读取 codec_params。

---

## Task 2: 后端 — 在 `commands.rs` 门面层注册新命令

**Files:**
- Modify: `backend/src-tauri/src/commands.rs`

在文件顶部添加模块引入：
```rust
#[path = "commands/file_browser.rs"]
mod file_browser;
```

在文件中添加 3 个 `#[tauri::command]` 函数：
```rust
// ===================== file_browser =====================

#[tauri::command(rename_all = "camelCase")]
pub fn list_directory(dir_path: String) -> Result<Vec<file_browser::FileEntry>, String> {
    file_browser::list_directory(dir_path)
}

#[tauri::command(rename_all = "camelCase")]
pub fn get_audio_file_info(file_path: String) -> Result<file_browser::AudioFileInfo, String> {
    file_browser::get_audio_file_info(file_path)
}

#[tauri::command(rename_all = "camelCase")]
pub fn read_audio_preview(
    file_path: String,
    max_frames: Option<u32>,
) -> Result<file_browser::AudioPreviewData, String> {
    file_browser::read_audio_preview(file_path, max_frames)
}
```

---

## Task 3: 后端 — 在 `lib.rs` 的 invoke_handler 注册新命令

**Files:**
- Modify: `backend/src-tauri/src/lib.rs`

在 `invoke_handler` 的 `tauri::generate_handler![]` 列表末尾追加：
```rust
commands::list_directory,
commands::get_audio_file_info,
commands::read_audio_preview,
```

---

## Task 4: 前端 API 层 — 新建 `services/api/fileBrowser.ts`

**Files:**
- Create: `frontend/src/services/api/fileBrowser.ts`
- Modify: `frontend/src/services/api/index.ts`

```typescript
// fileBrowser.ts
import { invoke } from "../invoke";

export interface FileEntry {
    name: string;
    path: string;
    isDir: boolean;
    size: number | null;
    extension: string | null;
}

export interface AudioFileInfo {
    sampleRate: number;
    channels: number;
    durationSec: number;
    totalFrames: number;
}

export interface AudioPreviewData {
    sampleRate: number;
    channels: number;
    pcmBase64: string;
}

export const fileBrowserApi = {
    listDirectory: (dirPath: string) =>
        invoke<FileEntry[]>("list_directory", dirPath),

    getAudioFileInfo: (filePath: string) =>
        invoke<AudioFileInfo>("get_audio_file_info", filePath),

    readAudioPreview: (filePath: string, maxFrames?: number) =>
        invoke<AudioPreviewData>("read_audio_preview", filePath, maxFrames),
};
```

在 `index.ts` 追加导出：
```typescript
export { fileBrowserApi } from "./fileBrowser";
```

---

## Task 5: 前端 — 在 `invoke.ts` 的 `buildTauriArgs` 中添加参数映射

**Files:**
- Modify: `frontend/src/services/invoke.ts`

在 `buildTauriArgs` 的 `switch` 中追加 3 个 case：
```typescript
case "list_directory":
    return { dirPath: args[0] };

case "get_audio_file_info":
    return { filePath: args[0] };

case "read_audio_preview":
    return {
        filePath: args[0],
        ...(args[1] !== undefined ? { maxFrames: args[1] } : {}),
    };
```

---

## Task 6: 前端 — 新建 Redux slice `features/fileBrowser/fileBrowserSlice.ts`

**Files:**
- Create: `frontend/src/features/fileBrowser/fileBrowserSlice.ts`

```typescript
import { createSlice, createAsyncThunk, type PayloadAction } from "@reduxjs/toolkit";
import { fileBrowserApi, type FileEntry } from "../../services/api/fileBrowser";

interface FileBrowserState {
    visible: boolean;
    currentPath: string;
    entries: FileEntry[];
    loading: boolean;
    error: string | null;
    previewVolume: number;      // 0~1
    previewingFile: string | null;
    searchQuery: string;        // 搜索过滤关键词
}

const STORAGE_KEY = "hachishifter.fileBrowser.lastPath";

function getInitialPath(): string {
    return localStorage.getItem(STORAGE_KEY) || "";
}

const initialState: FileBrowserState = {
    visible: false,
    currentPath: getInitialPath(),
    entries: [],
    loading: false,
    error: null,
    previewVolume: 0.8,
    previewingFile: null,
    searchQuery: "",
};

export const loadDirectory = createAsyncThunk(
    "fileBrowser/loadDirectory",
    async (dirPath: string, { rejectWithValue }) => {
        try {
            const entries = await fileBrowserApi.listDirectory(dirPath);
            return { dirPath, entries };
        } catch (err) {
            return rejectWithValue(
                err instanceof Error ? err.message : "Failed to load directory",
            );
        }
    },
);

const fileBrowserSlice = createSlice({
    name: "fileBrowser",
    initialState,
    reducers: {
        toggleVisible(state) {
            state.visible = !state.visible;
        },
        setVisible(state, action: PayloadAction<boolean>) {
            state.visible = action.payload;
        },
        setPreviewVolume(state, action: PayloadAction<number>) {
            state.previewVolume = Math.max(0, Math.min(1, action.payload));
        },
        setPreviewingFile(state, action: PayloadAction<string | null>) {
            state.previewingFile = action.payload;
        },
        setSearchQuery(state, action: PayloadAction<string>) {
            state.searchQuery = action.payload;
        },
    },
    extraReducers: (builder) => {
        builder
            .addCase(loadDirectory.pending, (state) => {
                state.loading = true;
                state.error = null;
            })
            .addCase(loadDirectory.fulfilled, (state, action) => {
                state.loading = false;
                state.currentPath = action.payload.dirPath;
                state.entries = action.payload.entries;
                localStorage.setItem(STORAGE_KEY, action.payload.dirPath);
            })
            .addCase(loadDirectory.rejected, (state, action) => {
                state.loading = false;
                state.error = String(action.payload ?? "Unknown error");
            });
    },
});

export const {
    toggleVisible,
    setVisible,
    setPreviewVolume,
    setPreviewingFile,
    setSearchQuery,
} = fileBrowserSlice.actions;

export default fileBrowserSlice.reducer;
```

---

## Task 7: 前端 — 注册 reducer 到 store

**Files:**
- Modify: `frontend/src/app/store.ts`

```typescript
import fileBrowserReducer from '../features/fileBrowser/fileBrowserSlice'

export const store = configureStore({
    reducer: {
        session: sessionReducer,
        fileBrowser: fileBrowserReducer,
    },
})
```

---

## Task 8: 前端 — 新建 `AudioPreviewEngine` 模块

**Files:**
- Create: `frontend/src/features/fileBrowser/audioPreview.ts`

单例模块，管理 Web Audio API 预览播放：

```typescript
import { fileBrowserApi, type AudioPreviewData } from "../../services/api/fileBrowser";

class AudioPreviewEngine {
    private ctx: AudioContext | null = null;
    private gainNode: GainNode | null = null;
    private source: AudioBufferSourceNode | null = null;
    private currentFile: string | null = null;
    private cache = new Map<string, AudioBuffer>();
    private onEndCallback: (() => void) | null = null;

    private ensureContext(): { ctx: AudioContext; gain: GainNode } {
        if (!this.ctx) {
            this.ctx = new AudioContext();
            this.gainNode = this.ctx.createGain();
            this.gainNode.connect(this.ctx.destination);
        }
        return { ctx: this.ctx, gain: this.gainNode! };
    }

    async play(filePath: string, onEnd?: () => void): Promise<void> {
        // 如果正在播放同一文件，停止
        if (this.currentFile === filePath && this.source) {
            this.stop();
            return;
        }

        this.stop();
        this.onEndCallback = onEnd ?? null;
        const { ctx, gain } = this.ensureContext();

        let buffer = this.cache.get(filePath);
        if (!buffer) {
            // 从后端获取 PCM
            const data: AudioPreviewData = await fileBrowserApi.readAudioPreview(filePath, 480_000);
            buffer = this.decodePreviewData(ctx, data);
            this.cache.set(filePath, buffer);
        }

        const source = ctx.createBufferSource();
        source.buffer = buffer;
        source.connect(gain);
        source.onended = () => {
            if (this.currentFile === filePath) {
                this.currentFile = null;
                this.source = null;
                this.onEndCallback?.();
            }
        };
        source.start();
        this.source = source;
        this.currentFile = filePath;
    }

    stop(): void {
        if (this.source) {
            try { this.source.stop(); } catch { /* 已停止 */ }
            this.source.disconnect();
            this.source = null;
        }
        this.currentFile = null;
        this.onEndCallback = null;
    }

    setVolume(v: number): void {
        if (this.gainNode) {
            this.gainNode.gain.value = Math.max(0, Math.min(1, v));
        }
    }

    isPlaying(): boolean {
        return this.source !== null && this.currentFile !== null;
    }

    getCurrentFile(): string | null {
        return this.currentFile;
    }

    clearCache(): void {
        this.cache.clear();
    }

    private decodePreviewData(ctx: AudioContext, data: AudioPreviewData): AudioBuffer {
        // 将 base64 PCM f32 LE 转为 AudioBuffer
        const binaryStr = atob(data.pcmBase64);
        const bytes = new Uint8Array(binaryStr.length);
        for (let i = 0; i < binaryStr.length; i++) {
            bytes[i] = binaryStr.charCodeAt(i);
        }
        const floats = new Float32Array(bytes.buffer);
        const frames = Math.floor(floats.length / data.channels);
        const audioBuffer = ctx.createBuffer(data.channels, frames, data.sampleRate);

        // 反交错到各声道
        for (let ch = 0; ch < data.channels; ch++) {
            const channelData = audioBuffer.getChannelData(ch);
            for (let f = 0; f < frames; f++) {
                channelData[f] = floats[f * data.channels + ch];
            }
        }

        return audioBuffer;
    }
}

export const audioPreview = new AudioPreviewEngine();
```

---

## Task 9: 前端 — i18n 添加文件浏览器相关翻译键

**Files:**
- Modify: `frontend/src/i18n/messages.ts`

在 `en-US` 中添加：
```typescript
fb_title: "File Browser",
fb_search_placeholder: "Search files...",
fb_open_folder: "Open Folder",
fb_refresh: "Refresh",
fb_parent_dir: "Parent Directory",
fb_preview_volume: "Preview Volume",
fb_no_folder: "No folder selected",
fb_empty_folder: "Empty folder",
fb_no_results: "No matching files",
fb_loading: "Loading...",
fb_error: "Error loading directory",
```

在 `zh-CN` 中添加：
```typescript
fb_title: "文件浏览器",
fb_search_placeholder: "搜索文件...",
fb_open_folder: "打开文件夹",
fb_refresh: "刷新",
fb_parent_dir: "上级目录",
fb_preview_volume: "预览音量",
fb_no_folder: "未选择文件夹",
fb_empty_folder: "空文件夹",
fb_no_results: "没有匹配的文件",
fb_loading: "加载中...",
fb_error: "加载目录出错",
```

---

## Task 10: 前端 — 新建 `FileBrowserPanel` UI 组件

**Files:**
- Create: `frontend/src/components/layout/FileBrowserPanel.tsx`

组件结构：
```
┌──────────────────────────────────┐
│ 📁 文件浏览器    [打开][刷新][×] │  ← 标题栏
├──────────────────────────────────┤
│ 🔍 [搜索框...]                   │  ← 搜索栏
├──────────────────────────────────┤
│ 📂 D:\Audio\Samples  [↑上级]    │  ← 路径栏 + 面包屑
├──────────────────────────────────┤
│ 📁 Drums/                        │  ← 文件列表（可滚动）
│ 📁 Vocals/                       │
│ ─────────────────                 │
│ 🎵 kick.wav    0:01  44.1k  ▶   │  ← 音频文件（点击预览）
│ 🎵 snare.wav   0:00  48.0k      │
│ 🎵 vocal.mp3   3:42  44.1k      │
│ 📄 notes.txt                     │  ← 非音频文件（灰色）
├──────────────────────────────────┤
│ 🔊 ████████░░  预览音量           │  ← 底部音量滑块
└──────────────────────────────────┘
```

关键功能：
- 标题栏：标题 + 打开文件夹按钮（调用 Tauri dialog 选目录）+ 刷新按钮 + 收起按钮
- 搜索栏：实时过滤当前目录的文件（前端过滤，对 `entries` 的 `name` 做 case-insensitive 匹配）
- 路径栏：显示当前路径 + 上级目录按钮
- 文件列表：
  - 目录：双击进入
  - 音频文件：单击预览播放/停止；`draggable="true"` 支持拖拽
  - 非音频文件：灰色显示，不可交互
- 底部音量滑块：调节 `audioPreview.setVolume()`

---

## Task 11: 前端 — 修改 `App.tsx` 集成右侧面板

**Files:**
- Modify: `frontend/src/App.tsx`

将现有的 `Main Splitter Area` 包裹在一个 `Flex` 容器中，右侧添加 `FileBrowserPanel`：

```tsx
{/* Main Splitter Area */}
<Flex className="flex-1 min-h-0">
    {/* 左侧：现有的上下分割区域 */}
    <div ref={containerRef} className="flex-1 min-w-0 min-h-0 flex flex-col">
        {/* 原有的 Timeline + Splitter + PianoRoll */}
    </div>

    {/* 右侧：文件浏览器面板 */}
    {fileBrowserVisible && (
        <div className="w-[280px] shrink-0 border-l border-qt-border bg-qt-window flex flex-col">
            <FileBrowserPanel />
        </div>
    )}
</Flex>
```

从 Redux 读取 `fileBrowserVisible`：
```tsx
const fileBrowserVisible = useAppSelector((state) => state.fileBrowser.visible);
```

---

## Task 12: 前端 — 修改 `invoke.ts` 中的 drop 处理以支持内部拖拽

**Files:**
- Modify: `frontend/src/components/layout/TimelinePanel.tsx`

在现有的 `onDrop` 处理中，优先检查自定义 MIME type `application/x-hachishifter-file`：

```typescript
// 在 onDrop 回调的开头添加
const internalPath = dt?.getData?.("application/x-hachishifter-file");
if (internalPath) {
    e.preventDefault();
    const el = e.currentTarget as HTMLDivElement;
    const bounds = el.getBoundingClientRect();
    const beat = beatFromClientX(e.clientX, bounds, el.scrollLeft);
    const trackId = trackIdFromClientY(e.clientY);
    setDropPreview(null);
    void dispatch(
        importAudioAtPosition({
            audioPath: internalPath,
            trackId,
            startSec: beat,
        }),
    );
    return;
}
```

同样在 `onDragOver` 中识别内部拖拽：
```typescript
const internalPath = dt?.getData?.("application/x-hachishifter-file");
// 注意：某些浏览器在 dragover 时无法读取 getData，所以还需检查 types
const hasInternal = dt?.types?.includes("application/x-hachishifter-file");
if (hasInternal) {
    e.preventDefault();
    // 更新 ghost 预览...
}
```

---

## Task 13: 前端 — 在 ActionBar 或 MenuBar 中添加文件浏览器切换入口

**Files:**
- Modify: `frontend/src/components/layout/ActionBar.tsx` (推荐)

在 ActionBar 最右侧添加一个文件夹图标按钮，用于切换文件浏览器面板显示/隐藏：

```tsx
import { toggleVisible } from "../../features/fileBrowser/fileBrowserSlice";
import { FileIcon } from "@radix-ui/react-icons";

// 在 Transport 按钮组后面追加：
<Separator orientation="vertical" size="2" />
<IconButton
    size="1"
    variant={fileBrowserVisible ? "solid" : "ghost"}
    color="gray"
    title={t("fb_title")}
    onClick={() => dispatch(toggleVisible())}
>
    <FileIcon />
</IconButton>
```

---

## 文件修改清单

| # | 操作 | 文件路径 |
|---|------|---------|
| 1 | 新建 | `backend/src-tauri/src/commands/file_browser.rs` |
| 2 | 修改 | `backend/src-tauri/src/commands.rs` |
| 3 | 修改 | `backend/src-tauri/src/lib.rs` |
| 4 | 新建 | `frontend/src/services/api/fileBrowser.ts` |
| 5 | 修改 | `frontend/src/services/api/index.ts` |
| 6 | 修改 | `frontend/src/services/invoke.ts` |
| 7 | 新建 | `frontend/src/features/fileBrowser/fileBrowserSlice.ts` |
| 8 | 修改 | `frontend/src/app/store.ts` |
| 9 | 新建 | `frontend/src/features/fileBrowser/audioPreview.ts` |
| 10 | 修改 | `frontend/src/i18n/messages.ts` |
| 11 | 新建 | `frontend/src/components/layout/FileBrowserPanel.tsx` |
| 12 | 修改 | `frontend/src/App.tsx` |
| 13 | 修改 | `frontend/src/components/layout/TimelinePanel.tsx` |
| 14 | 修改 | `frontend/src/components/layout/ActionBar.tsx` |
