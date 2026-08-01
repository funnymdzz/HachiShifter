#include "McpServer.h"
#include "AnalysisService.h"
#include <cmath>
#include <iostream>

namespace hachi::backend
{
namespace
{
juce::var object()
{
    return juce::var(new juce::DynamicObject());
}

void set(juce::var& value, const char* key, juce::var property)
{
    if (auto* dynamic = value.getDynamicObject()) dynamic->setProperty(key, std::move(property));
}

juce::var array(std::vector<juce::var> values)
{
    juce::Array<juce::var> result;
    result.ensureStorageAllocated(static_cast<int>(values.size()));
    for (auto& value : values) result.add(std::move(value));
    return juce::var(std::move(result));
}

juce::var permissiveSchema()
{
    auto schema = object();
    set(schema, "type", "object");
    set(schema, "additionalProperties", true);
    return schema;
}

juce::var makeTool(const char* name, const char* description)
{
    auto tool = object();
    set(tool, "name", name);
    set(tool, "description", juce::String::fromUTF8(description));
    set(tool, "inputSchema", permissiveSchema());
    return tool;
}

double number(const juce::var& args, const char* key, double fallback = 0.0)
{
    const auto value = args.getProperty(key, fallback);
    return value.isDouble() || value.isInt() || value.isInt64() ? static_cast<double>(value) : fallback;
}

juce::String string(const juce::var& args, const char* key)
{
    return args.getProperty(key, {}).toString();
}

AnalysisConfig analysisConfig(const juce::var& args)
{
    auto config = AnalysisService::configFromEnvironment();
    if (const auto path = string(args, "game_model_dir"); path.isNotEmpty())
        config.gameModelDirectory = juce::File(path);
    if (const auto path = string(args, "fcpe_model"); path.isNotEmpty())
        config.fcpeModelPath = juce::File(path);
    const auto variant = string(args, "game_model").toLowerCase();
    if (variant.isNotEmpty()) config.performanceMode = variant == "small";
    const auto inference = string(args, "inference").toLowerCase();
    if (inference.isNotEmpty())
        config.inference = inference == "cpu" ? InferenceBackend::cpu
            : inference == "directml" ? InferenceBackend::directML
            : inference == "cuda" ? InferenceBackend::cuda
            : inference == "coreml" ? InferenceBackend::coreML
            : InferenceBackend::automatic;
    if (args.hasProperty("device_index"))
        config.deviceIndex = static_cast<int>(number(args, "device_index", -1.0));
    return config;
}

juce::String analysisSummary(const AnalysisStatus& status)
{
    return "requested=" + status.requestedBackend
        + "; active=" + AnalysisService::backendText(status)
        + "; game_variant=" + (status.performanceMode ? "small" : "large")
        + "; game_ready=" + juce::String(status.gameModelReady ? 1 : 0)
        + "; game_path=" + status.gameModelDirectory.getFullPathName()
        + "; fcpe_ready=" + juce::String(status.fcpeModelReady ? 1 : 0)
        + "; fcpe_path=" + status.fcpeModelPath.getFullPathName()
        + "; onnx_runtime=" + juce::String(status.onnxRuntimeReady ? 1 : 0)
        + "; message=" + status.message;
}

juce::String pitchAlgorithmText(PitchAlgorithm value)
{
    if (value == PitchAlgorithm::nsfHifigan) return "nsf-hifigan";
    if (value == PitchAlgorithm::world) return "world";
    if (value == PitchAlgorithm::vocalShifter) return "vslib";
    return "mld5";
}

juce::String stretchAlgorithmText(StretchAlgorithm value)
{
    if (value == StretchAlgorithm::variableMelHop) return "variable-mel-hop";
    if (value == StretchAlgorithm::loop) return "loop";
    if (value == StretchAlgorithm::soundTouch) return "soundtouch";
    return "melodyne-hybrid";
}

juce::String summary(const ProjectData& project)
{
    std::size_t clips = 0;
    std::size_t notes = 0;
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
        {
            ++clips;
            notes += clip.notes.size();
        }
    return "project=" + project.name + "; bpm=" + juce::String(project.bpm)
        + "; tracks=" + juce::String(static_cast<juce::int64>(project.tracks.size()))
        + "; clips=" + juce::String(static_cast<juce::int64>(clips))
        + "; notes=" + juce::String(static_cast<juce::int64>(notes));
}

juce::var sampleRowsJson(const std::vector<SampleRegionSetting>& rows)
{
    std::vector<juce::var> values;
    values.reserve(rows.size());
    for (const auto& row : rows)
    {
        auto value = object();
        set(value, "name", row.name);
        set(value, "region_start_seconds", row.regionStartSeconds);
        set(value, "region_end_seconds", row.regionEndSeconds);
        set(value, "alignment_seconds", row.alignmentSeconds);
        set(value, "fixed_duration_seconds", row.fixedDurationSeconds);
        set(value, "relative_pitch_cents", row.relativePitchCents);
        set(value, "melodyne_data", row.melodyneData);
        set(value, "melodyne_pitch_center_cents", row.melodynePitchCenterCents);
        set(value, "melodyne_original_pitch_center_cents",
            row.melodyneOriginalPitchCenterCents);
        set(value, "melodyne_pitch_drift", row.melodynePitchDrift);
        set(value, "melodyne_pitch_modulation", row.melodynePitchModulation);
        set(value, "melodyne_transition_seconds", row.melodyneTransitionSeconds);
        set(value, "melodyne_formant_cents", row.melodyneFormantCents);
        set(value, "melodyne_amplitude", row.melodyneAmplitude);
        set(value, "melodyne_sibilant_balance", row.melodyneSibilantBalance);
        set(value, "melodyne_attack_seconds", row.melodyneAttackSeconds);
        set(value, "melodyne_decay_elongation", row.melodyneDecayElongation);
        values.push_back(std::move(value));
    }
    return array(std::move(values));
}

std::vector<SampleRegionSetting> sampleRowsFromJson(const juce::var& source)
{
    std::vector<SampleRegionSetting> rows;
    if (const auto* values = source.getArray())
        for (const auto& value : *values)
        {
            SampleRegionSetting row;
            row.name = string(value, "name");
            row.regionStartSeconds = number(value, "region_start_seconds");
            row.regionEndSeconds = number(value, "region_end_seconds", 0.5);
            row.alignmentSeconds = number(value, "alignment_seconds");
            row.fixedDurationSeconds = number(value, "fixed_duration_seconds");
            row.relativePitchCents = number(value, "relative_pitch_cents");
            row.melodyneData = static_cast<bool>(value.getProperty("melodyne_data", false));
            row.melodynePitchCenterCents = number(value, "melodyne_pitch_center_cents");
            row.melodyneOriginalPitchCenterCents = number(
                value, "melodyne_original_pitch_center_cents");
            row.melodynePitchDrift = number(value, "melodyne_pitch_drift", 1.0);
            row.melodynePitchModulation = number(value, "melodyne_pitch_modulation", 1.0);
            row.melodyneTransitionSeconds = number(value, "melodyne_transition_seconds");
            row.melodyneFormantCents = number(value, "melodyne_formant_cents");
            row.melodyneAmplitude = number(value, "melodyne_amplitude", 1.0);
            row.melodyneSibilantBalance = number(value, "melodyne_sibilant_balance");
            row.melodyneAttackSeconds = number(value, "melodyne_attack_seconds");
            row.melodyneDecayElongation = number(value, "melodyne_decay_elongation");
            if (row.regionEndSeconds > row.regionStartSeconds)
                rows.push_back(std::move(row));
        }
    return rows;
}
}

McpServer::McpServer()
    : audio(std::make_unique<AudioEngine>())
{
    formats.registerBasicFormats();
}

int McpServer::run()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty()) continue;
        const auto request = juce::JSON::parse(juce::String::fromUTF8(line.data(),
                                                                       static_cast<int>(line.size())));
        bool shouldRespond = true;
        auto response = handle(request, shouldRespond);
        if (shouldRespond)
        {
            std::cout << juce::JSON::toString(response, true).toStdString() << '\n';
            std::cout.flush();
        }
    }
    return 0;
}

