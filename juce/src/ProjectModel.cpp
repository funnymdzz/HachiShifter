#include "ProjectModel.h"
#include <algorithm>

namespace hachi
{
namespace
{
juce::String pitchAlgorithmName(PitchAlgorithm value)
{
    switch (value)
    {
        case PitchAlgorithm::mld5: return "mld5";
        case PitchAlgorithm::nsfHifigan: return "nsf-hifigan";
        case PitchAlgorithm::world: return "world";
        case PitchAlgorithm::vocalShifter: return "vslib";
    }
    return "mld5";
}

PitchAlgorithm parsePitchAlgorithm(const juce::String& value)
{
    if (value == "nsf-hifigan") return PitchAlgorithm::nsfHifigan;
    if (value == "world") return PitchAlgorithm::world;
    if (value == "vslib") return PitchAlgorithm::vocalShifter;
    return PitchAlgorithm::mld5;
}

juce::String stretchAlgorithmName(StretchAlgorithm value)
{
    switch (value)
    {
        case StretchAlgorithm::melodyneHybrid: return "melodyne-hybrid";
        case StretchAlgorithm::variableMelHop: return "variable-mel-hop";
        case StretchAlgorithm::loop: return "loop";
        case StretchAlgorithm::soundTouch: return "soundtouch";
    }
    return "melodyne-hybrid";
}

StretchAlgorithm parseStretchAlgorithm(const juce::String& value)
{
    if (value == "variable-mel-hop") return StretchAlgorithm::variableMelHop;
    if (value == "loop") return StretchAlgorithm::loop;
    if (value == "soundtouch") return StretchAlgorithm::soundTouch;
    return StretchAlgorithm::melodyneHybrid;
}
}

double ProjectData::durationSeconds() const
{
    double duration = 8.0;
    for (const auto& track : tracks)
        for (const auto& clip : track.clips)
            duration = std::max(duration, clip.startSeconds + clip.durationSeconds);
    return duration;
}

ProjectModel::ProjectModel()
{
    project.name = "Untitled";
}

ProjectData ProjectModel::snapshot() const
{
    const juce::ScopedLock guard(lock);
    return project;
}

void ProjectModel::replace(ProjectData replacement)
{
    {
        const juce::ScopedLock guard(lock);
        project = std::move(replacement);
    }
    sendChangeMessage();
}

void ProjectModel::clear()
{
    replace(ProjectData{});
}

juce::String ProjectModel::makeId(const char* prefix)
{
    return juce::String(prefix) + "_" + juce::Uuid().toString().removeCharacters("-");
}

void ProjectModel::addAudioFile(const juce::File& file, double durationSeconds)
{
    TrackData track;
    track.id = makeId("track");
    track.name = file.getFileNameWithoutExtension();

    ClipData clip;
    clip.id = makeId("clip");
    clip.sourceFile = file;
    clip.durationSeconds = std::max(0.01, durationSeconds);
    clip.sourceDurationSeconds = clip.durationSeconds;
    track.clips.push_back(std::move(clip));

    {
        const juce::ScopedLock guard(lock);
        project.tracks.push_back(std::move(track));
        if (project.name == "Untitled")
            project.name = file.getFileNameWithoutExtension();
    }
    sendChangeMessage();
}

void ProjectModel::setTempo(double bpm, int numerator, int denominator)
{
    {
        const juce::ScopedLock guard(lock);
        project.bpm = juce::jlimit(20.0, 400.0, bpm);
        project.numerator = juce::jlimit(1, 32, numerator);
        project.denominator = denominator == 2 || denominator == 8 || denominator == 16
            ? denominator : 4;
    }
    sendChangeMessage();
}

void ProjectModel::setGridDivision(const juce::String& division)
{
    {
        const juce::ScopedLock guard(lock);
        project.gridDivision = division;
    }
    sendChangeMessage();
}

void ProjectModel::setBaseScale(const juce::String& scale)
{
    {
        const juce::ScopedLock guard(lock);
        project.baseScale = scale;
    }
    sendChangeMessage();
}

void ProjectModel::setTrackCompose(const juce::String& trackId, bool enabled)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId)
                track.compose = enabled;
    }
    sendChangeMessage();
}

