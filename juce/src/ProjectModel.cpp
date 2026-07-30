#include "ProjectModel.h"
#include "SampleSettings.h"
#include <algorithm>
#include <optional>

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

void ProjectModel::pushUndoLocked()
{
    undoHistory.push_back(project);
    if (undoHistory.size() > maxHistory)
        undoHistory.erase(undoHistory.begin());
    redoHistory.clear();
}

void ProjectModel::replace(ProjectData replacement)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        project = std::move(replacement);
    }
    sendChangeMessage();
}

void ProjectModel::clear()
{
    replace(ProjectData{});
}

bool ProjectModel::undo()
{
    {
        const juce::ScopedLock guard(lock);
        if (undoHistory.empty()) return false;
        redoHistory.push_back(project);
        if (redoHistory.size() > maxHistory) redoHistory.erase(redoHistory.begin());
        project = std::move(undoHistory.back());
        undoHistory.pop_back();
    }
    sendChangeMessage();
    return true;
}

bool ProjectModel::redo()
{
    {
        const juce::ScopedLock guard(lock);
        if (redoHistory.empty()) return false;
        undoHistory.push_back(project);
        if (undoHistory.size() > maxHistory) undoHistory.erase(undoHistory.begin());
        project = std::move(redoHistory.back());
        redoHistory.pop_back();
    }
    sendChangeMessage();
    return true;
}

bool ProjectModel::canUndo() const
{
    const juce::ScopedLock guard(lock);
    return !undoHistory.empty();
}

bool ProjectModel::canRedo() const
{
    const juce::ScopedLock guard(lock);
    return !redoHistory.empty();
}

juce::String ProjectModel::makeId(const char* prefix)
{
    return juce::String(prefix) + "_" + juce::Uuid().toString().removeCharacters("-");
}

juce::String ProjectModel::addAudioFile(const juce::File& file, double durationSeconds,
                                        double startSeconds)
{
    TrackData track;
    track.id = makeId("track");
    track.name = file.getFileNameWithoutExtension();

    ClipData clip;
    clip.id = makeId("clip");
    clip.sourceFile = file;
    clip.startSeconds = std::max(0.0, startSeconds);
    clip.durationSeconds = std::max(0.01, durationSeconds);
    clip.sourceDurationSeconds = clip.durationSeconds;

    // Voicebank registration writes timing beside the source before the file
    // is dragged into a project.  Consume that sidecar immediately so an OTO
    // sample arrives as editable note objects instead of a silent/empty piano
    // roll that requires drawing every region again.
    const auto sidecar = SampleSettings::sidecarFor(file);
    const juce::File legacy(file.getFullPathName() + ".hachi.csv");
    if (sidecar.existsAsFile() || legacy.existsAsFile())
    {
        const auto rows = SampleSettings::loadOrDerive(file, ProjectData{});
        for (const auto& row : rows)
        {
            const auto regionStart = juce::jlimit(0.0, clip.durationSeconds,
                                                   row.regionStartSeconds);
            const auto regionEnd = juce::jlimit(regionStart, clip.durationSeconds,
                                                 row.regionEndSeconds);
            if (regionEnd - regionStart < 0.001) continue;
            NoteData note;
            note.id = makeId("note");
            note.startSeconds = regionStart;
            note.durationSeconds = regionEnd - regionStart;
            note.consonantSeconds = juce::jlimit(0.0, note.durationSeconds,
                                                  row.fixedDurationSeconds);
            const auto storedSource = row.melodyneOriginalPitchCenterCents > 0.0
                ? static_cast<float>(row.melodyneOriginalPitchCenterCents / 100.0)
                : row.melodynePitchCenterCents > 0.0
                    ? static_cast<float>(row.melodynePitchCenterCents / 100.0)
                    : 60.0f;
            note.sourceMidiCenter = juce::jlimit(0.0f, 127.0f, storedSource);
            note.midiNote = row.melodynePitchCenterCents > 0.0
                ? juce::jlimit(0.0f, 127.0f,
                    static_cast<float>(row.melodynePitchCenterCents / 100.0))
                : juce::jlimit(0.0f, 127.0f, note.sourceMidiCenter
                    + static_cast<float>(row.relativePitchCents / 100.0));
            note.drift = juce::jlimit(0.0f, 2.0f,
                static_cast<float>(row.melodynePitchDrift));
            note.modulation = juce::jlimit(0.0f, 2.0f,
                static_cast<float>(row.melodynePitchModulation));
            note.formantSemitones = juce::jlimit(-12.0f, 12.0f,
                static_cast<float>(row.melodyneFormantCents / 100.0));
            note.breath = juce::jlimit(0.0f, 1.0f,
                static_cast<float>(row.melodyneSibilantBalance));
            note.gain = juce::jlimit(0.0f, 4.0f,
                static_cast<float>(row.melodyneAmplitude));
            note.attackSpeed = juce::jlimit(0.05f, 20.0f,
                static_cast<float>(row.melodyneAttackSeconds > 1.0e-6
                    ? row.fixedDurationSeconds / row.melodyneAttackSeconds : 1.0));
            // A sidecar stores note-level controls rather than a dense F0
            // curve.  A neutral two-point contour keeps the source waveform's
            // own micro-pitch intact while allowing the whole region to move.
            note.contour.push_back({ 0.0, 0.0f, 0.0f, true });
            note.contour.push_back({ note.durationSeconds, 0.0f, 0.0f, true });
            clip.notes.push_back(std::move(note));
        }
    }
    const auto clipId = clip.id;
    track.clips.push_back(std::move(clip));

    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        project.tracks.push_back(std::move(track));
        if (project.name == "Untitled")
            project.name = file.getFileNameWithoutExtension();
    }
    sendChangeMessage();
    return clipId;
}

