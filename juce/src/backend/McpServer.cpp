#include "McpServer.h"
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
    set(tool, "description", description);
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
}

McpServer::McpServer()
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
            makeTool("import_midi", "Import MIDI notes and tempo / 导入 MIDI"),
            makeTool("import_melodyne", "Import Melodyne MPD edits / 导入 Melodyne 工程"),
            makeTool("set_tempo", "Set BPM and time signature / 设置速度与拍号"),
            makeTool("set_track", "Set compose, mute, solo or volume for a track / 设置轨道"),
            makeTool("move_clip", "Move a clip on the timeline / 移动采样"),
            makeTool("transpose_note", "Move a note and its whole contour / 整体移动音高线"),
            makeTool("resize_note", "Change note time bounds / 修改音符时间"),
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
            if (project.load(file, error)) return toolResult(summary(project.snapshot()));
        }
        else if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file)))
        {
            project.clear();
            project.addAudioFile(file, static_cast<double>(reader->lengthInSamples) / reader->sampleRate);
            return toolResult(summary(project.snapshot()));
        }
        else error = "Unsupported or unreadable file";
    }
    else if (name == "import_audio")
    {
        const juce::File file(string(args, "path"));
        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file)))
        {
            project.addAudioFile(file, static_cast<double>(reader->lengthInSamples) / reader->sampleRate,
                                 number(args, "start_seconds"));
            return toolResult("ok");
        }
        error = "Audio read failed";
    }
    else if (name == "import_midi")
    {
        if (project.addMidiFile(juce::File(string(args, "path")), error)) return toolResult("ok");
    }
    else if (name == "import_melodyne")
    {
        if (auto imported = MelodyneImporter::importProject(juce::File(string(args, "path")), error))
        {
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
        return toolResult("ok");
    }
    else if (name == "move_clip")
    {
        project.moveClip(string(args, "clip_id"), number(args, "start_seconds"));
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
                set(noteValue, "drift", note.drift);
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
                    set(pointValue, "voiced", point.voiced);
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