void ProjectModel::setTrackMuted(const juce::String& trackId, bool muted)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId)
                track.muted = muted;
    }
    sendChangeMessage();
}

void ProjectModel::setTrackSolo(const juce::String& trackId, bool solo)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId)
                track.solo = solo;
    }
    sendChangeMessage();
}

void ProjectModel::setTrackVolume(const juce::String& trackId, float volume)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId)
                track.volume = juce::jlimit(0.0f, 2.0f, volume);
    }
    sendChangeMessage();
}

void ProjectModel::setPitchAlgorithm(PitchAlgorithm algorithm)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.compose)
                track.pitchAlgorithm = algorithm;
    }
    sendChangeMessage();
}

void ProjectModel::setStretchAlgorithm(StretchAlgorithm algorithm)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.compose)
                track.stretchAlgorithm = algorithm;
    }
    sendChangeMessage();
}

void ProjectModel::moveClip(const juce::String& clipId, double startSeconds)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                    clip.startSeconds = std::max(0.0, startSeconds);
    }
    sendChangeMessage();
}

void ProjectModel::transposeNote(const juce::String& noteId, float semitones)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                        note.midiNote = juce::jlimit(0.0f, 127.0f, note.midiNote + semitones);
    }
    sendChangeMessage();
}

void ProjectModel::resizeNote(const juce::String& noteId, double newStart, double newDuration)
{
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        note.startSeconds = std::max(0.0, newStart);
                        note.durationSeconds = std::max(0.01, newDuration);
                    }
    }
    sendChangeMessage();
}