bool ProjectModel::setClipNotesIfEmpty(const juce::String& clipId,
                                       std::vector<NoteData> notes)
{
    if (notes.empty()) return false;
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId && clip.notes.empty())
                {
                    // Import analysis is one operation.  More importantly,
                    // do not replace notes the user drew while it was running.
                    clip.notes = std::move(notes);
                    changed = true;
                    break;
                }
    }
    if (changed) sendChangeMessage();
    return changed;
}

bool ProjectModel::addMidiFile(const juce::File& file, juce::String& error)
{
    auto input = file.createInputStream();
    if (input == nullptr)
    {
        error = "Could not open MIDI file: " + file.getFullPathName();
        return false;
    }
    juce::MidiFile midi;
    if (!midi.readFrom(*input))
    {
        error = "Invalid MIDI file: " + file.getFullPathName();
        return false;
    }
    std::optional<double> importedBpm;
    for (int trackIndex = 0; trackIndex < midi.getNumTracks() && !importedBpm; ++trackIndex)
        if (const auto* sequence = midi.getTrack(trackIndex))
            for (int eventIndex = 0; eventIndex < sequence->getNumEvents(); ++eventIndex)
                if (const auto* event = sequence->getEventPointer(eventIndex);
                    event != nullptr && event->message.isTempoMetaEvent())
                {
                    const auto secondsPerQuarter = event->message.getTempoSecondsPerQuarterNote();
                    if (secondsPerQuarter > 1.0e-9)
                        importedBpm = 60.0 / secondsPerQuarter;
                    break;
                }
    midi.convertTimestampTicksToSeconds();
    std::vector<TrackData> importedTracks;
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        const auto* sequence = midi.getTrack(trackIndex);
        if (sequence == nullptr) continue;
        juce::MidiMessageSequence matched(*sequence);
        matched.updateMatchedPairs();
        TrackData track;
        track.id = makeId("track");
        track.name = file.getFileNameWithoutExtension()
            + (midi.getNumTracks() > 1 ? " " + juce::String(trackIndex + 1) : juce::String());
        track.compose = true;
        ClipData clip;
        clip.id = makeId("clip");
        clip.sourceFile = file;
        clip.startSeconds = 0.0;
        for (int eventIndex = 0; eventIndex < matched.getNumEvents(); ++eventIndex)
        {
            const auto* event = matched.getEventPointer(eventIndex);
            if (event == nullptr || !event->message.isNoteOn()) continue;
            const auto offIndex = matched.getIndexOfMatchingKeyUp(eventIndex);
            const auto start = std::max(0.0, event->message.getTimeStamp());
            const auto end = offIndex >= 0
                ? std::max(start + 0.01, matched.getEventTime(offIndex)) : start + 0.25;
            NoteData note;
            note.id = makeId("note");
            note.startSeconds = start;
            note.durationSeconds = end - start;
            note.consonantSeconds = 0.0;
            note.midiNote = static_cast<float>(event->message.getNoteNumber());
            note.sourceMidiCenter = note.midiNote;
            note.contour.push_back({ 0.0, 0.0f, 0.0f, true });
            note.contour.push_back({ note.durationSeconds, 0.0f, 0.0f, true });
            clip.durationSeconds = std::max(clip.durationSeconds, end);
            clip.notes.push_back(std::move(note));
        }
        if (clip.notes.empty()) continue;
        clip.sourceDurationSeconds = clip.durationSeconds;
        track.clips.push_back(std::move(clip));
        importedTracks.push_back(std::move(track));
    }
    if (importedTracks.empty())
    {
        error = "MIDI file contains no notes: " + file.getFullPathName();
        return false;
    }
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        if (project.tracks.empty() && importedBpm)
            project.bpm = juce::jlimit(20.0, 400.0, *importedBpm);
        for (auto& track : importedTracks) project.tracks.push_back(std::move(track));
        if (project.name == "Untitled") project.name = file.getFileNameWithoutExtension();
    }
    sendChangeMessage();
    return true;
}

