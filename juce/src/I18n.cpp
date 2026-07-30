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
    { "param.pitch",     { "音高", "音高", "ピッチ", "피치", "Pitch" } },
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
    { "track.audio",     { "普通音轨", "一般音軌", "通常トラック", "일반 트랙", "Audio Track" } },
    { "track.mute",      { "静音", "靜音", "ミュート", "음소거", "Mute" } },
    { "track.volume",    { "音量", "音量", "音量", "음량", "Vol" } },
    { "status.ready",    { "就绪", "就緒", "準備完了", "준비됨", "Ready" } },
    { "status.loading",  { "正在加载…", "正在載入…", "読み込み中…", "불러오는 중…", "Loading…" } },
    { "status.rendering", { "正在预渲染…", "正在預先算繪…", "プリレンダリング中…", "사전 렌더링 중…", "Pre-rendering…" } },
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