juce::ValueTree ProjectModel::toValueTree() const
{
    const auto data = snapshot();
    juce::ValueTree root("HachiShifterProject");
    root.setProperty("version", 2, nullptr);
    root.setProperty("name", data.name, nullptr);
    root.setProperty("bpm", data.bpm, nullptr);
    root.setProperty("beatOriginSeconds", data.beatOriginSeconds, nullptr);
    root.setProperty("numerator", data.numerator, nullptr);
    root.setProperty("denominator", data.denominator, nullptr);
    root.setProperty("gridDivision", data.gridDivision, nullptr);
    root.setProperty("baseScale", data.baseScale, nullptr);

    for (const auto& track : data.tracks)
    {
        juce::ValueTree trackTree("Track");
        trackTree.setProperty("id", track.id, nullptr);
        trackTree.setProperty("name", track.name, nullptr);
        trackTree.setProperty("compose", track.compose, nullptr);
        trackTree.setProperty("muted", track.muted, nullptr);
        trackTree.setProperty("solo", track.solo, nullptr);
        trackTree.setProperty("volume", track.volume, nullptr);
        trackTree.setProperty("pan", track.pan, nullptr);
        trackTree.setProperty("pitchAlgorithm", pitchAlgorithmName(track.pitchAlgorithm), nullptr);
        trackTree.setProperty("stretchAlgorithm", stretchAlgorithmName(track.stretchAlgorithm), nullptr);

        for (const auto& clip : track.clips)
        {
            juce::ValueTree clipTree("Clip");
            clipTree.setProperty("id", clip.id, nullptr);
            clipTree.setProperty("sourceFile", clip.sourceFile.getFullPathName(), nullptr);
            clipTree.setProperty("startSeconds", clip.startSeconds, nullptr);
            clipTree.setProperty("sourceOffsetSeconds", clip.sourceOffsetSeconds, nullptr);
            clipTree.setProperty("sourceDurationSeconds", clip.sourceDurationSeconds, nullptr);
            clipTree.setProperty("durationSeconds", clip.durationSeconds, nullptr);
            clipTree.setProperty("fadeInSeconds", clip.fadeInSeconds, nullptr);
            clipTree.setProperty("fadeOutSeconds", clip.fadeOutSeconds, nullptr);
            clipTree.setProperty("gain", clip.gain, nullptr);
            clipTree.setProperty("muted", clip.muted, nullptr);

            for (const auto& note : clip.notes)
            {
                juce::ValueTree noteTree("Note");
                noteTree.setProperty("id", note.id, nullptr);
                noteTree.setProperty("startSeconds", note.startSeconds, nullptr);
                noteTree.setProperty("durationSeconds", note.durationSeconds, nullptr);
                noteTree.setProperty("consonantSeconds", note.consonantSeconds, nullptr);
                noteTree.setProperty("midiNote", note.midiNote, nullptr);
                noteTree.setProperty("sourceMidiCenter", note.sourceMidiCenter, nullptr);
                noteTree.setProperty("modulation", note.modulation, nullptr);
                noteTree.setProperty("drift", note.drift, nullptr);
                noteTree.setProperty("attackSpeed", note.attackSpeed, nullptr);
                noteTree.setProperty("connectedToPrevious", note.connectedToPrevious, nullptr);
                noteTree.setProperty("connectedToNext", note.connectedToNext, nullptr);
                for (const auto& point : note.contour)
                {
                    juce::ValueTree pointTree("PitchPoint");
                    pointTree.setProperty("timeSeconds", point.timeSeconds, nullptr);
                    pointTree.setProperty("relativeCents", point.relativeCents, nullptr);
                    pointTree.setProperty("withoutVibratoCents", point.withoutVibratoCents, nullptr);
                    pointTree.setProperty("voiced", point.voiced, nullptr);
                    noteTree.addChild(pointTree, -1, nullptr);
                }
                for (const auto marker : note.sibilantMarkers)
                {
                    juce::ValueTree markerTree("Sibilant");
                    markerTree.setProperty("timeSeconds", marker, nullptr);
                    noteTree.addChild(markerTree, -1, nullptr);
                }
                clipTree.addChild(noteTree, -1, nullptr);
            }
            trackTree.addChild(clipTree, -1, nullptr);
        }
        root.addChild(trackTree, -1, nullptr);
    }
    return root;
}