juce::var McpServer::handle(const juce::var& request, bool& shouldRespond)
{
    shouldRespond = request.hasProperty("id");
    const auto id = request.getProperty("id", {});
    const auto method = request.getProperty("method", {}).toString();
    auto response = object();
    set(response, "jsonrpc", "2.0");
    set(response, "id", id);
    if (method == "initialize")
    {
        auto result = object();
        set(result, "protocolVersion", "2025-06-18");
        auto capabilities = object();
        set(capabilities, "tools", object());
        set(capabilities, "resources", object());
        set(result, "capabilities", capabilities);
        auto serverInfo = object();
        set(serverInfo, "name", "hachishifter-next");
        set(serverInfo, "version", JUCE_APPLICATION_VERSION_STRING);
        set(result, "serverInfo", serverInfo);
        set(response, "result", result);
        return response;
    }
    if (method == "ping")
    {
        set(response, "result", object());
        return response;
    }
    if (method == "tools/list")
    {
        auto result = object();
        set(result, "tools", array({
            makeTool("project_new", "Create an empty HachiShifter project / 新建工程"),
            makeTool("project_open", "Open HSPX, MPD, MIDI, or audio from path / 打开工程或素材"),
            makeTool("project_save", "Save current project as HSPX / 保存工程"),
            makeTool("project_snapshot", "Read every current track, clip, note, pitch and marker / 读取全部工程内容"),
            makeTool("import_audio", "Import an audio file at start_seconds / 导入音频"),
            makeTool("analyse_audio", "Run configured GAME+FCPE analysis with native fallback and return actual backend / 执行 GAME+FCPE 分析并报告实际后端"),
            makeTool("analysis_status", "Inspect GAME large/small, FCPE and inference availability / 查看 GAME、FCPE 与推理状态"),
            makeTool("import_midi", "Import MIDI notes and tempo / 导入 MIDI"),
            makeTool("import_melodyne", "Import Melodyne MPD edits; recursive_media, preserve_edits and source_pitch control import / 导入 Melodyne 工程并控制素材搜索、工程编辑与原始 F0"),
            makeTool("set_tempo", "Set BPM and time signature / 设置速度与拍号"),
            makeTool("set_track", "Set compose, mute, solo, gain, pan and render algorithms / 设置轨道及算法"),
            makeTool("set_clip", "Set clip gain, fades and mute state / 设置采样增益、淡入淡出和静音"),
            makeTool("move_clip", "Move a clip on the timeline / 移动采样"),
            makeTool("resize_clip", "Stretch a whole clip while preserving its source media / 整体拉伸采样并保留原始素材"),
            makeTool("transpose_note", "Move a note and its whole contour / 整体移动音高线"),
            makeTool("resize_note", "Change note time bounds / 修改音符时间"),
            makeTool("set_note", "Set pitch, tension, breath, formant, gain, Attack and consonant parameters / 设置全部音符参数"),
            makeTool("set_pitch_curve", "Draw an absolute target-pitch curve without replacing source F0 / 绘制目标音高线并保留原始 F0"),
            makeTool("add_note", "Create a note in a clip / 在采样中创建音符"),
            makeTool("remove_note", "Delete a note / 删除音符"),
            makeTool("toggle_note_connection", "Connect or separate adjacent notes / 连接或分离相邻音符"),
            makeTool("remove_clip", "Delete a clip / 删除采样"),
            makeTool("remove_track", "Delete a track / 删除轨道"),
            makeTool("undo", "Undo the last project edit / 撤销工程编辑"),
            makeTool("redo", "Redo the last project edit / 重做工程编辑"),
            makeTool("render_prepare", "Pre-render the current project with its selected algorithms / 按当前所选算法预渲染工程"),
            makeTool("render_status", "Read pre-render progress and active backends / 读取预渲染进度与实际后端"),
            makeTool("export_wav", "Render and export the current project to WAV / 渲染并导出当前工程为 WAV"),
            makeTool("transport_play", "Render if needed and start transport playback / 必要时预渲染并开始播放"),
            makeTool("transport_stop", "Stop transport playback / 停止播放"),
            makeTool("transport_seek", "Seek transport to position_seconds / 跳转播放位置"),
            makeTool("transport_status", "Read playback position and render state / 读取播放位置与渲染状态"),
            makeTool("sample_settings_read", "Read or derive audio .hjm.csv regions / 读取或生成音频 .hjm.csv 分段"),
            makeTool("sample_settings_save", "Save audio regions to .hjm.csv / 保存音频分段到 .hjm.csv"),
            makeTool("oto_import", "Import one audio file's regions from UTAU oto.ini / 从 UTAU oto.ini 导入单个音频分段"),
            makeTool("oto_export", "Export one audio file's regions to UTAU oto.ini / 将单个音频分段导出为 UTAU oto.ini"),
            makeTool("voicebank_import", "Import an UTAU voicebank and create .hjm.csv sidecars / 导入 UTAU 音源并生成 .hjm.csv"),
            makeTool("read_file", "Read a byte range as base64 / 读取任意文件内容"),
            makeTool("list_directory", "List a directory with type and size / 列出目录内容")
        }));
        set(response, "result", result);
        return response;
    }
    if (method == "tools/call")
    {
        const auto params = request.getProperty("params", {});
        set(response, "result", callTool(string(params, "name"),
                                          params.getProperty("arguments", object())));
        return response;
    }
    if (method == "resources/list")
    {
        auto resource = object();
        set(resource, "uri", "hachishifter://project/current");
        set(resource, "name", "Current HachiShifter project");
        set(resource, "mimeType", "application/json");
        auto result = object();
        set(result, "resources", array({ resource }));
        set(response, "result", result);
        return response;
    }
    if (method == "resources/read")
    {
        const auto params = request.getProperty("params", {});
        if (string(params, "uri") != "hachishifter://project/current")
            return errorResponse(id, -32002, "Unknown resource");
        auto content = object();
        set(content, "uri", "hachishifter://project/current");
        set(content, "mimeType", "application/json");
        set(content, "text", juce::JSON::toString(projectJson(), false));
        auto result = object();
        set(result, "contents", array({ content }));
        set(response, "result", result);
        return response;
    }
    if (!shouldRespond) return {};
    return errorResponse(id, -32601, "Method not found");
}

