#include "ProjectModel.h"
#include "SampleSettings.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace hachi
{
float renderedPitchCents(const NoteData& note, const PitchPoint& point)
{
    if (point.hasManualTarget) return point.manualTargetCents;

    // Melodyne stores an exact zero for notes flattened with the pitch
    // modulation tool.  Some analysed source points do not contain a separate
    // pitchWithoutVibrato curve (it is byte-for-byte equal to pitchCent), so
    // the generic drift/modulation decomposition would otherwise leave the
    // original contour untouched.  Treat the saved zero as the authoritative
    // flat-note edit for both display and rendering.
    if (note.modulation <= 1.0e-4f) return 0.0f;

    return note.drift * point.withoutVibratoCents
        + note.modulation * (point.relativeCents - point.withoutVibratoCents);
}

namespace
{
juce::String pitchAlgorithmName(PitchAlgorithm value)
{
    switch (value)
    {
        case PitchAlgorithm::mld5: return "mld5";
        case PitchAlgorithm::mld3: return "mld3";
        case PitchAlgorithm::nsfHifigan: return "nsf-hifigan";
        case PitchAlgorithm::world: return "world";
        case PitchAlgorithm::vocalShifter: return "vslib";
        case PitchAlgorithm::llsm2: return "llsm2";
    }
    return "mld5";
}

PitchAlgorithm parsePitchAlgorithm(const juce::String& value)
{
    if (value == "nsf-hifigan") return PitchAlgorithm::nsfHifigan;
    if (value == "mld3") return PitchAlgorithm::mld3;
    if (value == "world") return PitchAlgorithm::world;
    if (value == "vslib") return PitchAlgorithm::vocalShifter;
    if (value == "llsm2") return PitchAlgorithm::llsm2;
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
        case StretchAlgorithm::nsfShiftThenSplice: return "nsf-shift-then-splice";
    }
    return "melodyne-hybrid";
}

StretchAlgorithm parseStretchAlgorithm(const juce::String& value)
{
    if (value == "variable-mel-hop") return StretchAlgorithm::variableMelHop;
    if (value == "loop") return StretchAlgorithm::loop;
    if (value == "soundtouch") return StretchAlgorithm::soundTouch;
    if (value == "nsf-shift-then-splice") return StretchAlgorithm::nsfShiftThenSplice;
    return StretchAlgorithm::melodyneHybrid;
}

juce::String renderOrderName(RenderOrder value)
{
    switch (value)
    {
        case RenderOrder::processThenSplice: return "process-then-splice";
        case RenderOrder::stretchSpliceThenPitch: return "stretch-splice-then-pitch";
    }
    return "process-then-splice";
}

RenderOrder parseRenderOrder(const juce::String& value)
{
    if (value == "stretch-splice-then-pitch") return RenderOrder::stretchSpliceThenPitch;
    return RenderOrder::processThenSplice;
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

std::uint64_t ProjectModel::revisionNumber() const
{
    const juce::ScopedLock guard(lock);
    return revision;
}

void ProjectModel::pushUndoLocked()
{
    undoHistory.push_back(project);
    if (undoHistory.size() > maxHistory)
        undoHistory.erase(undoHistory.begin());
    redoHistory.clear();
    ++revision;
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
        ++revision;
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
        ++revision;
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
                                        double startSeconds,
                                        const juce::String& targetTrackId)
{
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

    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        const auto target = std::find_if(project.tracks.begin(), project.tracks.end(),
            [&targetTrackId](const auto& track)
            {
                return targetTrackId.isNotEmpty() && track.id == targetTrackId;
            });
        if (target != project.tracks.end())
            target->clips.push_back(std::move(clip));
        else
        {
            TrackData track;
            track.id = makeId("track");
            track.name = file.getFileNameWithoutExtension();
            track.clips.push_back(std::move(clip));
            project.tracks.push_back(std::move(track));
        }
        if (project.name == "Untitled")
            project.name = file.getFileNameWithoutExtension();
    }
    sendChangeMessage();
    return clipId;
}

juce::String ProjectModel::addTrack(const juce::String& requestedName, bool compose)
{
    juce::String id;
    {
        const juce::ScopedLock guard(lock);
        pushUndoLocked();
        TrackData track;
        track.id = makeId("track");
        track.name = requestedName.trim().substring(0, 80);
        if (track.name.isEmpty())
            track.name = compose ? "Melodic Track" : "Audio Track";
        track.compose = compose;
        // A new track follows the currently established project workflow,
        // rather than unexpectedly returning to mld5 after the user has
        // selected NSF/WORLD or a different stretch engine.
        if (!project.tracks.empty())
        {
            track.pitchAlgorithm = project.tracks.back().pitchAlgorithm;
            track.stretchAlgorithm = project.tracks.back().stretchAlgorithm;
            track.renderOrder = project.tracks.back().renderOrder;
        }
        id = track.id;
        project.tracks.push_back(std::move(track));
    }
    sendChangeMessage();
    return id;
}

void ProjectModel::setTrackName(const juce::String& trackId,
                                const juce::String& requestedName)
{
    const auto name = requestedName.trim().substring(0, 80);
    if (name.isEmpty()) return;
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.name != name)
            {
                pushUndoLocked();
                track.name = name;
                changed = true;
                break;
            }
    }
    if (changed) sendChangeMessage();
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

