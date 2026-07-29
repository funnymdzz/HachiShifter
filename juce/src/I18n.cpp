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
    { "file.audio",      { "导入音频", "匯入音訊", "オーディオを読み込む", "오디오 가져오기", "Import Audio" } },
    { "file.melodyne",   { "导入 Melodyne", "匯入 Melodyne", "Melodyneを読み込む", "Melodyne 가져오기", "Import Melodyne" } },
    { "transport.play",  { "播放", "播放", "再生", "재생", "Play" } },
    { "transport.stop",  { "停止", "停止", "停止", "정지", "Stop" } },
    { "tool.main",       { "音符编辑", "音符編輯", "ノート編集", "노트 편집", "Note Edit" } },
    { "tool.wrench",     { "采样精修", "取樣精修", "サンプル編集", "샘플 정밀 편집", "Sample Edit" } },
    { "algo.pitch",      { "变调算法", "變調演算法", "ピッチアルゴリズム", "피치 알고리즘", "Pitch Algorithm" } },
    { "algo.stretch",    { "拉伸算法", "拉伸演算法", "タイムアルゴリズム", "타임 알고리즘", "Stretch Algorithm" } },
    { "track.compose",   { "旋律", "旋律", "メロディック", "멜로디", "Compose" } },
    { "track.audio",     { "普通音轨", "一般音軌", "通常トラック", "일반 트랙", "Audio Track" } },
    { "track.mute",      { "静音", "靜音", "ミュート", "음소거", "Mute" } },
    { "status.ready",    { "就绪", "就緒", "準備完了", "준비됨", "Ready" } },
    { "status.loading",  { "正在加载…", "正在載入…", "読み込み中…", "불러오는 중…", "Loading…" } },
    { "status.noTracks", { "导入音频或工程以开始", "匯入音訊或工程以開始", "音声またはプロジェクトを読み込んでください", "오디오 또는 프로젝트를 가져오세요", "Import audio or a project to begin" } },
    { "edit.source",     { "原始采样编辑：此模式不允许拉伸", "原始取樣編輯：此模式不允許拉伸", "元サンプル編集：このモードではストレッチできません", "원본 샘플 편집: 이 모드에서는 늘이기를 사용할 수 없습니다", "Original sample edit: stretching is disabled" } },
    { "error.audio",     { "音频文件读取失败", "音訊檔案讀取失敗", "オーディオを読み込めません", "오디오 파일을 읽지 못했습니다", "Could not read audio file" } },
    { "error.mpd",       { "Melodyne 导入器正在迁移到 JUCE 核心", "Melodyne 匯入器正在移植到 JUCE 核心", "MelodyneインポーターをJUCEコアへ移植中です", "Melodyne 가져오기 기능을 JUCE 코어로 이전 중입니다", "Melodyne importer migration is in progress" } }
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