juce::var McpServer::callTool(const juce::String& name, const juce::var& args)
{
    juce::String error;
    if (name == "project_new")
    {
        project.clear();
        return toolResult("ok");
    }
    if (name == "project_snapshot")
        return toolResult(juce::JSON::toString(projectJson(), false));
    if (name == "project_save")
    {
        const juce::File file(string(args, "path"));
        if (project.save(file, error)) return toolResult("saved=" + file.getFullPathName());
    }
    else if (name == "project_open")
    {
        const juce::File file(string(args, "path"));
        if (file.hasFileExtension("mpd"))
        {
            if (auto imported = MelodyneImporter::importProject(file, error))
            {
                project.replace(std::move(imported->project));
                return toolResult(summary(project.snapshot()));
            }
        }
        else if (file.hasFileExtension("mid;midi"))
        {
            project.clear();
            if (project.addMidiFile(file, error)) return toolResult(summary(project.snapshot()));
        }
        else if (file.hasFileExtension("hspx"))
        {
            if (project.load(file, error))
                return toolResult(summary(project.snapshot())
                    + (error.isNotEmpty() ? "; missing_media=" + error : juce::String()));
        }
        else if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file)))
        {
            project.clear();
            const auto clipId = project.addAudioFile(file,
                static_cast<double>(reader->lengthInSamples) / reader->sampleRate);
            auto analysis = AnalysisService::analyse(file, analysisConfig(args), error);
            (void) project.setClipNotesIfEmpty(clipId, std::move(analysis.notes));
            return toolResult(summary(project.snapshot()));
        }
        else error = "Unsupported or unreadable file";
    }
    else if (name == "import_audio")
    {
        const juce::File file(string(args, "path"));
        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file)))
        {
            const auto clipId = project.addAudioFile(file,
                static_cast<double>(reader->lengthInSamples) / reader->sampleRate,
                number(args, "start_seconds"));
            auto analysis = AnalysisService::analyse(file, analysisConfig(args), error);
            const auto backend = AnalysisService::backendText(analysis.status);
            const auto noteCount = analysis.notes.size();
            (void) project.setClipNotesIfEmpty(clipId, std::move(analysis.notes));
            return toolResult("ok; backend=" + backend + "; notes="
                + juce::String(static_cast<juce::int64>(noteCount))
                + (analysis.warning.isNotEmpty() ? "; warning=" + analysis.warning
                                                  : juce::String()));
        }
        error = "Audio read failed";
    }
    else if (name == "analysis_status")
        return toolResult(analysisSummary(AnalysisService::status(analysisConfig(args))));
    else if (name == "analyse_audio")
    {
        const juce::File file(string(args, "path"));
        auto analysis = AnalysisService::analyse(file, analysisConfig(args), error);
        if (!analysis.notes.empty())
            return toolResult(analysisSummary(analysis.status) + "; notes="
                + juce::String(static_cast<juce::int64>(analysis.notes.size()))
                + (analysis.warning.isNotEmpty() ? "; warning=" + analysis.warning
                                                  : juce::String()));
    }
    else if (name == "import_midi")
    {
        if (project.addMidiFile(juce::File(string(args, "path")), error)) return toolResult("ok");
    }
    else if (name == "import_melodyne")
    {
        MelodyneImportOptions options;
        options.recursiveMediaSearch = static_cast<bool>(
            args.getProperty("recursive_media", true));
        options.preserveProjectEdits = static_cast<bool>(
            args.getProperty("preserve_edits", true));
        if (auto imported = MelodyneImporter::importProject(
                juce::File(string(args, "path")), error, {}, options))
        {
            const auto sourcePitch = string(args, "source_pitch").toLowerCase();
            if (sourcePitch == "native" || sourcePitch == "reanalyze"
                || sourcePitch == "reanalyse" || sourcePitch == "game+fcpe"
                || sourcePitch == "game_fcpe")
            {
                juce::String pitchError;
                (void) AnalysisService::reanalyseProjectSourcePitch(
                    imported->project, analysisConfig(args), pitchError);
            }
            project.replace(std::move(imported->project));
            return toolResult(summary(project.snapshot()));
        }
    }
    else if (name == "set_tempo")
    {
        project.setTempo(number(args, "bpm", 120.0), static_cast<int>(number(args, "numerator", 4.0)),
                         static_cast<int>(number(args, "denominator", 4.0)));
        return toolResult("ok");
    }
    else if (name == "set_track")
    {
        const auto id = string(args, "track_id");
        if (args.hasProperty("compose")) project.setTrackCompose(id, static_cast<bool>(args["compose"]));
        if (args.hasProperty("muted")) project.setTrackMuted(id, static_cast<bool>(args["muted"]));
        if (args.hasProperty("solo")) project.setTrackSolo(id, static_cast<bool>(args["solo"]));
        if (args.hasProperty("volume")) project.setTrackVolume(id, static_cast<float>(number(args, "volume", 1.0)));
        if (args.hasProperty("pan")) project.setTrackPan(id, static_cast<float>(number(args, "pan")));
        if (args.hasProperty("pitch_algorithm"))
        {
            const auto value = string(args, "pitch_algorithm").toLowerCase();
            project.setTrackPitchAlgorithm(id,
                value == "nsf-hifigan" ? PitchAlgorithm::nsfHifigan
                : value == "world" ? PitchAlgorithm::world
                : value == "vslib" ? PitchAlgorithm::vocalShifter : PitchAlgorithm::mld5);
        }
        if (args.hasProperty("stretch_algorithm"))
        {
            const auto value = string(args, "stretch_algorithm").toLowerCase();
            project.setTrackStretchAlgorithm(id,
                value == "variable-mel-hop" ? StretchAlgorithm::variableMelHop
                : value == "loop" ? StretchAlgorithm::loop
                : value == "soundtouch" ? StretchAlgorithm::soundTouch
                : StretchAlgorithm::melodyneHybrid);
        }
        return toolResult("ok");
    }
    else if (name == "set_clip")
    {
        const auto id = string(args, "clip_id");
        if (args.hasProperty("gain"))
            project.setClipGain(id, static_cast<float>(number(args, "gain", 1.0)));
        if (args.hasProperty("fade_in_seconds") || args.hasProperty("fade_out_seconds"))
        {
            auto fadeIn = number(args, "fade_in_seconds");
            auto fadeOut = number(args, "fade_out_seconds");
            const auto data = project.snapshot();
            for (const auto& track : data.tracks)
                for (const auto& clip : track.clips)
                    if (clip.id == id)
                    {
                        if (!args.hasProperty("fade_in_seconds")) fadeIn = clip.fadeInSeconds;
                        if (!args.hasProperty("fade_out_seconds")) fadeOut = clip.fadeOutSeconds;
                    }
            project.setClipFades(id, fadeIn, fadeOut);
        }
        if (args.hasProperty("muted"))
            project.setClipMuted(id, static_cast<bool>(args["muted"]));
        return toolResult("ok");
    }
    else if (name == "move_clip")
    {
        project.moveClip(string(args, "clip_id"), number(args, "start_seconds"));
        return toolResult("ok");
    }
    else if (name == "resize_clip")
    {
        project.resizeClip(string(args, "clip_id"), number(args, "start_seconds"),
                           number(args, "duration_seconds", 0.25));
        return toolResult("ok");
    }
    else if (name == "transpose_note")
    {
        project.transposeNote(string(args, "note_id"), static_cast<float>(number(args, "semitones")));
        return toolResult("ok");
    }
    else if (name == "resize_note")
    {
        project.resizeNote(string(args, "note_id"), number(args, "start_seconds"),
                           number(args, "duration_seconds", 0.25));
        return toolResult("ok");
    }
    else if (name == "set_note")
    {
        const auto id = string(args, "note_id");
        if (args.hasProperty("modulation"))
            project.setNoteModulation(id, static_cast<float>(number(args, "modulation", 1.0)));
        if (args.hasProperty("drift"))
            project.setNoteDrift(id, static_cast<float>(number(args, "drift", 1.0)));
        if (args.hasProperty("tension"))
            project.setNoteTension(id, static_cast<float>(number(args, "tension")));
        if (args.hasProperty("breath"))
            project.setNoteBreath(id, static_cast<float>(number(args, "breath")));
        if (args.hasProperty("formant_semitones"))
            project.setNoteFormant(id, static_cast<float>(number(args, "formant_semitones")));
        if (args.hasProperty("gain"))
            project.setNoteGain(id, static_cast<float>(number(args, "gain", 1.0)));
        if (args.hasProperty("consonant_seconds") || args.hasProperty("attack_speed"))
        {
            auto consonant = number(args, "consonant_seconds", 0.04);
            auto speed = static_cast<float>(number(args, "attack_speed", 1.0));
            const auto data = project.snapshot();
            for (const auto& track : data.tracks)
                for (const auto& clip : track.clips)
                    for (const auto& note : clip.notes)
                        if (note.id == id)
                        {
                            if (!args.hasProperty("consonant_seconds")) consonant = note.consonantSeconds;
                            if (!args.hasProperty("attack_speed")) speed = note.attackSpeed;
                        }
            project.setNoteAttack(id, consonant, speed);
        }
        return toolResult("ok");
    }
    else if (name == "set_pitch_curve")
    {
        std::vector<PitchCurveEditPoint> points;
        const auto pointValues = args.getProperty("points", {});
        if (const auto* values = pointValues.getArray())
            for (const auto& value : *values)
                points.push_back({ number(value, "time_seconds"),
                                   static_cast<float>(number(value, "midi", 60.0)) });
        if (project.setNotePitchCurve(string(args, "note_id"), std::move(points)))
            return toolResult("ok");
        error = "Pitch curve needs a valid note_id and at least one point";
    }
    else if (name == "add_note")
    {
        const auto id = project.addNote(string(args, "clip_id"), number(args, "start_seconds"),
            number(args, "duration_seconds", 0.25), static_cast<float>(number(args, "midi", 60.0)));
        return id.isNotEmpty() ? toolResult("note_id=" + id)
                               : toolResult("No compose clip accepts the note", true);
    }
    else if (name == "remove_note")
    {
        project.removeNote(string(args, "note_id"));
        return toolResult("ok");
    }
    else if (name == "toggle_note_connection")
    {
        project.toggleNoteConnection(string(args, "note_id"));
        return toolResult("ok");
    }
    else if (name == "remove_clip")
    {
        project.removeClip(string(args, "clip_id"));
        return toolResult("ok");
    }
    else if (name == "remove_track")
    {
        project.removeTrack(string(args, "track_id"));
        return toolResult("ok");
    }
    else if (name == "undo")
    {
        const auto ok = project.undo();
        return toolResult(ok ? "ok" : "history_empty", !ok);
    }
    else if (name == "redo")
    {
        const auto ok = project.redo();
        return toolResult(ok ? "ok" : "history_empty", !ok);
    }
    else if (name == "render_prepare")
    {
        syncAudio();
        if (static_cast<bool>(args.getProperty("wait", false)))
        {
            const auto timeout = juce::jlimit(0.1, 3600.0, number(args, "timeout_seconds", 300.0));
            if (!waitForRender(timeout, error)) return toolResult(error, true);
        }
        return toolResult(juce::JSON::toString(transportStatusJson(), false));
    }
    else if (name == "render_status" || name == "transport_status")
    {
        return toolResult(juce::JSON::toString(transportStatusJson(), false));
    }
    else if (name == "export_wav")
    {
        syncAudio();
        const auto timeout = juce::jlimit(0.1, 3600.0, number(args, "timeout_seconds", 300.0));
        if (!waitForRender(timeout, error)) return toolResult(error, true);
        const juce::File file(string(args, "path"));
        if (file.getFullPathName().isEmpty()) return toolResult("WAV output path is empty", true);
        if (audio->exportWav(file, error))
        {
            auto value = object();
            set(value, "path", file.getFullPathName());
            set(value, "size", file.getSize());
            set(value, "backend", audio->activeRenderBackends());
            return toolResult(juce::JSON::toString(value, false));
        }
    }
    else if (name == "transport_play")
    {
        syncAudio();
        const auto timeout = juce::jlimit(0.1, 3600.0, number(args, "timeout_seconds", 300.0));
        if (!waitForRender(timeout, error)) return toolResult(error, true);
        if (args.hasProperty("position_seconds"))
            audio->setPosition(number(args, "position_seconds"));
        audio->play();
        return toolResult(juce::JSON::toString(transportStatusJson(), false));
    }
    else if (name == "transport_stop")
    {
        audio->stop();
        return toolResult(juce::JSON::toString(transportStatusJson(), false));
    }
    else if (name == "transport_seek")
    {
        audio->setPosition(number(args, "position_seconds"));
        return toolResult(juce::JSON::toString(transportStatusJson(), false));
    }
    else if (name == "sample_settings_read")
    {
        const juce::File file(string(args, "audio_path"));
        if (!file.existsAsFile()) return toolResult("Audio file not found", true);
        auto value = object();
        set(value, "audio_path", file.getFullPathName());
        set(value, "sidecar_path", SampleSettings::sidecarFor(file).getFullPathName());
        set(value, "rows", sampleRowsJson(SampleSettings::loadOrDerive(file, project.snapshot())));
        return toolResult(juce::JSON::toString(value, false));
    }
    else if (name == "sample_settings_save")
    {
        const juce::File file(string(args, "audio_path"));
        if (!file.existsAsFile()) return toolResult("Audio file not found", true);
        const auto rows = sampleRowsFromJson(args.getProperty("rows", juce::var()));
        if (rows.empty()) return toolResult("rows must contain at least one valid region", true);
        if (SampleSettings::save(file, rows, error))
            return toolResult("saved=" + SampleSettings::sidecarFor(file).getFullPathName()
                              + "; regions=" + juce::String(static_cast<juce::int64>(rows.size())));
    }
    else if (name == "oto_import")
    {
        const juce::File audioFile(string(args, "audio_path"));
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(audioFile));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return toolResult("Audio read failed", true);
        std::vector<SampleRegionSetting> rows;
        const auto duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        if (SampleSettings::importOto(juce::File(string(args, "oto_path")), audioFile,
                                      duration, rows, error))
        {
            if (static_cast<bool>(args.getProperty("save_sidecar", true))
                && !SampleSettings::save(audioFile, rows, error))
                return toolResult(error, true);
            auto value = object();
            set(value, "sidecar_path", SampleSettings::sidecarFor(audioFile).getFullPathName());
            set(value, "rows", sampleRowsJson(rows));
            return toolResult(juce::JSON::toString(value, false));
        }
    }
    else if (name == "oto_export")
    {
        const juce::File audioFile(string(args, "audio_path"));
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(audioFile));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return toolResult("Audio read failed", true);
        const auto rows = SampleSettings::loadOrDerive(audioFile, project.snapshot());
        const auto duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        const juce::File otoFile(string(args, "oto_path"));
        if (SampleSettings::exportOto(otoFile, audioFile, rows, duration, error))
            return toolResult("saved=" + otoFile.getFullPathName()
                              + "; regions=" + juce::String(static_cast<juce::int64>(rows.size())));
    }
    else if (name == "voicebank_import")
    {
        juce::StringArray audioFiles;
        juce::StringArray warnings;
        auto sidecars = 0;
        auto regions = 0;
        if (SampleSettings::importVoicebank(juce::File(string(args, "path")), audioFiles,
                                            sidecars, regions, warnings))
        {
            auto value = object();
            std::vector<juce::var> files;
            for (const auto& file : audioFiles) files.emplace_back(file);
            std::vector<juce::var> warningValues;
            for (const auto& warning : warnings) warningValues.emplace_back(warning);
            set(value, "audio_files", array(std::move(files)));
            set(value, "sidecars_written", sidecars);
            set(value, "regions_written", regions);
            set(value, "warnings", array(std::move(warningValues)));
            return toolResult(juce::JSON::toString(value, false));
        }
        error = warnings.isEmpty() ? "Voicebank import failed" : warnings.joinIntoString("\n");
    }
    else if (name == "read_file")
    {
        const juce::File file(string(args, "path"));
        auto input = file.createInputStream();
        if (input != nullptr)
        {
            const auto offset = std::max<juce::int64>(0, static_cast<juce::int64>(number(args, "offset")));
            const auto requested = juce::jlimit(1, 4 * 1024 * 1024,
                                                 static_cast<int>(number(args, "max_bytes", 1024 * 1024)));
            input->setPosition(offset);
            juce::MemoryBlock bytes;
            input->readIntoMemoryBlock(bytes, requested);
            auto value = object();
            set(value, "path", file.getFullPathName());
            set(value, "offset", offset);
            set(value, "size", static_cast<juce::int64>(bytes.getSize()));
            set(value, "base64", juce::Base64::toBase64(bytes.getData(), bytes.getSize()));
            return toolResult(juce::JSON::toString(value, false));
        }
        error = "File read failed";
    }
    else if (name == "list_directory")
    {
        const juce::File directory(string(args, "path"));
        juce::Array<juce::File> entries;
        directory.findChildFiles(entries, juce::File::findFilesAndDirectories, false);
        std::vector<juce::var> values;
        for (const auto& entry : entries)
        {
            auto value = object();
            set(value, "name", entry.getFileName());
            set(value, "path", entry.getFullPathName());
            set(value, "directory", entry.isDirectory());
            set(value, "size", entry.isDirectory() ? juce::int64(0) : entry.getSize());
            values.push_back(std::move(value));
        }
        return toolResult(juce::JSON::toString(array(std::move(values)), false));
    }
    else error = "Unknown tool: " + name;
    return toolResult(error.isNotEmpty() ? error : "Operation failed", true);
}