void ProjectModel::setTrackSmoothOverlaps(const juce::String& trackId, bool enabled)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.smoothOverlaps != enabled)
            {
                pushUndoLocked();
                track.smoothOverlaps = enabled;
                changed = true;
                break;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setTrackNormalizeVolume(const juce::String& trackId, bool enabled)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.normalizeVolume != enabled)
            {
                pushUndoLocked();
                track.normalizeVolume = enabled;
                changed = true;
                break;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setPitchAlgorithm(PitchAlgorithm algorithm)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.pitchAlgorithm != algorithm)
            {
                if (!changed) pushUndoLocked();
                track.pitchAlgorithm = algorithm;
                changed = true;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setTrackPitchAlgorithm(const juce::String& trackId,
                                          PitchAlgorithm algorithm)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.pitchAlgorithm != algorithm)
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
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.stretchAlgorithm != algorithm)
            {
                if (!changed) pushUndoLocked();
                track.stretchAlgorithm = algorithm;
                changed = true;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setTrackStretchAlgorithm(const juce::String& trackId,
                                            StretchAlgorithm algorithm)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.stretchAlgorithm != algorithm)
            {
                pushUndoLocked();
                track.stretchAlgorithm = algorithm;
                changed = true;
                break;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setRenderOrder(RenderOrder order)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.renderOrder != order)
            {
                if (!changed) pushUndoLocked();
                track.renderOrder = order;
                changed = true;
            }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setTrackRenderOrder(const juce::String& trackId,
                                       RenderOrder order)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            if (track.id == trackId && track.renderOrder != order)
            {
                pushUndoLocked();
                track.renderOrder = order;
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

juce::String ProjectModel::duplicateClip(const juce::String& clipId,
                                         double startSeconds,
                                         const juce::String& targetTrackId)
{
    juce::String insertedId;
    {
        const juce::ScopedLock guard(lock);
        auto sourceTrackIndex = project.tracks.size();
        ClipData copy;
        auto found = false;
        for (std::size_t trackIndex = 0; trackIndex < project.tracks.size() && !found;
             ++trackIndex)
            for (const auto& clip : project.tracks[trackIndex].clips)
                if (clip.id == clipId)
                {
                    sourceTrackIndex = trackIndex;
                    copy = clip;
                    found = true;
                    break;
                }
        if (!found || sourceTrackIndex >= project.tracks.size()) return {};

        auto destinationTrackIndex = sourceTrackIndex;
        if (targetTrackId.isNotEmpty())
            for (std::size_t trackIndex = 0; trackIndex < project.tracks.size(); ++trackIndex)
                if (project.tracks[trackIndex].id == targetTrackId)
                {
                    destinationTrackIndex = trackIndex;
                    break;
                }

        pushUndoLocked();
        copy.id = makeId("clip");
        copy.startSeconds = startSeconds >= 0.0
            ? startSeconds : copy.startSeconds + copy.durationSeconds;
        copy.startSeconds = std::max(0.0, copy.startSeconds);
        for (auto& note : copy.notes) note.id = makeId("note");

        // Connection flags at the outer edges describe neighbouring notes in
        // the original timeline.  Preserve joins inside the copied clip, but
        // do not accidentally glide into unrelated material at its new place.
        if (!copy.notes.empty())
        {
            const auto first = std::min_element(copy.notes.begin(), copy.notes.end(),
                [](const auto& left, const auto& right)
                {
                    return left.startSeconds < right.startSeconds;
                });
            const auto last = std::max_element(copy.notes.begin(), copy.notes.end(),
                [](const auto& left, const auto& right)
                {
                    return left.startSeconds + left.durationSeconds
                        < right.startSeconds + right.durationSeconds;
                });
            first->connectedToPrevious = false;
            last->connectedToNext = false;
        }
        insertedId = copy.id;
        project.tracks[destinationTrackIndex].clips.push_back(std::move(copy));
    }
    sendChangeMessage();
    return insertedId;
}

void ProjectModel::resizeClip(const juce::String& clipId, double startSeconds,
                              double durationSeconds)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                {
                    const auto oldDuration = std::max(0.01, clip.durationSeconds);
                    const auto nextDuration = std::max(0.01, durationSeconds);
                    const auto nextStart = std::max(0.0, startSeconds);
                    if (std::abs(clip.startSeconds - nextStart) <= 1.0e-9
                        && std::abs(oldDuration - nextDuration) <= 1.0e-9) return;
                    pushUndoLocked();
                    const auto ratio = nextDuration / oldDuration;
                    for (auto& note : clip.notes)
                    {
                        note.startSeconds *= ratio;
                        note.durationSeconds *= ratio;
                        note.consonantSeconds *= ratio;
                        // attackSpeed is the source-time / element-time slope.
                        // Keep the source Attack boundary fixed while its target
                        // position stretches with the rest of the clip.
                        note.attackSpeed = juce::jlimit(0.05f, 20.0f,
                            note.attackSpeed / static_cast<float>(ratio));
                        for (auto& point : note.contour) point.timeSeconds *= ratio;
                        for (auto& marker : note.sibilantMarkers) marker *= ratio;
                    }
                    // The selected source range is unchanged by a timeline
                    // stretch.  Move only the target side of the imported warp
                    // so the original Melodyne Attack/vowel source anchors are
                    // retained exactly.
                    for (auto& point : clip.sourceTimeMap)
                        point.targetSeconds *= ratio;
                    clip.fadeInSeconds = std::min(nextDuration, clip.fadeInSeconds * ratio);
                    clip.fadeOutSeconds = std::min(nextDuration, clip.fadeOutSeconds * ratio);
                    clip.crossfadeInSeconds = std::min(nextDuration, clip.crossfadeInSeconds * ratio);
                    clip.crossfadeOutSeconds = std::min(nextDuration, clip.crossfadeOutSeconds * ratio);
                    clip.startSeconds = nextStart;
                    clip.durationSeconds = nextDuration;
                    changed = true;
                    break;
                }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setClipGain(const juce::String& clipId, float gain)
{
    const auto next = juce::jlimit(0.0f, 4.0f, gain);
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                {
                    if (std::abs(clip.gain - next) <= 1.0e-6f) return;
                    pushUndoLocked();
                    clip.gain = next;
                    changed = true;
                    break;
                }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setClipFades(const juce::String& clipId, double fadeInSeconds,
                                double fadeOutSeconds)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                {
                    const auto nextIn = juce::jlimit(0.0, clip.durationSeconds,
                                                     fadeInSeconds);
                    const auto nextOut = juce::jlimit(0.0, clip.durationSeconds,
                                                      fadeOutSeconds);
                    if (std::abs(clip.fadeInSeconds - nextIn) <= 1.0e-9
                        && std::abs(clip.fadeOutSeconds - nextOut) <= 1.0e-9) return;
                    pushUndoLocked();
                    clip.fadeInSeconds = nextIn;
                    clip.fadeOutSeconds = nextOut;
                    changed = true;
                    break;
                }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setClipMuted(const juce::String& clipId, bool muted)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                {
                    if (clip.muted == muted) return;
                    pushUndoLocked();
                    clip.muted = muted;
                    changed = true;
                    break;
                }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setClipGlideConnected(const juce::String& clipId, bool connectedToNext)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                {
                    if (clip.glideConnectedToNext == connectedToNext) return;
                    pushUndoLocked();
                    clip.glideConnectedToNext = connectedToNext;
                    changed = true;
                    if (connectedToNext)
                    {
                        for (auto& next : track.clips)
                            if (next.id != clip.id
                                && next.startSeconds >= clip.startSeconds
                                    + clip.durationSeconds - 0.01
                                && next.startSeconds <= clip.startSeconds
                                    + clip.durationSeconds + 0.5)
                            {
                                next.glideConnectedFromPrevious = true;
                                break;
                            }
                    }
                    break;
                }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::forceConnectClips(const juce::String& clipA, const juce::String& clipB)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
        {
            ClipData* first = nullptr;
            ClipData* second = nullptr;
            for (auto& clip : track.clips)
            {
                if (clip.id == clipA) first = &clip;
                if (clip.id == clipB) second = &clip;
            }
            if (first == nullptr || second == nullptr) continue;
            if (first->glideConnectedToNext && second->glideConnectedFromPrevious) return;
            pushUndoLocked();
            if (first->startSeconds <= second->startSeconds)
            {
                first->glideConnectedToNext = true;
                second->glideConnectedFromPrevious = true;
            }
            else
            {
                second->glideConnectedToNext = true;
                first->glideConnectedFromPrevious = true;
            }
            changed = true;
            break;
        }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::removeClipGlide(const juce::String& clipId)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                {
                    if (!clip.glideConnectedToNext && !clip.glideConnectedFromPrevious) return;
                    pushUndoLocked();
                    if (clip.glideConnectedToNext)
                    {
                        // Clear the successor's previous flag
                        for (auto& next : track.clips)
                            if (next.glideConnectedFromPrevious
                                && std::abs(next.startSeconds - (clip.startSeconds + clip.durationSeconds)) < 0.01)
                                next.glideConnectedFromPrevious = false;
                    }
                    if (clip.glideConnectedFromPrevious)
                    {
                        // Clear the predecessor's next flag
                        for (auto& prev : track.clips)
                            if (prev.glideConnectedToNext
                                && std::abs(clip.startSeconds - (prev.startSeconds + prev.durationSeconds)) < 0.01)
                                prev.glideConnectedToNext = false;
                    }
                    clip.glideConnectedToNext = false;
                    clip.glideConnectedFromPrevious = false;
                    changed = true;
                    break;
                }
    }
    if (changed) sendChangeMessage();
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

void ProjectModel::setNotesMidi(const std::vector<juce::String>& noteIds, float midiNote)
{
    const auto target = juce::jlimit(0.0f, 127.0f, midiNote);
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
                    changed = changed || (includes(note.id)
                        && std::abs(note.midiNote - target) > 1.0e-6f);
        if (!changed) return;
        pushUndoLocked();
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (includes(note.id)) note.midiNote = target;
    }
    sendChangeMessage();
}