void ProjectModel::setTempo(double bpm, int numerator, int denominator)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
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
        pushUndoLocked();
        project.gridDivision = division;
    }
    sendChangeMessage();
}

void ProjectModel::setBaseScale(const juce::String& scale)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        project.baseScale = scale;
    }
    sendChangeMessage();
}

void ProjectModel::setTrackCompose(const juce::String& trackId, bool enabled)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
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
        pushUndoLocked();
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
        pushUndoLocked();
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
        pushUndoLocked();
        for (auto& track : project.tracks)
            if (track.id == trackId)
                track.volume = juce::jlimit(0.0f, 2.0f, volume);
    }
    sendChangeMessage();
}

void ProjectModel::setTrackPan(const juce::String& trackId, float pan)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        for (auto& track : project.tracks)
            if (track.id == trackId)
                track.pan = juce::jlimit(-1.0f, 1.0f, pan);
    }
    sendChangeMessage();
}

void ProjectModel::setPitchAlgorithm(PitchAlgorithm algorithm)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        for (auto& track : project.tracks)
            if (track.compose)
                track.pitchAlgorithm = algorithm;
    }
    sendChangeMessage();
}

void ProjectModel::setTrackPitchAlgorithm(const juce::String& trackId,
                                          PitchAlgorithm algorithm)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.compose && track.pitchAlgorithm != algorithm)
            {
                pushUndoLocked();
                track.pitchAlgorithm = algorithm;
                changed = true;
                break;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setStretchAlgorithm(StretchAlgorithm algorithm)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        for (auto& track : project.tracks)
            if (track.compose)
                track.stretchAlgorithm = algorithm;
    }
    sendChangeMessage();
}

void ProjectModel::setTrackStretchAlgorithm(const juce::String& trackId,
                                            StretchAlgorithm algorithm)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.compose && track.stretchAlgorithm != algorithm)
            {
                pushUndoLocked();
                track.stretchAlgorithm = algorithm;
                changed = true;
                break;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::moveClip(const juce::String& clipId, double startSeconds)
{
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                    clip.startSeconds = std::max(0.0, startSeconds);
    }
    sendChangeMessage();
}