juce::var McpServer::projectJson() const
{
    const auto data = project.snapshot();
    auto root = object();
    set(root, "name", data.name);
    set(root, "bpm", data.bpm);
    set(root, "beat_origin_seconds", data.beatOriginSeconds);
    set(root, "numerator", data.numerator);
    set(root, "denominator", data.denominator);
    set(root, "grid", data.gridDivision);
    set(root, "base_scale", data.baseScale);
    std::vector<juce::var> tracks;
    for (const auto& track : data.tracks)
    {
        auto trackValue = object();
        set(trackValue, "id", track.id);
        set(trackValue, "name", track.name);
        set(trackValue, "compose", track.compose);
        set(trackValue, "muted", track.muted);
        set(trackValue, "solo", track.solo);
        set(trackValue, "volume", track.volume);
        set(trackValue, "pan", track.pan);
        set(trackValue, "pitch_algorithm", pitchAlgorithmText(track.pitchAlgorithm));
        set(trackValue, "stretch_algorithm", stretchAlgorithmText(track.stretchAlgorithm));
        std::vector<juce::var> clips;
        for (const auto& clip : track.clips)
        {
            auto clipValue = object();
            set(clipValue, "id", clip.id);
            set(clipValue, "source_file", clip.sourceFile.getFullPathName());
            set(clipValue, "start_seconds", clip.startSeconds);
            set(clipValue, "source_offset_seconds", clip.sourceOffsetSeconds);
            set(clipValue, "source_duration_seconds", clip.sourceDurationSeconds);
            set(clipValue, "duration_seconds", clip.durationSeconds);
            set(clipValue, "fade_in_seconds", clip.fadeInSeconds);
            set(clipValue, "fade_out_seconds", clip.fadeOutSeconds);
            set(clipValue, "gain", clip.gain);
            set(clipValue, "muted", clip.muted);
            std::vector<juce::var> sourceTimeMap;
            sourceTimeMap.reserve(clip.sourceTimeMap.size());
            for (const auto& point : clip.sourceTimeMap)
            {
                auto pointValue = object();
                set(pointValue, "target_seconds", point.targetSeconds);
                set(pointValue, "source_seconds", point.sourceSeconds);
                sourceTimeMap.push_back(std::move(pointValue));
            }
            set(clipValue, "source_time_map", array(std::move(sourceTimeMap)));
            std::vector<juce::var> notes;
            for (const auto& note : clip.notes)
            {
                auto noteValue = object();
                set(noteValue, "id", note.id);
                set(noteValue, "start_seconds", note.startSeconds);
                set(noteValue, "duration_seconds", note.durationSeconds);
                set(noteValue, "consonant_seconds", note.consonantSeconds);
                set(noteValue, "midi", note.midiNote);
                set(noteValue, "source_midi_center", note.sourceMidiCenter);
                set(noteValue, "modulation", note.modulation);
                set(noteValue, "flattened", note.modulation <= 1.0e-4f);
                set(noteValue, "drift", note.drift);
                set(noteValue, "tension", note.tension);
                set(noteValue, "breath", note.breath);
                set(noteValue, "formant_semitones", note.formantSemitones);
                set(noteValue, "gain", note.gain);
                set(noteValue, "attack_speed", note.attackSpeed);
                set(noteValue, "connected_previous", note.connectedToPrevious);
                set(noteValue, "connected_next", note.connectedToNext);
                std::vector<juce::var> contour;
                for (const auto& point : note.contour)
                {
                    auto pointValue = object();
                    set(pointValue, "time_seconds", point.timeSeconds);
                    set(pointValue, "relative_cents", point.relativeCents);
                    set(pointValue, "without_vibrato_cents", point.withoutVibratoCents);
                    set(pointValue, "rendered_target_cents", renderedPitchCents(note, point));
                    set(pointValue, "voiced", point.voiced);
                    set(pointValue, "manual_target_cents", point.manualTargetCents);
                    set(pointValue, "has_manual_target", point.hasManualTarget);
                    contour.push_back(std::move(pointValue));
                }
                set(noteValue, "contour", array(std::move(contour)));
                std::vector<juce::var> markers;
                for (const auto marker : note.sibilantMarkers) markers.emplace_back(marker);
                set(noteValue, "sibilant_markers", array(std::move(markers)));
                notes.push_back(std::move(noteValue));
            }
            set(clipValue, "notes", array(std::move(notes)));
            clips.push_back(std::move(clipValue));
        }
        set(trackValue, "clips", array(std::move(clips)));
        tracks.push_back(std::move(trackValue));
    }
    set(root, "tracks", array(std::move(tracks)));
    return root;
}