void ProjectModel::averageNotesMidi(const std::vector<juce::String>& noteIds)
{
    const auto includes = [&](const juce::String& id)
    {
        return std::find(noteIds.begin(), noteIds.end(), id) != noteIds.end();
    };
    auto sum = 0.0;
    auto count = 0;
    {
        const juce::ScopedLock guard(lock);
        for (const auto& track : project.tracks)
            for (const auto& clip : track.clips)
                for (const auto& note : clip.notes)
                    if (includes(note.id))
                    {
                        sum += note.midiNote;
                        ++count;
                    }
    }
    if (count > 0) setNotesMidi(noteIds, static_cast<float>(sum / count));
}

void ProjectModel::quantizeNotesMidi(const std::vector<juce::String>& noteIds,
                                     float stepSemitones)
{
    const auto step = juce::jlimit(0.01f, 12.0f, stepSemitones);
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
                    if (includes(note.id))
                    {
                        const auto target = juce::jlimit(0.0f, 127.0f,
                            std::round(note.midiNote / step) * step);
                        changed = changed || std::abs(note.midiNote - target) > 1.0e-6f;
                    }
        if (!changed) return;
        pushUndoLocked();
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (includes(note.id))
                        note.midiNote = juce::jlimit(0.0f, 127.0f,
                            std::round(note.midiNote / step) * step);
    }
    sendChangeMessage();
}