void ProjectModel::removeClip(const juce::String& clipId)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
        {
            const auto found = std::find_if(track.clips.begin(), track.clips.end(),
                [&](const auto& clip) { return clip.id == clipId; });
            if (found == track.clips.end()) continue;
            pushUndoLocked();
            track.clips.erase(found);
            changed = true;
            break;
        }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::removeTrack(const juce::String& trackId)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        const auto found = std::find_if(project.tracks.begin(), project.tracks.end(),
            [&](const auto& track) { return track.id == trackId; });
        if (found != project.tracks.end())
        {
            pushUndoLocked();
            project.tracks.erase(found);
            changed = true;
        }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::transposeNote(const juce::String& noteId, float semitones)
{
    transposeNotes({ noteId }, semitones);
}

void ProjectModel::transposeNotes(const std::vector<juce::String>& noteIds, float semitones)
{
    const auto includes = [&](const juce::String& id)
    {
        return std::find(noteIds.begin(), noteIds.end(), id) != noteIds.end();
    };
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (const auto& track : project.tracks)
            for (const auto& clip : track.clips)
                for (const auto& note : clip.notes)
                    changed = changed || includes(note.id);
        if (!changed) return;
        pushUndoLocked();
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (includes(note.id))
                        note.midiNote = juce::jlimit(0.0f, 127.0f, note.midiNote + semitones);
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::resizeNote(const juce::String& noteId, double newStart, double newDuration)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
        {
            for (auto& clip : track.clips)
            {
                for (auto& note : clip.notes)
                {
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        const auto oldStart = note.startSeconds;
                        const auto oldDuration = std::max(0.01, note.durationSeconds);
                        const auto oldEnd = oldStart + oldDuration;
                        auto otherEnd = 0.0;
                        for (const auto& candidate : clip.notes)
                            if (candidate.id != noteId)
                                otherEnd = std::max(otherEnd,
                                    candidate.startSeconds + candidate.durationSeconds);
                        const auto wasTail = oldEnd >= otherEnd - 1.0e-6
                            && oldEnd >= clip.durationSeconds - 0.002;
                        const auto targetDuration = std::max(0.01, newDuration);
                        const auto oldAttack = juce::jlimit(0.0, oldDuration,
                            note.consonantSeconds);
                        const auto newAttack = std::min(oldAttack, targetDuration);
                        const auto remapTime = [&](double time)
                        {
                            const auto clamped = juce::jlimit(0.0, oldDuration, time);
                            if (clamped <= oldAttack || oldDuration <= oldAttack + 1.0e-9)
                                return oldAttack > 1.0e-9
                                    ? clamped * newAttack / oldAttack : 0.0;
                            return newAttack + (clamped - oldAttack)
                                * (targetDuration - newAttack) / (oldDuration - oldAttack);
                        };
                        for (auto& point : note.contour)
                            point.timeSeconds = remapTime(point.timeSeconds);
                        for (auto& marker : note.sibilantMarkers)
                            marker = remapTime(marker);
                        note.startSeconds = std::max(0.0, newStart);
                        note.durationSeconds = targetDuration;
                        note.consonantSeconds = newAttack;
                        if (wasTail)
                            clip.durationSeconds = std::max(0.01,
                                std::max(otherEnd, note.startSeconds + note.durationSeconds));
                        changed = true;
                        break;
                    }
                }
                if (changed) break;
            }
            if (changed) break;
        }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteModulation(const juce::String& noteId, float modulation)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        note.modulation = juce::jlimit(0.0f, 2.0f, modulation);
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteDrift(const juce::String& noteId, float drift)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        note.drift = juce::jlimit(0.0f, 2.0f, drift);
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteTension(const juce::String& noteId, float tension)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        note.tension = juce::jlimit(-1.0f, 1.0f, tension);
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteBreath(const juce::String& noteId, float breath)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        note.breath = juce::jlimit(0.0f, 1.0f, breath);
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteFormant(const juce::String& noteId, float semitones)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        note.formantSemitones = juce::jlimit(-12.0f, 12.0f, semitones);
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteGain(const juce::String& noteId, float gain)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        note.gain = juce::jlimit(0.0f, 4.0f, gain);
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteAttack(const juce::String& noteId, double consonantSeconds,
                                 float attackSpeed)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        pushUndoLocked();
                        note.consonantSeconds = juce::jlimit(0.0, note.durationSeconds,
                                                            consonantSeconds);
                        note.attackSpeed = juce::jlimit(0.05f, 20.0f, attackSpeed);
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