ProjectData ProjectModel::fromValueTree(const juce::ValueTree& root)
{
    ProjectData data;
    data.name = root.getProperty("name", "Untitled").toString();
    data.bpm = static_cast<double>(root.getProperty("bpm", 120.0));
    data.beatOriginSeconds = static_cast<double>(root.getProperty("beatOriginSeconds", 0.0));
    data.numerator = static_cast<int>(root.getProperty("numerator", 4));
    data.denominator = static_cast<int>(root.getProperty("denominator", 4));
    data.gridDivision = root.getProperty("gridDivision", "1/16").toString();
    data.baseScale = root.getProperty("baseScale", "C").toString();

    for (const auto trackTree : root)
    {
        if (!trackTree.hasType("Track")) continue;
        TrackData track;
        track.id = trackTree.getProperty("id").toString();
        track.name = trackTree.getProperty("name").toString();
        track.compose = static_cast<bool>(trackTree.getProperty("compose", true));
        track.muted = static_cast<bool>(trackTree.getProperty("muted", false));
        track.solo = static_cast<bool>(trackTree.getProperty("solo", false));
        track.volume = static_cast<float>(trackTree.getProperty("volume", 1.0));
        track.pan = static_cast<float>(trackTree.getProperty("pan", 0.0));
        track.pitchAlgorithm = parsePitchAlgorithm(trackTree.getProperty("pitchAlgorithm", "mld5").toString());
        track.stretchAlgorithm = parseStretchAlgorithm(trackTree.getProperty("stretchAlgorithm", "melodyne-hybrid").toString());

        for (const auto clipTree : trackTree)
        {
            if (!clipTree.hasType("Clip")) continue;
            ClipData clip;
            clip.id = clipTree.getProperty("id").toString();
            clip.sourceFile = juce::File(clipTree.getProperty("sourceFile").toString());
            clip.startSeconds = static_cast<double>(clipTree.getProperty("startSeconds", 0.0));
            clip.sourceOffsetSeconds = static_cast<double>(clipTree.getProperty("sourceOffsetSeconds", 0.0));
            clip.sourceDurationSeconds = static_cast<double>(clipTree.getProperty("sourceDurationSeconds", 0.0));
            clip.durationSeconds = static_cast<double>(clipTree.getProperty("durationSeconds", 1.0));
            clip.fadeInSeconds = static_cast<double>(clipTree.getProperty("fadeInSeconds", 0.0));
            clip.fadeOutSeconds = static_cast<double>(clipTree.getProperty("fadeOutSeconds", 0.0));
            clip.gain = static_cast<float>(clipTree.getProperty("gain", 1.0));
            clip.muted = static_cast<bool>(clipTree.getProperty("muted", false));

            for (const auto noteTree : clipTree)
            {
                if (!noteTree.hasType("Note")) continue;
                NoteData note;
                note.id = noteTree.getProperty("id").toString();
                note.startSeconds = static_cast<double>(noteTree.getProperty("startSeconds", 0.0));
                note.durationSeconds = static_cast<double>(noteTree.getProperty("durationSeconds", 0.25));
                note.consonantSeconds = static_cast<double>(noteTree.getProperty("consonantSeconds", 0.04));
                note.midiNote = static_cast<float>(noteTree.getProperty("midiNote", 60.0));
                note.sourceMidiCenter = static_cast<float>(noteTree.getProperty("sourceMidiCenter", -1.0));
                note.modulation = static_cast<float>(noteTree.getProperty("modulation", 1.0));
                note.drift = static_cast<float>(noteTree.getProperty("drift", 1.0));
                note.attackSpeed = static_cast<float>(noteTree.getProperty("attackSpeed", 1.0));
                note.connectedToPrevious = static_cast<bool>(noteTree.getProperty("connectedToPrevious", false));
                note.connectedToNext = static_cast<bool>(noteTree.getProperty("connectedToNext", false));
                for (const auto child : noteTree)
                {
                    if (child.hasType("PitchPoint"))
                    {
                        const auto relative = static_cast<float>(child.getProperty("relativeCents", 0.0));
                        note.contour.push_back({ static_cast<double>(child.getProperty("timeSeconds", 0.0)),
                                                 relative,
                                                 static_cast<float>(child.getProperty("withoutVibratoCents", relative)),
                                                 static_cast<bool>(child.getProperty("voiced", true)) });
                    }
                    else if (child.hasType("Sibilant"))
                        note.sibilantMarkers.push_back(static_cast<double>(child.getProperty("timeSeconds", 0.0)));
                }
                clip.notes.push_back(std::move(note));
            }
            track.clips.push_back(std::move(clip));
        }
        data.tracks.push_back(std::move(track));
    }
    return data;
}

bool ProjectModel::save(const juce::File& file, juce::String& error) const
{
    if (auto stream = file.createOutputStream())
    {
        stream->setPosition(0);
        stream->truncate();
        toValueTree().writeToStream(*stream);
        stream->flush();
        return true;
    }
    error = "Could not write " + file.getFullPathName();
    return false;
}

bool ProjectModel::load(const juce::File& file, juce::String& error)
{
    if (auto stream = file.createInputStream())
    {
        auto tree = juce::ValueTree::readFromStream(*stream);
        if (tree.hasType("HachiShifterProject"))
        {
            replace(fromValueTree(tree));
            return true;
        }
    }
    error = "Invalid HachiShifter Next project: " + file.getFullPathName();
    return false;
}
}