std::vector<juce::String> ProjectModel::insertNotes(
    const juce::String& clipId, const std::vector<NoteData>& noteTemplates,
    double absoluteStartSeconds)
{
    std::vector<juce::String> inserted;
    if (noteTemplates.empty()) return inserted;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                if (clip.id == clipId)
                {
                    if (clip.durationSeconds <= 0.0) return inserted;
                    // Preserve the requested playhead position.  Notes that
                    // reach the target clip edge are cropped below instead of
                    // shifting the whole pasted phrase earlier.
                    const auto localOrigin = juce::jlimit(0.0, clip.durationSeconds,
                        absoluteStartSeconds - clip.startSeconds);
                    std::vector<NoteData> candidates;
                    candidates.reserve(noteTemplates.size());
                    for (const auto& noteTemplate : noteTemplates)
                    {
                        auto note = noteTemplate;
                        note.startSeconds = localOrigin
                            + std::max(0.0, noteTemplate.startSeconds);
                        const auto remaining = clip.durationSeconds - note.startSeconds;
                        if (remaining < 0.01) continue;
                        note.durationSeconds = std::min(
                            std::max(0.01, noteTemplate.durationSeconds), remaining);
                        note.consonantSeconds = std::min(note.consonantSeconds,
                                                        note.durationSeconds);
                        for (auto& point : note.contour)
                            point.timeSeconds = juce::jlimit(0.0,
                                note.durationSeconds, point.timeSeconds);
                        for (auto& marker : note.sibilantMarkers)
                            marker = juce::jlimit(0.0, note.durationSeconds, marker);
                        candidates.push_back(std::move(note));
                    }
                    if (candidates.empty()) return inserted;
                    candidates.front().connectedToPrevious = false;
                    candidates.back().connectedToNext = false;
                    pushUndoLocked();
                    inserted.reserve(candidates.size());
                    for (auto& note : candidates)
                    {
                        note.id = makeId("note");
                        inserted.push_back(note.id);
                        clip.notes.push_back(std::move(note));
                    }
                    std::stable_sort(clip.notes.begin(), clip.notes.end(),
                        [](const auto& left, const auto& right)
                        {
                            return left.startSeconds < right.startSeconds;
                        });
                    break;
                }
    }
    if (!inserted.empty()) sendChangeMessage();
    return inserted;
}