juce::String ProjectModel::addNote(const juce::String& preferredClipId,
                                   double absoluteStart, double duration, float midiNote)
{
    juce::String created;
    {
        const juce::ScopedLock guard(lock);
        ClipData* destination = nullptr;
        for (auto& track : project.tracks)
            if (track.compose)
                for (auto& clip : track.clips)
                {
                    if (clip.id == preferredClipId) destination = &clip;
                    if (destination == nullptr
                        && absoluteStart >= clip.startSeconds
                        && absoluteStart <= clip.startSeconds + clip.durationSeconds)
                        destination = &clip;
                }
        if (destination != nullptr)
        {
            pushUndoLocked();
            NoteData note;
            note.id = makeId("note");
            note.startSeconds = juce::jlimit(0.0, destination->durationSeconds,
                absoluteStart - destination->startSeconds);
            note.durationSeconds = juce::jlimit(0.01,
                std::max(0.01, destination->durationSeconds - note.startSeconds), duration);
            note.consonantSeconds = std::min(0.04, note.durationSeconds * 0.3);
            note.midiNote = juce::jlimit(0.0f, 127.0f, midiNote);
            note.sourceMidiCenter = note.midiNote;
            note.contour.push_back({ 0.0, 0.0f, 0.0f, true });
            note.contour.push_back({ note.durationSeconds, 0.0f, 0.0f, true });
            created = note.id;
            destination->notes.push_back(std::move(note));
            std::stable_sort(destination->notes.begin(), destination->notes.end(),
                [](const auto& left, const auto& right) { return left.startSeconds < right.startSeconds; });
        }
    }
    if (created.isNotEmpty()) sendChangeMessage();
    return created;
}

void ProjectModel::removeNote(const juce::String& noteId)
{
    removeNotes({ noteId });
}