juce::int64 McpServer::currentProjectFingerprint() const
{
    return juce::JSON::toString(projectJson(), false).hashCode64();
}

void McpServer::syncAudio()
{
    const auto fingerprint = currentProjectFingerprint();
    if (audioPrepared && fingerprint == preparedFingerprint) return;
    audio->stop();
    audio->syncProject(project.snapshot());
    preparedFingerprint = fingerprint;
    audioPrepared = true;
}

bool McpServer::waitForRender(double timeoutSeconds, juce::String& error)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    while (audio->renderProgress().has_value())
    {
        if ((juce::Time::getMillisecondCounterHiRes() - started) * 0.001 >= timeoutSeconds)
        {
            error = "Pre-render timed out after " + juce::String(timeoutSeconds, 1) + " seconds";
            return false;
        }
        juce::Thread::sleep(10);
    }
    return true;
}

juce::var McpServer::transportStatusJson() const
{
    auto value = object();
    const auto fingerprint = currentProjectFingerprint();
    const auto prepared = audioPrepared && fingerprint == preparedFingerprint;
    const auto progress = prepared ? audio->renderProgress() : std::optional<double>();
    set(value, "prepared", prepared);
    set(value, "rendering", progress.has_value());
    set(value, "render_progress", progress.has_value() ? *progress : (prepared ? 1.0 : 0.0));
    set(value, "backend", prepared ? audio->activeRenderBackends() : juce::String());
    set(value, "playing", audio->isPlaying());
    set(value, "position_seconds", audio->position());
    return value;
}

juce::var McpServer::toolResult(const juce::String& text, bool isError)
{
    auto content = object();
    set(content, "type", "text");
    set(content, "text", text);
    auto result = object();
    set(result, "content", array({ content }));
    set(result, "isError", isError);
    return result;
}

juce::var McpServer::errorResponse(const juce::var& id, int code, const juce::String& message)
{
    auto error = object();
    set(error, "code", code);
    set(error, "message", message);
    auto response = object();
    set(response, "jsonrpc", "2.0");
    set(response, "id", id);
    set(response, "error", error);
    return response;
}
}