std::vector<juce::String> ProjectModel::duplicateNotes(
    const std::vector<juce::String>& noteIds, const juce::String& targetClipId,
    double absoluteStartSeconds)
{
    const auto data = snapshot();
    std::vector<std::pair<double, NoteData>> found;
    juce::String destination = targetClipId;
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            for (const auto& note : clip.notes)
                if (std::find(noteIds.begin(), noteIds.end(), note.id) != noteIds.end())
                {
                    if (destination.isEmpty()) destination = clip.id;
                    found.emplace_back(clip.startSeconds + note.startSeconds, note);
                }
    if (found.empty() || destination.isEmpty()) return {};
    std::stable_sort(found.begin(), found.end(), [](const auto& left, const auto& right)
    {
        return left.first < right.first;
    });
    const auto origin = found.front().first;
    std::vector<NoteData> templates;
    templates.reserve(found.size());
    for (auto& [absolute, note] : found)
    {
        note.startSeconds = absolute - origin;
        templates.push_back(std::move(note));
    }
    templates.front().connectedToPrevious = false;
    templates.back().connectedToNext = false;
    return insertNotes(destination, templates, absoluteStartSeconds);
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

juce::String ProjectModel::splitNote(const juce::String& noteId, double localSeconds)
{
    juce::String createdId;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
        {
            for (auto& clip : track.clips)
            {
                for (std::size_t noteIndex = 0; noteIndex < clip.notes.size(); ++noteIndex)
                {
                    if (clip.notes[noteIndex].id != noteId) continue;
                    const auto original = clip.notes[noteIndex];
                    const auto split = juce::jlimit(0.01,
                        std::max(0.01, original.durationSeconds - 0.01), localSeconds);
                    if (split <= 0.0099 || split >= original.durationSeconds - 0.0099)
                        return {};

                    const auto evaluate = [&](double time)
                    {
                        PitchPoint result;
                        result.timeSeconds = time;
                        if (original.contour.empty()) return result;
                        const auto right = std::lower_bound(original.contour.begin(),
                            original.contour.end(), time,
                            [](const PitchPoint& point, double value)
                            {
                                return point.timeSeconds < value;
                            });
                        const auto rightIndex = static_cast<std::size_t>(right == original.contour.end()
                            ? original.contour.size() - 1 : right - original.contour.begin());
                        const auto leftIndex = rightIndex > 0
                            && original.contour[rightIndex].timeSeconds > time
                                ? rightIndex - 1 : rightIndex;
                        const auto& left = original.contour[leftIndex];
                        const auto& next = original.contour[rightIndex];
                        const auto amount = next.timeSeconds > left.timeSeconds
                            ? static_cast<float>(juce::jlimit(0.0, 1.0,
                                (time - left.timeSeconds)
                                    / (next.timeSeconds - left.timeSeconds))) : 0.0f;
                        result.relativeCents = left.relativeCents
                            + (next.relativeCents - left.relativeCents) * amount;
                        result.withoutVibratoCents = left.withoutVibratoCents
                            + (next.withoutVibratoCents - left.withoutVibratoCents) * amount;
                        result.voiced = left.voiced && next.voiced;
                        result.manualTargetCents = left.manualTargetCents
                            + (next.manualTargetCents - left.manualTargetCents) * amount;
                        result.hasManualTarget = left.hasManualTarget && next.hasManualTarget;
                        return result;
                    };

                    auto left = original;
                    auto right = original;
                    left.durationSeconds = split;
                    left.consonantSeconds = std::min(original.consonantSeconds, split);
                    left.connectedToNext = false;
                    right.id = makeId("note");
                    createdId = right.id;
                    right.startSeconds = original.startSeconds + split;
                    right.durationSeconds = original.durationSeconds - split;
                    right.consonantSeconds = original.consonantSeconds > split
                        ? original.consonantSeconds - split : 0.0;
                    right.connectedToPrevious = false;

                    left.contour.clear();
                    right.contour.clear();
                    for (const auto& point : original.contour)
                    {
                        if (point.timeSeconds < split - 1.0e-8)
                            left.contour.push_back(point);
                        if (point.timeSeconds > split + 1.0e-8)
                        {
                            auto shifted = point;
                            shifted.timeSeconds -= split;
                            right.contour.push_back(shifted);
                        }
                    }
                    auto boundary = evaluate(split);
                    boundary.timeSeconds = split;
                    left.contour.push_back(boundary);
                    boundary.timeSeconds = 0.0;
                    right.contour.insert(right.contour.begin(), boundary);

                    left.sibilantMarkers.clear();
                    right.sibilantMarkers.clear();
                    for (const auto marker : original.sibilantMarkers)
                        if (marker <= split) left.sibilantMarkers.push_back(marker);
                        else right.sibilantMarkers.push_back(marker - split);

                    pushUndoLocked();
                    clip.notes[noteIndex] = std::move(left);
                    clip.notes.insert(clip.notes.begin()
                        + static_cast<std::ptrdiff_t>(noteIndex + 1), std::move(right));
                    break;
                }
                if (createdId.isNotEmpty()) break;
            }
            if (createdId.isNotEmpty()) break;
        }
    }
    if (createdId.isNotEmpty()) sendChangeMessage();
    return createdId;
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

void ProjectModel::setNoteAttackSpeed(const juce::String& noteId, float attackSpeed)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId)
                    {
                        const auto next = juce::jlimit(0.05f, 20.0f, attackSpeed);
                        if (std::abs(note.attackSpeed - next) <= 1.0e-6f) return;
                        pushUndoLocked();
                        note.attackSpeed = next;
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

void ProjectModel::setNoteRobustPitchCurve(const juce::String& noteId, bool enabled)
{
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                    if (note.id == noteId && note.robustPitchCurve != enabled)
                    {
                        pushUndoLocked();
                        note.robustPitchCurve = enabled;
                        changed = true;
                        break;
                    }
    }
    if (changed) sendChangeMessage();
}