void ProjectModel::removeNotes(const std::vector<juce::String>& noteIds)
{
    const auto includes = [&](const juce::String& id)
    {
        return std::find(noteIds.begin(), noteIds.end(), id) != noteIds.end();
    };
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (const auto& track : project.tracks)
            for (const auto& clip : track.clips)
                for (const auto& note : clip.notes)
                    changed = changed || includes(note.id);
        if (!changed) return;
        pushUndoLocked();
        for (auto& track : project.tracks)
        {
            struct Positioned { NoteData* note; double start; };
            std::vector<Positioned> ordered;
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    ordered.push_back({ &note, clip.startSeconds + note.startSeconds });
            std::stable_sort(ordered.begin(), ordered.end(),
                [](const auto& left, const auto& right) { return left.start < right.start; });
            for (std::size_t index = 0; index < ordered.size(); ++index)
                if (includes(ordered[index].note->id))
                {
                    if (index > 0 && !includes(ordered[index - 1].note->id))
                        ordered[index - 1].note->connectedToNext = false;
                    if (index + 1 < ordered.size() && !includes(ordered[index + 1].note->id))
                        ordered[index + 1].note->connectedToPrevious = false;
                }
            for (auto& clip : track.clips)
                clip.notes.erase(std::remove_if(clip.notes.begin(), clip.notes.end(),
                    [&](const auto& note) { return includes(note.id); }), clip.notes.end());
        }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::toggleNoteConnection(const juce::String& noteId)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
        {
            struct Positioned { NoteData* note; double start; };
            std::vector<Positioned> ordered;
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    ordered.push_back({ &note, clip.startSeconds + note.startSeconds });
            std::stable_sort(ordered.begin(), ordered.end(),
                [](const auto& left, const auto& right) { return left.start < right.start; });
            for (std::size_t index = 1; index < ordered.size(); ++index)
                if (ordered[index].note->id == noteId)
                {
                    pushUndoLocked();
                    const auto connected = ordered[index].note->connectedToPrevious
                        && ordered[index - 1].note->connectedToNext;
                    ordered[index].note->connectedToPrevious = !connected;
                    ordered[index - 1].note->connectedToNext = !connected;
                    changed = true;
                    break;
                }
            if (changed) break;
        }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::applySourceSettings(const juce::File& source,
                                       const std::vector<SampleRegionSetting>& rows)
{
    if (rows.empty()) return;
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        struct Entry { ClipData* clip; NoteData* note; };
        std::vector<Entry> entries;
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.sourceFile == source)
                    for (auto& note : clip.notes) entries.push_back({ &clip, &note });
        std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right)
        {
            return left.clip->sourceOffsetSeconds < right.clip->sourceOffsetSeconds;
        });
        if (entries.empty()) return;
        pushUndoLocked();
        for (std::size_t index = 0; index < std::min(entries.size(), rows.size()); ++index)
        {
            auto& clip = *entries[index].clip;
            auto& note = *entries[index].note;
            const auto& row = rows[index];
            clip.sourceOffsetSeconds = std::max(0.0, row.regionStartSeconds);
            clip.sourceDurationSeconds = std::max(0.001,
                row.regionEndSeconds - row.regionStartSeconds);
            note.gain = juce::jlimit(0.0f, 4.0f,
                static_cast<float>(row.melodyneAmplitude));
            const auto targetPerSource = clip.durationSeconds / clip.sourceDurationSeconds;
            note.consonantSeconds = juce::jlimit(0.0, note.durationSeconds,
                row.fixedDurationSeconds * targetPerSource);
            if (note.sourceMidiCenter >= 0.0f)
                note.midiNote = juce::jlimit(0.0f, 127.0f,
                    note.sourceMidiCenter + static_cast<float>(row.relativePitchCents / 100.0));
            if (row.melodyneData)
            {
                note.drift = juce::jlimit(0.0f, 2.0f,
                    static_cast<float>(row.melodynePitchDrift));
                note.modulation = juce::jlimit(0.0f, 2.0f,
                    static_cast<float>(row.melodynePitchModulation));
                note.formantSemitones = juce::jlimit(-12.0f, 12.0f,
                    static_cast<float>(row.melodyneFormantCents / 100.0));
                note.breath = juce::jlimit(0.0f, 1.0f,
                    static_cast<float>(row.melodyneSibilantBalance));
                note.attackSpeed = juce::jlimit(0.05f, 20.0f,
                    static_cast<float>(row.melodyneAttackSeconds > 1.0e-6
                        ? row.fixedDurationSeconds / row.melodyneAttackSeconds : 1.0));
            }
            changed = true;
        }
    }
    if (changed) sendChangeMessage();
}

juce::ValueTree ProjectModel::toValueTree() const
{
    const auto data = snapshot();
    juce::ValueTree root("HachiShifterProject");
    root.setProperty("version", 3, nullptr);
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
                noteTree.setProperty("tension", note.tension, nullptr);
                noteTree.setProperty("breath", note.breath, nullptr);
                noteTree.setProperty("formantSemitones", note.formantSemitones, nullptr);
                noteTree.setProperty("gain", note.gain, nullptr);
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
                note.tension = static_cast<float>(noteTree.getProperty("tension", 0.0));
                note.breath = static_cast<float>(noteTree.getProperty("breath", 0.0));
                note.formantSemitones = static_cast<float>(noteTree.getProperty("formantSemitones", 0.0));
                note.gain = static_cast<float>(noteTree.getProperty("gain", 1.0));
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
