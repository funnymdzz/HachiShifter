#include "I18n.h"
#include <array>
#include <unordered_map>

namespace hachi
{
namespace
{
using Row = std::array<const char*, 5>;
const std::unordered_map<std::string, Row> strings {
    { "app.title",       { "HachiShifter Next", "HachiShifter Next", "HachiShifter Next", "HachiShifter Next", "HachiShifter Next" } },
    { "file.open",       { "打开工程", "開啟工程", "プロジェクトを開く", "프로젝트 열기", "Open Project" } },
    { "file.save",       { "保存工程", "儲存工程", "プロジェクトを保存", "프로젝트 저장", "Save Project" } },
    { "file.export",     { "导出 WAV", "匯出 WAV", "WAVを書き出す", "WAV 내보내기", "Export WAV" } },
    { "file.audio",      { "导入音频", "匯入音訊", "オーディオを読み込む", "오디오 가져오기", "Import Audio" } },
    { "file.melodyne",   { "导入 Melodyne", "匯入 Melodyne", "Melodyneを読み込む", "Melodyne 가져오기", "Import Melodyne" } },
    { "file.new",        { "新建工程", "新增工程", "新規プロジェクト", "새 프로젝트", "New Project" } },
    { "file.midi",       { "导入 MIDI", "匯入 MIDI", "MIDIを読み込む", "MIDI 가져오기", "Import MIDI" } },
    { "file.exit",       { "退出", "結束", "終了", "종료", "Exit" } },
    { "file.settings",   { "设置…", "設定…", "設定…", "설정…", "Settings…" } },
    { "file.assets",     { "素材管理器…", "素材管理器…", "素材マネージャー…", "소재 관리자…", "Asset Manager…" } },
    { "menu.file",       { "文件", "檔案", "ファイル", "파일", "File" } },
    { "menu.edit",       { "编辑", "編輯", "編集", "편집", "Edit" } },
    { "menu.track",      { "轨道", "軌道", "トラック", "트랙", "Track" } },
    { "menu.view",       { "视图", "檢視", "表示", "보기", "View" } },
    { "menu.help",       { "帮助", "說明", "ヘルプ", "도움말", "Help" } },
    { "edit.undo",       { "撤销", "復原", "元に戻す", "실행 취소", "Undo" } },
    { "edit.redo",       { "重做", "重做", "やり直す", "다시 실행", "Redo" } },
    { "edit.selectAll",  { "全选音符", "全選音符", "全ノートを選択", "모든 노트 선택", "Select All Notes" } },
    { "view.zoomIn",     { "放大", "放大", "拡大", "확대", "Zoom In" } },
    { "view.zoomOut",    { "缩小", "縮小", "縮小", "축소", "Zoom Out" } },
    { "view.zoomFit",    { "适合工程", "符合工程", "プロジェクト全体", "프로젝트 맞춤", "Fit Project" } },
    { "help.about",      { "关于 HachiShifter", "關於 HachiShifter", "HachiShifterについて", "HachiShifter 정보", "About HachiShifter" } },
    { "help.aboutText",  { "HachiShifter Next · JUCE/C++ 原生重构版", "HachiShifter Next · JUCE/C++ 原生重構版", "HachiShifter Next · JUCE/C++ ネイティブ版", "HachiShifter Next · JUCE/C++ 네이티브 버전", "HachiShifter Next · Native JUCE/C++ edition" } },
    { "transport.play",  { "播放", "播放", "再生", "재생", "Play" } },
    { "transport.pause", { "暂停", "暫停", "一時停止", "일시 정지", "Pause" } },
    { "transport.stop",  { "停止", "停止", "停止", "정지", "Stop" } },
    { "tool.main",       { "音符编辑", "音符編輯", "ノート編集", "노트 편집", "Note Edit" } },
    { "tool.wrench",     { "采样精修", "取樣精修", "サンプル編集", "샘플 정밀 편집", "Sample Edit" } },
    { "tool.draw",       { "自由绘制", "自由繪製", "フリーハンド", "자유 그리기", "Free Draw" } },
    { "tool.line",       { "直线工具", "直線工具", "直線ツール", "직선 도구", "Line Tool" } },
    { "tool.connect",    { "连接/分离音符", "連接/分離音符", "ノート接続/分離", "노트 연결/분리", "Connect/Separate Notes" } },
    { "editor.parameters", { "参数编辑器", "參數編輯器", "パラメータ", "매개변수 편집기", "Parameters" } },
    { "editor.smooth",   { "平滑", "平滑", "平滑化", "평활", "Smooth" } },
    { "editor.drift",    { "漂移修正", "漂移修正", "ドリフト補正", "드리프트 보정", "Drift Correction" } },
    { "editor.attackSpeed", { "辅音速度", "子音速度", "アタックスピード", "어택 속도", "Attack Speed" } },
    { "param.pitch",     { "音高", "音高", "ピッチ", "피치", "Pitch" } },
    { "param.drift",     { "漂移", "漂移", "ドリフト", "드리프트", "Drift" } },
    { "param.attack",    { "起音", "起音", "アタック", "어택", "Attack" } },
    { "param.breath",    { "呼吸", "呼吸", "ブレス", "브레스", "Breath" } },
    { "param.tension",   { "张力", "張力", "テンション", "텐션", "Tension" } },
    { "param.formant",   { "共振峰", "共振峰", "フォルマント", "포먼트", "Formant" } },
    { "param.volume",    { "音量", "音量", "音量", "음량", "Volume" } },
    { "beats.bar",       { "每小节", "每小節", "拍子", "마디 박자", "Beats" } },
    { "grid",            { "网格", "網格", "グリッド", "그리드", "Grid" } },
    { "base.scale",      { "基准调", "基準調", "基準キー", "기준 키", "Key" } },
    { "algo.pitch",      { "变调算法", "變調演算法", "ピッチアルゴリズム", "피치 알고리즘", "Pitch Algorithm" } },
    { "algo.stretch",    { "拉伸算法", "拉伸演算法", "タイムアルゴリズム", "타임 알고리즘", "Stretch Algorithm" } },
    { "track.compose",   { "旋律", "旋律", "メロディック", "멜로디", "Compose" } },
    { "track.toggleCompose", { "切换旋律/普通音轨", "切換旋律/一般音軌", "メロディック/通常を切替", "멜로디/일반 전환", "Toggle Compose/Audio" } },
    { "track.delete",    { "删除所选轨道", "刪除所選軌道", "選択トラックを削除", "선택 트랙 삭제", "Delete Selected Track" } },
    { "clip.delete",     { "删除所选采样", "刪除所選取樣", "選択クリップを削除", "선택 클립 삭제", "Delete Selected Clip" } },
    { "track.audio",     { "普通音轨", "一般音軌", "通常トラック", "일반 트랙", "Audio Track" } },
    { "track.mute",      { "静音", "靜音", "ミュート", "음소거", "Mute" } },
    { "track.volume",    { "音量", "音量", "音量", "음량", "Vol" } },
    { "status.ready",    { "就绪", "就緒", "準備完了", "준비됨", "Ready" } },
    { "status.loading",  { "正在加载…", "正在載入…", "読み込み中…", "불러오는 중…", "Loading…" } },
    { "status.rendering", { "正在预渲染…", "正在預先算繪…", "プリレンダリング中…", "사전 렌더링 중…", "Pre-rendering…" } },
    { "status.analyzing", { "正在分析原始音高…", "正在分析原始音高…", "元ピッチを解析中…", "원본 피치 분석 중…", "Analysing source pitch…" } },
    { "status.analysisComplete", { "音高与音符分析完成", "音高與音符分析完成", "ピッチとノートの解析が完了しました", "피치 및 노트 분석 완료", "Pitch and note analysis complete" } },
    { "status.analysisSkipped", { "已保留现有音符数据", "已保留現有音符資料", "既存のノートデータを保持しました", "기존 노트 데이터를 유지했습니다", "Existing note data preserved" } },
    { "status.exporting", { "正在导出 WAV…", "正在匯出 WAV…", "WAVを書き出しています…", "WAV 내보내는 중…", "Exporting WAV…" } },
    { "status.midiPending", { "MIDI 导入器将在下一阶段接入", "MIDI 匯入器將於下一階段接入", "MIDIインポーターは次段階で接続します", "MIDI 가져오기는 다음 단계에서 연결됩니다", "MIDI importer will be connected in the next stage" } },
    { "status.noTracks", { "导入音频或工程以开始", "匯入音訊或工程以開始", "音声またはプロジェクトを読み込んでください", "오디오 또는 프로젝트를 가져오세요", "Import audio or a project to begin" } },
    { "edit.source",     { "原始采样编辑：此模式不允许拉伸", "原始取樣編輯：此模式不允許拉伸", "元サンプル編集：このモードではストレッチできません", "원본 샘플 편집: 이 모드에서는 늘이기를 사용할 수 없습니다", "Original sample edit: stretching is disabled" } },
    { "error.audio",     { "音频文件读取失败", "音訊檔案讀取失敗", "オーディオを読み込めません", "오디오 파일을 읽지 못했습니다", "Could not read audio file" } },
    { "error.midi",      { "MIDI 导入失败", "MIDI 匯入失敗", "MIDIの読み込みに失敗しました", "MIDI 가져오기에 실패했습니다", "MIDI import failed" } },
    { "error.mpd",       { "Melodyne 工程读取失败", "Melodyne 工程讀取失敗", "Melodyneプロジェクトを読み込めません", "Melodyne 프로젝트를 읽지 못했습니다", "Could not read Melodyne project" } },
    { "error.export",    { "音频导出失败", "音訊匯出失敗", "オーディオの書き出しに失敗しました", "오디오 내보내기 실패", "Audio export failed" } },
    { "warning.missingMedia", { "以下素材未找到", "找不到以下素材", "次の素材が見つかりません", "다음 미디어를 찾지 못했습니다", "The following media files were not found" } },
    { "mpd.stage.open", { "打开工程", "開啟工程", "プロジェクトを開く", "프로젝트 열기", "Opening project" } },
    { "mpd.stage.scan_container", { "扫描工程容器", "掃描工程容器", "コンテナを走査", "프로젝트 컨테이너 검사", "Scanning container" } },
    { "mpd.stage.decompress_graph", { "解压工程数据", "解壓工程資料", "データを展開", "프로젝트 데이터 압축 해제", "Decompressing graph" } },
    { "mpd.stage.read_tracks", { "读取轨道和 BPM", "讀取軌道與 BPM", "トラックとBPMを読込", "트랙 및 BPM 읽기", "Reading tracks and BPM" } },
    { "mpd.stage.create_tracks", { "恢复音符和编辑", "還原音符與編輯", "ノートと編集を復元", "노트 및 편집 복원", "Restoring notes and edits" } },
    { "mpd.stage.complete", { "完成", "完成", "完了", "완료", "Complete" } },
    { "mpd.compose.title", { "选择 Compose 轨道", "選擇 Compose 軌道", "Composeトラックを選択", "Compose 트랙 선택", "Choose Compose Tracks" } },
    { "mpd.compose.description", { "勾选需要恢复 Melodyne 音符和修音的旋律轨道；其余轨道按普通音频播放。", "勾選需要還原 Melodyne 音符與修音的旋律軌道；其餘軌道作為一般音訊播放。", "Melodyneのノート編集を復元する旋律トラックを選択します。その他は通常の音声トラックとして扱います。", "Melodyne 노트 편집을 복원할 멜로디 트랙을 선택하세요. 나머지는 일반 오디오 트랙으로 처리됩니다.", "Select melodic tracks whose Melodyne note edits should be restored. Other tracks remain regular audio tracks." } },
    { "dialog.import", { "导入", "匯入", "読み込む", "가져오기", "Import" } },
    { "dialog.cancel", { "取消", "取消", "キャンセル", "취소", "Cancel" } }
    ,{ "dialog.delete", { "删除", "刪除", "削除", "삭제", "Delete" } }
    ,{ "dialog.destructiveMessage", { "此操作会删除所选内容，是否继续？", "此操作會刪除所選內容，是否繼續？", "選択した内容を削除します。続行しますか？", "선택한 내용을 삭제합니다. 계속할까요?", "The selected content will be deleted. Continue?" } }
    ,{ "dialog.apply", { "应用", "套用", "適用", "적용", "Apply" } }
    ,{ "settings.title", { "设置", "設定", "設定", "설정", "Settings" } }
    ,{ "settings.interface", { "界面", "介面", "インターフェース", "인터페이스", "Interface" } }
    ,{ "settings.audio", { "音频", "音訊", "オーディオ", "오디오", "Audio" } }
    ,{ "settings.audioDevice", { "当前音频设备", "目前音訊裝置", "現在のオーディオデバイス", "현재 오디오 장치", "Current Audio Device" } }
    ,{ "settings.sampleRate", { "采样率", "取樣率", "サンプルレート", "샘플 레이트", "Sample Rate" } }
    ,{ "settings.bufferSize", { "缓冲区大小", "緩衝區大小", "バッファサイズ", "버퍼 크기", "Buffer Size" } }
    ,{ "settings.advancedAudio", { "选择输入、输出和驱动…", "選擇輸入、輸出與驅動…", "入出力とドライバーを選択…", "입출력 및 드라이버 선택…", "Choose Inputs, Outputs and Driver…" } }
    ,{ "settings.noAudioDevice", { "未选择音频设备", "尚未選擇音訊裝置", "オーディオデバイス未選択", "오디오 장치가 선택되지 않음", "No audio device selected" } }
    ,{ "settings.algorithm", { "算法", "演算法", "アルゴリズム", "알고리즘", "Algorithms" } }
    ,{ "settings.operation", { "操作", "操作", "操作", "조작", "Operations" } }
    ,{ "settings.import", { "文件导入", "檔案匯入", "ファイル読込", "파일 가져오기", "File Import" } }
    ,{ "settings.language", { "语言", "語言", "言語", "언어", "Language" } }
    ,{ "settings.theme", { "颜色主题", "色彩主題", "カラーテーマ", "색상 테마", "Colour Theme" } }
    ,{ "settings.accent", { "主色（Hex）", "主色（Hex）", "アクセント（Hex）", "강조색 (Hex)", "Accent (Hex)" } }
    ,{ "settings.accentLight", { "浅主色（Hex）", "淺主色（Hex）", "明るい主色（Hex）", "밝은 강조색 (Hex)", "Light Accent (Hex)" } }
    ,{ "settings.noteColour", { "音符色（Hex）", "音符色（Hex）", "ノート色（Hex）", "노트 색상 (Hex)", "Note Colour (Hex)" } }
    ,{ "settings.gamePath", { "GAME 模型目录", "GAME 模型目錄", "GAMEモデルフォルダー", "GAME 모델 폴더", "GAME Model Directory" } }
    ,{ "settings.gameModel", { "GAME 默认模型", "GAME 預設模型", "GAME既定モデル", "GAME 기본 모델", "Default GAME Model" } }
    ,{ "settings.hifiganPath", { "HiFi-GAN 模型目录", "HiFi-GAN 模型目錄", "HiFi-GANモデルフォルダー", "HiFi-GAN 모델 폴더", "HiFi-GAN Model Directory" } }
    ,{ "settings.inference", { "推理方式", "推理方式", "推論バックエンド", "추론 백엔드", "Inference Backend" } }
    ,{ "settings.device", { "推理设备", "推理裝置", "推論デバイス", "추론 장치", "Inference Device" } }
    ,{ "settings.utauResampler", { "UTAU 重采样器（预留）", "UTAU 重取樣器（預留）", "UTAUリサンプラー（予約）", "UTAU 리샘플러 (예약)", "UTAU Resampler (reserved)" } }
    ,{ "settings.shortcuts", { "快捷键方案", "快速鍵配置", "ショートカット方式", "단축키 방식", "Shortcut Scheme" } }
    ,{ "settings.wheel", { "鼠标滚轮", "滑鼠滾輪", "マウスホイール", "마우스 휠", "Mouse Wheel" } }
    ,{ "settings.spacePlayback", { "空格键播放/暂停", "空白鍵播放/暫停", "スペースで再生/一時停止", "스페이스바 재생/일시정지", "Space toggles playback" } }
    ,{ "settings.confirmDestructive", { "删除前确认", "刪除前確認", "削除前に確認", "삭제 전 확인", "Confirm before delete" } }
    ,{ "settings.melodyneCompose", { "Melodyne Compose 默认方式", "Melodyne Compose 預設方式", "Melodyne Composeの既定値", "Melodyne Compose 기본값", "Default Melodyne Compose" } }
    ,{ "settings.melodynePitch", { "Melodyne 原音高来源", "Melodyne 原音高來源", "Melodyne元ピッチの取得元", "Melodyne 원본 피치 소스", "Melodyne Source Pitch" } }
    ,{ "settings.importAlgorithm", { "导入工程默认算法", "匯入工程預設演算法", "読込時の既定アルゴリズム", "가져오기 기본 알고리즘", "Default Import Algorithm" } }
    ,{ "settings.preserveEdits", { "保留工程中的音符、分界、Attack、音量和音色编辑", "保留工程中的音符、分界、Attack、音量與音色編輯", "ノート・境界・Attack・音量・音色の編集を保持", "노트·경계·Attack·음량·음색 편집 유지", "Preserve notes, boundaries, Attack, level and timbre edits" } }
    ,{ "settings.recursiveMedia", { "递归查找缺失素材", "遞迴尋找遺失素材", "不足素材を再帰検索", "누락 미디어 재귀 검색", "Search recursively for missing media" } }
    ,{ "sample.alias", { "别名", "別名", "エイリアス", "별칭", "Alias" } }
    ,{ "sample.start", { "起点", "起點", "開始", "시작", "Start" } }
    ,{ "sample.end", { "终点", "終點", "終了", "끝", "End" } }
    ,{ "sample.alignment", { "对齐", "對齊", "整列", "정렬", "Align" } }
    ,{ "sample.fixed", { "辅音", "子音", "子音", "자음", "Fixed" } }
    ,{ "sample.save", { "保存设定", "儲存設定", "設定を保存", "설정 저장", "Save Settings" } }
    ,{ "sample.saved", { "音频设定已保存", "音訊設定已儲存", "音声設定を保存しました", "오디오 설정 저장됨", "Audio settings saved" } }
    ,{ "sample.importOto", { "读取 oto", "讀取 oto", "oto読込", "oto 읽기", "Import oto" } }
    ,{ "sample.exportOto", { "导出 oto", "匯出 oto", "oto書出", "oto 내보내기", "Export oto" } }
    ,{ "asset.title", { "素材管理器", "素材管理器", "素材マネージャー", "소재 관리자", "Asset Manager" } }
    ,{ "asset.register", { "注册音频素材", "註冊音訊素材", "音声素材を登録", "오디오 소재 등록", "Register Audio Assets" } }
    ,{ "asset.utau", { "导入 UTAU 音源库", "匯入 UTAU 音源庫", "UTAU 音源を読み込む", "UTAU 음원 가져오기", "Import UTAU Voicebank" } }
    ,{ "asset.utauDone", { "已注册 {files} 个音频，生成 {sidecars} 个 HJM 文件、{regions} 个分段。", "已註冊 {files} 個音訊，產生 {sidecars} 個 HJM 檔案、{regions} 個分段。", "{files} 件の音声を登録し、{sidecars} 件の HJM と {regions} 件の区間を作成しました。", "오디오 {files}개를 등록하고 HJM {sidecars}개와 구간 {regions}개를 생성했습니다.", "Registered {files} audio files and generated {sidecars} HJM files with {regions} regions." } }
    ,{ "asset.remove", { "移除", "移除", "削除", "제거", "Remove" } }
    ,{ "asset.empty", { "把音频拖入此处注册；注册后可拖到时间线或钢琴卷帘。", "將音訊拖入此處註冊；註冊後可拖到時間軸或鋼琴捲簾。", "音声をここへドロップして登録し、タイムラインまたはピアノロールへドラッグできます。", "오디오를 여기에 놓아 등록한 뒤 타임라인이나 피아노 롤로 끌 수 있습니다.", "Drop audio here to register it, then drag it to the timeline or piano roll." } }
};
}

I18n::I18n()
{
    const auto locale = juce::SystemStats::getUserLanguage().toLowerCase();
    if (locale.startsWith("ja")) language = Language::jaJP;
    else if (locale.startsWith("ko")) language = Language::koKR;
    else if (locale.contains("tw") || locale.contains("hk") || locale.contains("hant")) language = Language::zhTW;
    else if (!locale.startsWith("zh")) language = Language::enUS;
}

juce::String I18n::text(const juce::String& key) const
{
    const auto found = strings.find(key.toStdString());
    if (found == strings.end()) return key;
    return juce::String::fromUTF8(found->second[static_cast<std::size_t>(language)]);
}
}