bool ProjectModel::setNotePitchCurve(const juce::String& noteId,
                                     std::vector<PitchCurveEditPoint> points)
{
    if (points.empty()) return false;
    std::stable_sort(points.begin(), points.end(), [](const auto& left, const auto& right)
    {
        return left.timeSeconds < right.timeSeconds;
    });
    auto changed = false;
    {
        const juce::ScopedLock guard(lock);
        for (auto& track : project.tracks)
            for (auto& clip : track.clips)
                for (auto& note : clip.notes)
                {
                    if (note.id != noteId) continue;
                    for (auto& point : points)
                    {
                        point.timeSeconds = juce::jlimit(0.0, note.durationSeconds,
                                                         point.timeSeconds);
                        point.targetMidi = juce::jlimit(0.0f, 127.0f, point.targetMidi);
                    }
                    pushUndoLocked();
                    changed = true;

                    // User-created notes initially contain only two endpoints.
                    // Densify them before drawing so a freehand edit has the
                    // same 5 ms precision as imported Melodyne/FCPE contours.
                    auto needsDensifying = note.contour.size() < 2;
                    for (std::size_t index = 1; index < note.contour.size(); ++index)
                        needsDensifying = needsDensifying
                            || note.contour[index].timeSeconds
                                - note.contour[index - 1].timeSeconds > 0.0075;
                    if (needsDensifying)
                    {
                        const auto original = note.contour;
                        const auto evaluate = [&](double time)
                        {
                            PitchPoint result;
                            result.timeSeconds = time;
                            if (original.empty()) return result;
                            const auto right = std::lower_bound(original.begin(), original.end(), time,
                                [](const PitchPoint& point, double value)
                                {
                                    return point.timeSeconds < value;
                                });
                            const auto rightIndex = static_cast<std::size_t>(right == original.end()
                                ? original.size() - 1 : right - original.begin());
                            const auto leftIndex = rightIndex > 0
                                && original[rightIndex].timeSeconds > time ? rightIndex - 1 : rightIndex;
                            const auto& left = original[leftIndex];
                            const auto& next = original[rightIndex];
                            const auto amount = next.timeSeconds > left.timeSeconds
                                ? static_cast<float>(juce::jlimit(0.0, 1.0,
                                    (time - left.timeSeconds)
                                        / (next.timeSeconds - left.timeSeconds))) : 0.0f;
                            result.relativeCents = left.relativeCents
                                + (next.relativeCents - left.relativeCents) * amount;
                            result.withoutVibratoCents = left.withoutVibratoCents
                                + (next.withoutVibratoCents - left.withoutVibratoCents) * amount;
                            result.voiced = left.voiced && next.voiced;
                            if (left.hasManualTarget && next.hasManualTarget)
                            {
                                result.hasManualTarget = true;
                                result.manualTargetCents = left.manualTargetCents
                                    + (next.manualTargetCents - left.manualTargetCents) * amount;
                            }
                            return result;
                        };
                        note.contour.clear();
                        for (double time = 0.0; time < note.durationSeconds; time += 0.005)
                            note.contour.push_back(evaluate(time));
                        note.contour.push_back(evaluate(note.durationSeconds));
                    }

                    const auto firstTime = points.front().timeSeconds;
                    const auto lastTime = points.back().timeSeconds;
                    const auto targetAt = [&](double time)
                    {
                        if (points.size() == 1) return points.front().targetMidi;
                        const auto right = std::upper_bound(points.begin(), points.end(), time,
                            [](double value, const PitchCurveEditPoint& point)
                            {
                                return value < point.timeSeconds;
                            });
                        if (right == points.begin()) return right->targetMidi;
                        if (right == points.end()) return points.back().targetMidi;
                        const auto& left = *(right - 1);
                        const auto& next = *right;
                        const auto amount = next.timeSeconds > left.timeSeconds
                            ? static_cast<float>((time - left.timeSeconds)
                                / (next.timeSeconds - left.timeSeconds)) : 0.0f;
                        return left.targetMidi + (next.targetMidi - left.targetMidi) * amount;
                    };
                    if (points.size() == 1 && !note.contour.empty())
                    {
                        auto nearest = std::min_element(note.contour.begin(), note.contour.end(),
                            [&](const auto& left, const auto& right)
                            {
                                return std::abs(left.timeSeconds - firstTime)
                                    < std::abs(right.timeSeconds - firstTime);
                            });
                        nearest->manualTargetCents =
                            (points.front().targetMidi - note.midiNote) * 100.0f;
                        nearest->hasManualTarget = true;
                    }
                    else
                        for (auto& point : note.contour)
                            if (point.voiced && point.timeSeconds >= firstTime - 1.0e-7
                                && point.timeSeconds <= lastTime + 1.0e-7)
                            {
                                point.manualTargetCents =
                                    (targetAt(point.timeSeconds) - note.midiNote) * 100.0f;
                                point.hasManualTarget = true;
                            }
                    break;
                }
    }
    if (changed) sendChangeMessage();
    return changed;
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
            note.label = row.name.trim();
            clip.sourceOffsetSeconds = std::max(0.0, row.regionStartSeconds);
            clip.sourceDurationSeconds = std::max(0.001,
                row.regionEndSeconds - row.regionStartSeconds);
            // Source-region editing establishes a new linear mapping.  An old
            // MPD warp refers to the previous source range and must not be
            // silently applied to the newly selected samples.
            clip.sourceTimeMap.clear();
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

juce::ValueTree ProjectModel::toValueTree(const juce::File& projectFile) const
{
    const auto data = snapshot();
    juce::ValueTree root("HachiShifterProject");
    root.setProperty("version", 8, nullptr);
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
        trackTree.setProperty("smoothOverlaps", track.smoothOverlaps, nullptr);
        trackTree.setProperty("normalizeVolume", track.normalizeVolume, nullptr);
        trackTree.setProperty("pitchAlgorithm", pitchAlgorithmName(track.pitchAlgorithm), nullptr);
        trackTree.setProperty("stretchAlgorithm", stretchAlgorithmName(track.stretchAlgorithm), nullptr);
        trackTree.setProperty("renderOrder", renderOrderName(track.renderOrder), nullptr);

        for (const auto& clip : track.clips)
        {
            juce::ValueTree clipTree("Clip");
            clipTree.setProperty("id", clip.id, nullptr);
            clipTree.setProperty("sourceFile", clip.sourceFile.getFullPathName(), nullptr);
            if (projectFile != juce::File{} && clip.sourceFile != juce::File{})
            {
                const auto relative = clip.sourceFile.getRelativePathFrom(
                    projectFile.getParentDirectory());
                if (relative.isNotEmpty() && !juce::File::isAbsolutePath(relative))
                    clipTree.setProperty("sourceFileRelative", relative, nullptr);
            }
            clipTree.setProperty("startSeconds", clip.startSeconds, nullptr);
            clipTree.setProperty("sourceOffsetSeconds", clip.sourceOffsetSeconds, nullptr);
            clipTree.setProperty("sourceDurationSeconds", clip.sourceDurationSeconds, nullptr);
            clipTree.setProperty("durationSeconds", clip.durationSeconds, nullptr);
            clipTree.setProperty("fadeInSeconds", clip.fadeInSeconds, nullptr);
            clipTree.setProperty("fadeOutSeconds", clip.fadeOutSeconds, nullptr);
            clipTree.setProperty("crossfadeInSeconds", clip.crossfadeInSeconds, nullptr);
            clipTree.setProperty("crossfadeOutSeconds", clip.crossfadeOutSeconds, nullptr);
            clipTree.setProperty("gain", clip.gain, nullptr);
            clipTree.setProperty("muted", clip.muted, nullptr);

            for (const auto& point : clip.sourceTimeMap)
            {
                juce::ValueTree pointTree("SourceTimePoint");
                pointTree.setProperty("targetSeconds", point.targetSeconds, nullptr);
                pointTree.setProperty("sourceSeconds", point.sourceSeconds, nullptr);
                clipTree.addChild(pointTree, -1, nullptr);
            }

            for (const auto& note : clip.notes)
            {
                juce::ValueTree noteTree("Note");
                noteTree.setProperty("id", note.id, nullptr);
                noteTree.setProperty("label", note.label, nullptr);
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
                noteTree.setProperty("robustPitchCurve", note.robustPitchCurve, nullptr);
                noteTree.setProperty("connectedToPrevious", note.connectedToPrevious, nullptr);
                noteTree.setProperty("connectedToNext", note.connectedToNext, nullptr);
                for (const auto& point : note.contour)
                {
                    juce::ValueTree pointTree("PitchPoint");
                    pointTree.setProperty("timeSeconds", point.timeSeconds, nullptr);
                    pointTree.setProperty("relativeCents", point.relativeCents, nullptr);
                    pointTree.setProperty("withoutVibratoCents", point.withoutVibratoCents, nullptr);
                    pointTree.setProperty("voiced", point.voiced, nullptr);
                    pointTree.setProperty("manualTargetCents", point.manualTargetCents, nullptr);
                    pointTree.setProperty("hasManualTarget", point.hasManualTarget, nullptr);
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

ProjectData ProjectModel::fromValueTree(const juce::ValueTree& root,
                                        const juce::File& projectFile)
{
    ProjectData data;
    const auto projectDirectory = projectFile.getParentDirectory();
    std::map<juce::String, juce::File> recursiveMedia;
    auto indexedMedia = false;
    const auto resolveSource = [&](const juce::ValueTree& clipTree)
    {
        const auto storedPath = clipTree.getProperty("sourceFile").toString();
        juce::File source(storedPath);
        if (source.existsAsFile()) return source;
        const auto relative = clipTree.getProperty("sourceFileRelative").toString();
        if (relative.isNotEmpty())
        {
            const auto candidate = projectDirectory.getChildFile(relative);
            if (candidate.existsAsFile()) return candidate;
        }
        const auto fileName = source.getFileName();
        if (fileName.isNotEmpty())
        {
            const auto besideProject = projectDirectory.getChildFile(fileName);
            if (besideProject.existsAsFile()) return besideProject;
            if (!indexedMedia && projectDirectory.isDirectory())
            {
                indexedMedia = true;
                juce::Array<juce::File> files;
                projectDirectory.findChildFiles(files, juce::File::findFiles, true);
                for (const auto& file : files)
                    recursiveMedia.try_emplace(file.getFileName().toLowerCase(), file);
            }
            if (const auto found = recursiveMedia.find(fileName.toLowerCase());
                found != recursiveMedia.end()) return found->second;
        }
        return source;
    };
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
        track.smoothOverlaps = static_cast<bool>(trackTree.getProperty("smoothOverlaps", true));
        track.normalizeVolume = static_cast<bool>(trackTree.getProperty("normalizeVolume", false));
        track.pitchAlgorithm = parsePitchAlgorithm(trackTree.getProperty("pitchAlgorithm", "mld5").toString());
        track.stretchAlgorithm = parseStretchAlgorithm(trackTree.getProperty("stretchAlgorithm", "melodyne-hybrid").toString());
        track.renderOrder = parseRenderOrder(trackTree.getProperty("renderOrder", "process-then-splice").toString());

        for (const auto clipTree : trackTree)
        {
            if (!clipTree.hasType("Clip")) continue;
            ClipData clip;
            clip.id = clipTree.getProperty("id").toString();
            clip.sourceFile = resolveSource(clipTree);
            clip.startSeconds = static_cast<double>(clipTree.getProperty("startSeconds", 0.0));
            clip.sourceOffsetSeconds = static_cast<double>(clipTree.getProperty("sourceOffsetSeconds", 0.0));
            clip.sourceDurationSeconds = static_cast<double>(clipTree.getProperty("sourceDurationSeconds", 0.0));
            clip.durationSeconds = static_cast<double>(clipTree.getProperty("durationSeconds", 1.0));
            clip.fadeInSeconds = static_cast<double>(clipTree.getProperty("fadeInSeconds", 0.0));
            clip.fadeOutSeconds = static_cast<double>(clipTree.getProperty("fadeOutSeconds", 0.0));
            clip.crossfadeInSeconds = static_cast<double>(clipTree.getProperty("crossfadeInSeconds", 0.0));
            clip.crossfadeOutSeconds = static_cast<double>(clipTree.getProperty("crossfadeOutSeconds", 0.0));
            clip.gain = static_cast<float>(clipTree.getProperty("gain", 1.0));
            clip.muted = static_cast<bool>(clipTree.getProperty("muted", false));

            for (const auto noteTree : clipTree)
            {
                if (noteTree.hasType("SourceTimePoint"))
                {
                    clip.sourceTimeMap.push_back({
                        static_cast<double>(noteTree.getProperty("targetSeconds", 0.0)),
                        static_cast<double>(noteTree.getProperty("sourceSeconds", 0.0)) });
                    continue;
                }
                if (!noteTree.hasType("Note")) continue;
                NoteData note;
                note.id = noteTree.getProperty("id").toString();
                note.label = noteTree.getProperty("label").toString();
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
                note.robustPitchCurve = static_cast<bool>(
                    noteTree.getProperty("robustPitchCurve", false));
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
                                                 static_cast<bool>(child.getProperty("voiced", true)),
                                                 static_cast<float>(child.getProperty("manualTargetCents", 0.0)),
                                                 static_cast<bool>(child.getProperty("hasManualTarget", false)) });
                    }
                    else if (child.hasType("Sibilant"))
                        note.sibilantMarkers.push_back(static_cast<double>(child.getProperty("timeSeconds", 0.0)));
                }
                clip.notes.push_back(std::move(note));
            }
            std::stable_sort(clip.sourceTimeMap.begin(), clip.sourceTimeMap.end(),
                [](const auto& left, const auto& right)
                {
                    return left.targetSeconds < right.targetSeconds;
                });
            auto previousTarget = -1.0;
            auto previousSource = -1.0;
            std::erase_if(clip.sourceTimeMap, [&](auto& point)
            {
                point.targetSeconds = juce::jlimit(0.0, clip.durationSeconds,
                                                   point.targetSeconds);
                point.sourceSeconds = juce::jlimit(0.0,
                    clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds
                                                     : clip.durationSeconds,
                    point.sourceSeconds);
                const auto invalid = !std::isfinite(point.targetSeconds)
                    || !std::isfinite(point.sourceSeconds)
                    || point.targetSeconds <= previousTarget + 1.0e-9
                    || point.sourceSeconds < previousSource - 1.0e-9;
                if (!invalid)
                {
                    previousTarget = point.targetSeconds;
                    previousSource = point.sourceSeconds;
                }
                return invalid;
            });
            if (clip.sourceTimeMap.size() < 2) clip.sourceTimeMap.clear();
            track.clips.push_back(std::move(clip));
        }
        data.tracks.push_back(std::move(track));
    }
    return data;
}

bool ProjectModel::save(const juce::File& file, juce::String& error) const
{
    error.clear();
    if (auto stream = file.createOutputStream())
    {
        stream->setPosition(0);
        stream->truncate();
        toValueTree(file).writeToStream(*stream);
        stream->flush();
        return true;
    }
    error = "Could not write " + file.getFullPathName();
    return false;
}

bool ProjectModel::load(const juce::File& file, juce::String& error)
{
    error.clear();
    if (auto stream = file.createInputStream())
    {
        auto tree = juce::ValueTree::readFromStream(*stream);
        if (tree.hasType("HachiShifterProject"))
        {
            auto loaded = fromValueTree(tree, file);
            juce::StringArray missing;
            for (const auto& track : loaded.tracks)
                for (const auto& clip : track.clips)
                    if (!clip.sourceFile.existsAsFile())
                        missing.addIfNotAlreadyThere(clip.sourceFile.getFullPathName());
            replace(std::move(loaded));
            if (!missing.isEmpty())
                error = missing.joinIntoString("\n");
            return true;
        }
    }
    error = "Invalid HachiShifter Next project: " + file.getFullPathName();
    return false;
}
}
