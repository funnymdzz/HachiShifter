#include "AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace hachi
{
namespace
{
std::optional<std::pair<float, float>> contourAt(const NoteData& note, double localSeconds)
{
    if (note.contour.empty()) return std::pair { 0.0f, 0.0f };
    const auto right = std::lower_bound(note.contour.begin(), note.contour.end(), localSeconds,
        [](const PitchPoint& point, double time) { return point.timeSeconds < time; });
    const auto rightIndex = right == note.contour.end()
        ? note.contour.size() - 1 : static_cast<std::size_t>(std::distance(note.contour.begin(), right));
    const auto leftIndex = rightIndex > 0 && note.contour[rightIndex].timeSeconds > localSeconds
        ? rightIndex - 1 : rightIndex;
    const auto& left = note.contour[leftIndex];
    const auto& next = note.contour[rightIndex];
    if (!left.voiced || !next.voiced) return std::nullopt;
    const auto amount = next.timeSeconds > left.timeSeconds
        ? static_cast<float>(juce::jlimit(0.0, 1.0,
            (localSeconds - left.timeSeconds) / (next.timeSeconds - left.timeSeconds))) : 0.0f;
    const auto source = left.relativeCents
        + (next.relativeCents - left.relativeCents) * amount;
    const auto leftTarget = renderedPitchCents(note, left);
    const auto rightTarget = renderedPitchCents(note, next);
    return std::pair { source, leftTarget + (rightTarget - leftTarget) * amount };
}

std::pair<float, float> panGains(float pan, bool mono)
{
    pan = juce::jlimit(-1.0f, 1.0f, pan);
    if (mono)
        return { std::sqrt(0.5f * (1.0f - pan)),
                 std::sqrt(0.5f * (1.0f + pan)) };
    // Stereo tracks use a balance law: centre must preserve both source
    // channels at unity instead of applying an unintended -3 dB attenuation.
    return { pan > 0.0f ? std::sqrt(1.0f - pan) : 1.0f,
             pan < 0.0f ? std::sqrt(1.0f + pan) : 1.0f };
}

backend::Mld5FileRenderRequest makeRenderRequest(const ClipData& clip, const TrackData& track,
                                                  const juce::File& hifiganModelDirectory,
                                                  const backend::OrtExecutionConfig& inference,
                                                  bool connectedToPreviousClip = false,
                                                  bool connectedToNextClip = false)
{
    backend::Mld5FileRenderRequest request;
    request.sourceFile = clip.sourceFile;
    request.sourceOffsetSeconds = clip.sourceOffsetSeconds;
    request.sourceDurationSeconds = clip.sourceDurationSeconds > 1.0e-9
        ? clip.sourceDurationSeconds : clip.durationSeconds;
    request.targetDurationSeconds = clip.durationSeconds;
    request.hifiganModelDirectory = hifiganModelDirectory;
    request.inference = inference;
    switch (track.pitchAlgorithm)
    {
        case PitchAlgorithm::nsfHifigan:
            request.pitchBackend = backend::PitchRenderBackend::nsfHifigan;
            break;
        case PitchAlgorithm::mld3:
            request.pitchBackend = backend::PitchRenderBackend::mld3;
            break;
        case PitchAlgorithm::world:
            request.pitchBackend = backend::PitchRenderBackend::world;
            break;
        case PitchAlgorithm::vocalShifter:
            request.pitchBackend = backend::PitchRenderBackend::vslib;
            break;
        case PitchAlgorithm::llsm2:
            request.pitchBackend = backend::PitchRenderBackend::llsm2;
            break;
        case PitchAlgorithm::mld5:
        default:
            request.pitchBackend = backend::PitchRenderBackend::mld5;
            break;
    }
    request.stretchAlgorithm = static_cast<int>(track.stretchAlgorithm);
    request.normalizeVolume = track.normalizeVolume;
    const auto sourceDuration = request.sourceDurationSeconds;
    std::vector<backend::TimeMapPoint> timeAnchors;
    if (!clip.sourceTimeMap.empty())
    {
        timeAnchors.reserve(clip.sourceTimeMap.size());
        for (const auto& point : clip.sourceTimeMap)
            timeAnchors.push_back({ juce::jlimit(0.0, clip.durationSeconds, point.targetSeconds),
                                    juce::jlimit(0.0, sourceDuration, point.sourceSeconds) });
    }
    else
    {
        timeAnchors.push_back({ 0.0, 0.0 });
        for (const auto& note : clip.notes)
        {
            const auto noteTargetStart = juce::jlimit(0.0, clip.durationSeconds, note.startSeconds);
            const auto sourceStart = clip.durationSeconds > 1.0e-9
                ? noteTargetStart / clip.durationSeconds * sourceDuration : 0.0;
            timeAnchors.push_back({ noteTargetStart, sourceStart });
            if (note.consonantSeconds <= 1.0e-6 || note.attackSpeed <= 1.0e-6f) continue;
            const auto targetAttack = juce::jlimit(noteTargetStart, clip.durationSeconds,
                noteTargetStart + note.consonantSeconds);
            const auto sourceAttack = juce::jlimit(sourceStart, sourceDuration,
                sourceStart + note.consonantSeconds * static_cast<double>(note.attackSpeed));
            timeAnchors.push_back({ targetAttack, sourceAttack });
        }
    }
    timeAnchors.push_back({ clip.durationSeconds, sourceDuration });
    std::stable_sort(timeAnchors.begin(), timeAnchors.end(), [](const auto& left, const auto& right)
    {
        if (std::abs(left.targetSeconds - right.targetSeconds) > 1.0e-9)
            return left.targetSeconds < right.targetSeconds;
        return left.sourceSeconds < right.sourceSeconds;
    });
    for (const auto& anchor : timeAnchors)
    {
        if (request.timeMap.empty())
        {
            request.timeMap.push_back(anchor);
            continue;
        }
        auto& previous = request.timeMap.back();
        if (std::abs(anchor.targetSeconds - previous.targetSeconds) <= 1.0e-7)
        {
            previous.sourceSeconds = std::max(previous.sourceSeconds, anchor.sourceSeconds);
            continue;
        }
        if (anchor.sourceSeconds > previous.sourceSeconds + 1.0e-7)
            request.timeMap.push_back(anchor);
    }
    if (request.timeMap.empty() || request.timeMap.back().targetSeconds < clip.durationSeconds - 1.0e-7)
        request.timeMap.push_back({ clip.durationSeconds, sourceDuration });
    constexpr auto framePeriodSeconds = 0.005;
    request.framePeriodMs = framePeriodSeconds * 1000.0;
    const auto frameCount = std::max(2, static_cast<int>(std::ceil(clip.durationSeconds
                                                                   / framePeriodSeconds)) + 1);
    request.sourceMidi.resize(static_cast<std::size_t>(frameCount), 0.0f);
    request.targetMidi.resize(static_cast<std::size_t>(frameCount), 0.0f);
    request.formantSemitones.resize(static_cast<std::size_t>(frameCount), 0.0f);
    request.noteGain.resize(static_cast<std::size_t>(frameCount), 1.0f);
    request.tension.resize(static_cast<std::size_t>(frameCount), 0.0f);
    request.breath.resize(static_cast<std::size_t>(frameCount), 0.0f);
    request.robustPitchCurve.resize(static_cast<std::size_t>(frameCount), 0.0f);
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto time = std::min(clip.durationSeconds, static_cast<double>(frame) * framePeriodSeconds);
        for (std::size_t noteIndex = 0; noteIndex < clip.notes.size(); ++noteIndex)
        {
            const auto& note = clip.notes[noteIndex];
            const auto local = time - note.startSeconds;
            if (local < -1.0e-9 || local > note.durationSeconds + 1.0e-9) continue;
            request.formantSemitones[static_cast<std::size_t>(frame)] = note.formantSemitones;
            request.noteGain[static_cast<std::size_t>(frame)] = note.gain;
            request.tension[static_cast<std::size_t>(frame)] = note.tension;
            request.breath[static_cast<std::size_t>(frame)] = note.breath;
            // Keep adjacent robust notes as separate detector regions.  A
            // boolean mask alone lets a slope limiter bridge two valid notes
            // at a hard musical interval and mistakes it for an F0 outlier.
            // Zero remains disabled; positive values identify the owning note.
            request.robustPitchCurve[static_cast<std::size_t>(frame)] =
                note.robustPitchCurve ? static_cast<float>(noteIndex + 1) : 0.0f;
            const auto cents = contourAt(note, juce::jlimit(0.0, note.durationSeconds, local));
            if (!cents) break;
            const auto sourceCenter = note.sourceMidiCenter >= 0.0f ? note.sourceMidiCenter : note.midiNote;
            request.sourceMidi[static_cast<std::size_t>(frame)] = sourceCenter + cents->first / 100.0f;
            request.targetMidi[static_cast<std::size_t>(frame)] = note.midiNote
                + cents->second / 100.0f;
            break;
        }
    }

    for (const auto& joinedNote : clip.notes)
    {
        const auto joinsHere = joinedNote.connectedToPrevious || connectedToPreviousClip;
        if (!joinsHere) continue;
        const NoteData* previousNote = nullptr;
        auto previousEnd = -std::numeric_limits<double>::infinity();
        const auto joinedStart = clip.startSeconds + joinedNote.startSeconds;
        for (const auto& candidateClip : track.clips)
            for (const auto& candidate : candidateClip.notes)
            {
                const auto end = candidateClip.startSeconds + candidate.startSeconds
                    + candidate.durationSeconds;
                // The joined partner may overlap the note head (a CVVC connector
                // sits under the previous vowel tail), so accept partners whose
                // end lands no more than 80 ms past the join start and take the
                // closest one.
                if (end <= joinedStart + 0.08
                    && end > previousEnd && candidate.id != joinedNote.id)
                {
                    previousEnd = end;
                    previousNote = &candidate;
                }
            }
        if (previousNote != nullptr)
        {
            const auto previousCents = contourAt(*previousNote, previousNote->durationSeconds);
            const auto previousPitch = previousNote->midiNote + (previousCents
                ? previousCents->second / 100.0f
                : 0.0f);
            const auto joinSeconds = std::min(0.08,
                std::max(0.012, joinedNote.durationSeconds * 0.22));
            const auto firstFrame = juce::jlimit(0, frameCount - 1,
                static_cast<int>(std::llround(joinedNote.startSeconds / framePeriodSeconds)));
            const auto joinFrames = std::min(frameCount - firstFrame,
                std::max(2, static_cast<int>(std::ceil(joinSeconds / framePeriodSeconds))));
            if (joinFrames < 2) continue;
            for (int frame = 0; frame < joinFrames; ++frame)
            {
                auto& target = request.targetMidi[static_cast<std::size_t>(firstFrame + frame)];
                if (!(target > 0.0f)) continue;
                const auto x = static_cast<float>(frame) / static_cast<float>(joinFrames - 1);
                const auto smooth = x * x * (3.0f - 2.0f * x);
                target = previousPitch + (target - previousPitch) * smooth;
            }
        }
    }
    return request;
}

std::string renderKey(const ClipData& clip, const TrackData& track,
                      const juce::File& hifiganModelDirectory,
                      const backend::OrtExecutionConfig& inference,
                      bool connectedToPreviousClip = false,
                      bool connectedToNextClip = false,
                      const ClipData* previousNeighbour = nullptr,
                      const ClipData* nextNeighbour = nullptr,
                      RenderOrder renderOrder = RenderOrder::processThenSplice)
{
    juce::MemoryOutputStream stream;
    const auto path = clip.sourceFile.getFullPathName().toUTF8();
    stream.write(path.getAddress(), path.sizeInBytes());
    stream.writeInt64(clip.sourceFile.getLastModificationTime().toMilliseconds());
    stream.writeDouble(clip.sourceOffsetSeconds);
    stream.writeDouble(clip.sourceDurationSeconds);
    stream.writeDouble(clip.durationSeconds);
    stream.writeInt64(static_cast<juce::int64>(clip.sourceTimeMap.size()));
    for (const auto& point : clip.sourceTimeMap)
    {
        stream.writeDouble(point.targetSeconds);
        stream.writeDouble(point.sourceSeconds);
    }
    stream.writeInt(static_cast<int>(track.pitchAlgorithm));
    stream.writeInt(static_cast<int>(track.stretchAlgorithm));
    stream.writeInt(static_cast<int>(renderOrder));
    stream.writeBool(track.normalizeVolume);
    stream.writeByte(static_cast<char>(connectedToPreviousClip ? 1 : 0));
    stream.writeByte(static_cast<char>(connectedToNextClip ? 1 : 0));
    // A boundary f0 glide and a reduced neural guard make a clip's render depend
    // on the pitch of its connected partners, so key on a compact summary of
    // those neighbours as well.
    const auto writeNeighbour = [&stream](const ClipData* neighbour)
    {
        if (neighbour == nullptr)
        {
            stream.writeByte(0);
            return;
        }
        stream.writeByte(1);
        stream.writeDouble(neighbour->startSeconds);
        stream.writeDouble(neighbour->durationSeconds);
        for (const auto& note : neighbour->notes)
        {
            stream.writeFloat(note.midiNote);
            stream.writeFloat(note.sourceMidiCenter);
            if (note.contour.empty())
            {
                stream.writeByte(0);
                continue;
            }
            stream.writeByte(1);
            const auto& tail = note.contour.back();
            stream.writeDouble(tail.timeSeconds);
            stream.writeFloat(tail.relativeCents);
            stream.writeFloat(tail.withoutVibratoCents);
        }
    };
    writeNeighbour(previousNeighbour);
    writeNeighbour(nextNeighbour);
    const auto modelPath = hifiganModelDirectory.getFullPathName().toUTF8();
    stream.write(modelPath.getAddress(), modelPath.sizeInBytes());
    const auto modelDirectory = hifiganModelDirectory.existsAsFile()
        ? hifiganModelDirectory.getParentDirectory() : hifiganModelDirectory;
    const auto model = modelDirectory.getChildFile("pc_nsf_hifigan.onnx");
    const auto config = modelDirectory.getChildFile("config.json");
    stream.writeInt64(model.getLastModificationTime().toMilliseconds());
    stream.writeInt64(model.getSize());
    stream.writeInt64(config.getLastModificationTime().toMilliseconds());
    stream.writeInt64(config.getSize());
    stream.writeInt(static_cast<int>(inference.requested));
    stream.writeInt(inference.deviceIndex);
    stream.writeInt(inference.intraOpThreads);
    for (const auto& note : clip.notes)
    {
        stream.writeDouble(note.startSeconds);
        stream.writeDouble(note.durationSeconds);
        stream.writeFloat(note.midiNote);
        stream.writeFloat(note.sourceMidiCenter);
        stream.writeDouble(note.consonantSeconds);
        stream.writeFloat(note.attackSpeed);
        stream.writeByte(static_cast<char>(note.robustPitchCurve ? 1 : 0));
        stream.writeByte(static_cast<char>(note.connectedToPrevious ? 1 : 0));
        stream.writeByte(static_cast<char>(note.connectedToNext ? 1 : 0));
        stream.writeFloat(note.modulation);
        stream.writeFloat(note.drift);
        stream.writeFloat(note.tension);
        stream.writeFloat(note.breath);
        stream.writeFloat(note.formantSemitones);
        stream.writeFloat(note.gain);
        for (const auto& point : note.contour)
        {
            stream.writeDouble(point.timeSeconds);
            stream.writeFloat(point.relativeCents);
            stream.writeFloat(point.withoutVibratoCents);
            stream.writeByte(static_cast<char>(point.voiced ? 1 : 0));
            stream.writeFloat(point.manualTargetCents);
            stream.writeByte(static_cast<char>(point.hasManualTarget ? 1 : 0));
        }
    }
    return std::string(static_cast<const char*>(stream.getData()), stream.getDataSize());
}

// stretch-splice-then-pitch: render a whole connected phrase as one request.
// The source is the single contiguous media range spanning every element's
// selected source region; the time map maps each element's target seconds onto
// its own source seconds (concatenated in target time), and the pitch/formant
// curves are sampled from the element owning each target frame with a smooth
// glide across every seam.  One NSF-HiFiGAN decode then covers the whole phrase
// with continuous mel/F0/phase, and the caller cuts the result back into
// per-element buffers.
backend::Mld5FileRenderRequest makeMergedRenderRequest(
    const std::vector<const ClipData*>& group, const TrackData& track,
    const juce::File& hifiganModelDirectory,
    const backend::OrtExecutionConfig& inference)
{
    backend::Mld5FileRenderRequest request;
    request.sourceFile = group.front()->sourceFile;
    auto sourceStart = std::numeric_limits<double>::max();
    auto sourceEnd = 0.0;
    std::vector<double> targetOffsets;
    targetOffsets.reserve(group.size());
    auto targetDuration = 0.0;
    for (const auto* clip : group)
    {
        targetOffsets.push_back(targetDuration);
        targetDuration += clip->durationSeconds;
        sourceStart = std::min(sourceStart, clip->sourceOffsetSeconds);
        sourceEnd = std::max(sourceEnd, clip->sourceOffsetSeconds + clip->sourceDurationSeconds);
    }
    request.sourceOffsetSeconds = sourceStart;
    request.sourceDurationSeconds = std::max(1.0e-6, sourceEnd - sourceStart);
    request.targetDurationSeconds = targetDuration;
    request.hifiganModelDirectory = hifiganModelDirectory;
    request.inference = inference;
    request.pitchBackend = backend::PitchRenderBackend::nsfHifigan;
    request.stretchAlgorithm = static_cast<int>(track.stretchAlgorithm);
    request.normalizeVolume = track.normalizeVolume;
    const auto preserveSourceSeams = track.stretchAlgorithm == StretchAlgorithm::variableMelHop
        || track.stretchAlgorithm == StretchAlgorithm::nsfShiftThenSplice;

    for (std::size_t index = 0; index < group.size(); ++index)
    {
        const auto& clip = *group[index];
        const auto targetOffset = targetOffsets[index];
        const auto sourceOffset = clip.sourceOffsetSeconds - sourceStart;
        std::vector<backend::TimeMapPoint> localAnchors;
        if (!clip.sourceTimeMap.empty())
        {
            for (const auto& point : clip.sourceTimeMap)
                localAnchors.push_back({
                    juce::jlimit(0.0, clip.durationSeconds, point.targetSeconds),
                    juce::jlimit(0.0, clip.sourceDurationSeconds, point.sourceSeconds) });
        }
        else
        {
            for (const auto& note : clip.notes)
            {
                const auto noteStart = juce::jlimit(0.0, clip.durationSeconds, note.startSeconds);
                const auto srcStart = clip.durationSeconds > 1.0e-9
                    ? noteStart / clip.durationSeconds * clip.sourceDurationSeconds : 0.0;
                localAnchors.push_back({ noteStart, srcStart });
                if (note.consonantSeconds <= 1.0e-6 || note.attackSpeed <= 1.0e-6f) continue;
                const auto targetAttack = juce::jlimit(noteStart, clip.durationSeconds,
                    noteStart + note.consonantSeconds);
                const auto sourceAttack = juce::jlimit(srcStart, clip.sourceDurationSeconds,
                    srcStart + note.consonantSeconds * static_cast<double>(note.attackSpeed));
                localAnchors.push_back({ targetAttack, sourceAttack });
            }
        }
        localAnchors.push_back({ 0.0, 0.0 });
        localAnchors.push_back({ clip.durationSeconds, clip.sourceDurationSeconds });
        std::stable_sort(localAnchors.begin(), localAnchors.end(), [](const auto& left,
                                                                      const auto& right)
        {
            if (std::abs(left.targetSeconds - right.targetSeconds) > 1.0e-9)
                return left.targetSeconds < right.targetSeconds;
            return left.sourceSeconds < right.sourceSeconds;
        });
        std::vector<backend::TimeMapPoint> localMap;
        for (const auto& anchor : localAnchors)
        {
            if (localMap.empty())
            {
                localMap.push_back(anchor);
                continue;
            }
            auto& previous = localMap.back();
            if (std::abs(anchor.targetSeconds - previous.targetSeconds) <= 1.0e-7)
            {
                previous.sourceSeconds = std::max(previous.sourceSeconds, anchor.sourceSeconds);
                continue;
            }
            if (anchor.sourceSeconds > previous.sourceSeconds + 1.0e-7)
                localMap.push_back(anchor);
        }
        for (const auto& anchor : localMap)
        {
            const backend::TimeMapPoint mapped {
                targetOffset + anchor.targetSeconds,
                sourceOffset + anchor.sourceSeconds
            };
            if (!request.timeMap.empty()
                && std::abs(mapped.targetSeconds - request.timeMap.back().targetSeconds) <= 1.0e-7
                && std::abs(mapped.sourceSeconds - request.timeMap.back().sourceSeconds) <= 1.0e-7)
                continue;
            // Keep both sides of a source discontinuity at an element seam.
            // The variable-hop renderer uses their order to select the old
            // source before the seam and the new source immediately after it.
            request.timeMap.push_back(mapped);
        }
    }
    if (!preserveSourceSeams)
    {
        auto anchors = std::move(request.timeMap);
        std::stable_sort(anchors.begin(), anchors.end(), [](const auto& left, const auto& right)
        {
            if (std::abs(left.targetSeconds - right.targetSeconds) > 1.0e-9)
                return left.targetSeconds < right.targetSeconds;
            return left.sourceSeconds < right.sourceSeconds;
        });
        for (const auto& anchor : anchors)
        {
            if (request.timeMap.empty())
            {
                request.timeMap.push_back(anchor);
                continue;
            }
            auto& previous = request.timeMap.back();
            if (std::abs(anchor.targetSeconds - previous.targetSeconds) <= 1.0e-7)
            {
                previous.sourceSeconds = std::max(previous.sourceSeconds, anchor.sourceSeconds);
                continue;
            }
            if (anchor.sourceSeconds > previous.sourceSeconds + 1.0e-7)
                request.timeMap.push_back(anchor);
        }
    }
    if (request.timeMap.empty() || request.timeMap.back().targetSeconds < targetDuration - 1.0e-7)
        request.timeMap.push_back({ targetDuration, sourceEnd - sourceStart });

    constexpr auto framePeriodSeconds = 0.005;
    request.framePeriodMs = framePeriodSeconds * 1000.0;
    const auto frameCount = std::max(2, static_cast<int>(std::ceil(targetDuration / framePeriodSeconds)) + 1);
    request.sourceMidi.assign(static_cast<std::size_t>(frameCount), 0.0f);
    request.targetMidi.assign(static_cast<std::size_t>(frameCount), 0.0f);
    request.formantSemitones.assign(static_cast<std::size_t>(frameCount), 0.0f);
    request.noteGain.assign(static_cast<std::size_t>(frameCount), 1.0f);
    request.tension.assign(static_cast<std::size_t>(frameCount), 0.0f);
    request.breath.assign(static_cast<std::size_t>(frameCount), 0.0f);
    request.robustPitchCurve.assign(static_cast<std::size_t>(frameCount), 0.0f);
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto time = std::min(targetDuration, static_cast<double>(frame) * framePeriodSeconds);
        std::size_t index = 0;
        while (index + 1 < group.size() && targetOffsets[index + 1] <= time) ++index;
        const auto& clip = *group[index];
        const auto local = time - targetOffsets[index];
        for (const auto& note : clip.notes)
        {
            const auto noteLocal = local - note.startSeconds;
            if (noteLocal < -1.0e-9 || noteLocal > note.durationSeconds + 1.0e-9) continue;
            request.formantSemitones[static_cast<std::size_t>(frame)] = note.formantSemitones;
            request.noteGain[static_cast<std::size_t>(frame)] = note.gain;
            request.tension[static_cast<std::size_t>(frame)] = note.tension;
            request.breath[static_cast<std::size_t>(frame)] = note.breath;
            request.robustPitchCurve[static_cast<std::size_t>(frame)] =
                note.robustPitchCurve ? static_cast<float>(index + 1) : 0.0f;
            const auto cents = contourAt(note, juce::jlimit(0.0, note.durationSeconds, noteLocal));
            if (!cents) break;
            const auto sourceCenter = note.sourceMidiCenter >= 0.0f ? note.sourceMidiCenter : note.midiNote;
            request.sourceMidi[static_cast<std::size_t>(frame)] = sourceCenter + cents->first / 100.0f;
            request.targetMidi[static_cast<std::size_t>(frame)] = note.midiNote + cents->second / 100.0f;
            break;
        }
    }
    // Continuous pitch line: glide every seam from the previous element's tail
    // pitch into the next element so the single decode never sees a hard f0
    // step inside the phrase.
    for (std::size_t index = 1; index < group.size(); ++index)
    {
        const auto& prevClip = *group[index - 1];
        const auto& nextClip = *group[index];
        auto prevPitch = prevClip.notes.empty() ? 0.0 : prevClip.notes.back().midiNote;
        if (!prevClip.notes.empty())
        {
            const auto& note = prevClip.notes.back();
            const auto cents = contourAt(note, note.durationSeconds);
            prevPitch = note.midiNote + (cents ? cents->second / 100.0f : 0.0f);
        }
        const auto joinSeconds = std::min(0.08, std::max(0.012, nextClip.durationSeconds * 0.22));
        const auto firstFrame = juce::jlimit(0, frameCount - 1,
            static_cast<int>(std::llround(targetOffsets[index] / framePeriodSeconds)));
        const auto joinFrames = std::min(frameCount - firstFrame,
            std::max(2, static_cast<int>(std::ceil(joinSeconds / framePeriodSeconds))));
        if (joinFrames < 2) continue;
        for (int frame = 0; frame < joinFrames; ++frame)
        {
            auto& target = request.targetMidi[static_cast<std::size_t>(firstFrame + frame)];
            if (!(target > 0.0f)) continue;
            const auto x = static_cast<float>(frame) / static_cast<float>(joinFrames - 1);
            const auto smooth = x * x * (3.0f - 2.0f * x);
            target = static_cast<float>(prevPitch + (target - prevPitch) * smooth);
        }
    }
    return request;
}

std::string mergedRenderKey(const std::vector<const ClipData*>& group, const TrackData& track,
                            const juce::File& hifiganModelDirectory,
                            const backend::OrtExecutionConfig& inference)
{
    std::string key = "merged|" + std::to_string(static_cast<int>(track.renderOrder)) + "|";
    for (const auto* clip : group)
        key += renderKey(*clip, track, hifiganModelDirectory, inference) + ";";
    return key;
}
}

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats();
    sourcePlayer.setSource(this);
    deviceManager.initialiseWithDefaultDevices(0, 2);
    deviceManager.addAudioCallback(&sourcePlayer);
}

AudioEngine::~AudioEngine()
{
    sourcePlayer.setSource(nullptr);
    deviceManager.removeAudioCallback(&sourcePlayer);
}

bool AudioEngine::ensureOutputDevice(juce::String& error)
{
    const auto hasOutput = [this]
    {
        const auto* device = deviceManager.getCurrentAudioDevice();
        return device != nullptr
            && device->getActiveOutputChannels().countNumberOfSetBits() > 0;
    };
    if (hasOutput())
    {
        error.clear();
        return true;
    }
    // A driver may appear after startup (USB interface connected, Bluetooth
    // endpoint enabled, or Windows device service restarted).  Retry here so
    // Play does not enter a false playing state with no callback to advance
    // the transport.
    error = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (hasOutput())
    {
        error.clear();
        return true;
    }
    if (error.isEmpty()) error = "No audio output device is available";
    return false;
}

void AudioEngine::restoreDeviceState(juce::PropertiesFile& properties)
{
    const auto saved = properties.getValue("audio.deviceState");
    if (saved.isEmpty()) return;
    const auto xml = juce::parseXML(saved);
    if (xml == nullptr) return;
    // Keep the application usable if a previously selected interface has
    // been unplugged; JUCE then selects the current system default.
    deviceManager.initialise(0, 2, xml.get(), true);
}

void AudioEngine::saveDeviceState(juce::PropertiesFile& properties) const
{
    if (const auto state = deviceManager.createStateXml())
    {
        properties.setValue("audio.deviceState", state->toString());
        properties.saveIfNeeded();
    }
}

void AudioEngine::prepareToPlay(int, double sampleRate)
{
    outputSampleRate.store(sampleRate > 0.0 ? sampleRate : 48'000.0);
}

void AudioEngine::releaseResources()
{
}

std::optional<double> AudioEngine::probeDuration(const juce::File& file)
{
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file)))
        return static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    return std::nullopt;
}

bool AudioEngine::setAuditionFile(const juce::File& file)
{
    auto reader = std::shared_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file));
    if (reader == nullptr) return false;
    stop();
    {
        const juce::ScopedWriteLock guard(renderLock);
        auditionReader = std::move(reader);
        auditionScratch.setSize(juce::jlimit(1, 2, static_cast<int>(auditionReader->numChannels)), 2);
        auditionMode.store(true);
    }
    timelineSample.store(0);
    sendChangeMessage();
    return true;
}

void AudioEngine::clearAuditionFile()
{
    stop();
    {
        const juce::ScopedWriteLock guard(renderLock);
        auditionReader.reset();
        auditionScratch.setSize(0, 0);
        auditionMode.store(false);
    }
    timelineSample.store(0);
    sendChangeMessage();
}

void AudioEngine::syncProject(const ProjectData& project)
{
    const juce::ScopedWriteLock guard(renderLock);
    rebuildLoadedClips(project);
    auto contentDuration = 0.0;
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
            if (!clip.muted)
                contentDuration = std::max(contentDuration, clip.startSeconds + clip.durationSeconds);
    projectDurationSeconds.store(contentDuration);
}

void AudioEngine::setHifiganModelDirectory(const juce::File& directory)
{
    const juce::ScopedWriteLock guard(renderLock);
    if (hifiganModelDirectory == directory) return;
    hifiganModelDirectory = directory;
    renderCache.clear();
}

void AudioEngine::setInferenceConfiguration(backend::InferenceBackend inference, int deviceIndex)
{
    const backend::OrtExecutionConfig next {
        inference, deviceIndex, std::max(1, juce::SystemStats::getNumCpus() - 1)
    };
    const juce::ScopedWriteLock guard(renderLock);
    if (inferenceConfiguration.requested == next.requested
        && inferenceConfiguration.deviceIndex == next.deviceIndex
        && inferenceConfiguration.intraOpThreads == next.intraOpThreads)
        return;
    inferenceConfiguration = next;
    renderCache.clear();
}

void AudioEngine::rebuildLoadedClips(const ProjectData& project)
{
    loadedClips.clear();
    trackMeters.clear();
    std::unordered_map<std::string, std::shared_ptr<juce::AudioFormatReader>> readers;
    std::unordered_set<std::string> activeRenderKeys;
    const auto anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                     [](const auto& track) { return track.solo; });
    // A connection is the smallest shared unit the two neural pathways agree
    // on: the next element starts at most 20 ms after the previous one ends and
    // no earlier than 80 ms before it (a CVVC connector sits under the vowel
    // tail), and its source region continues the previous element's source
    // range so independent overlapping layers are never fused into a phrase.
    const auto clipsConnected = [](const ClipData& left, const ClipData& right) -> bool
    {
        if (left.sourceFile.getFullPathName() != right.sourceFile.getFullPathName()) return false;
        const auto gap = right.startSeconds - (left.startSeconds + left.durationSeconds);
        if (gap > 0.02) return false;
        if (gap < -0.08) return false;
        if (right.sourceOffsetSeconds < left.sourceOffsetSeconds - 1.0e-6) return false;
        if (right.sourceOffsetSeconds > left.sourceOffsetSeconds + left.sourceDurationSeconds + 0.03)
            return false;
        return true;
    };
    const auto sliceInto = [](const RenderedClip& source, const RenderedClip::SliceTarget& target)
    {
        if (target.clip == nullptr || source.buffer.getNumSamples() <= 0) return;
        const auto channels = source.buffer.getNumChannels();
        const auto copied = std::min(target.sampleCount,
            std::max(0, source.buffer.getNumSamples() - target.startSample));
        target.clip->buffer.setSize(channels, target.sampleCount);
        target.clip->buffer.clear();
        for (int channel = 0; channel < channels; ++channel)
            target.clip->buffer.copyFrom(channel, 0, source.buffer, channel,
                target.startSample, copied);
        target.clip->sampleRate = source.sampleRate;
        target.clip->backend = source.backend;
        target.clip->ready.store(true, std::memory_order_release);
        target.clip->finished.store(true, std::memory_order_release);
    };
    for (const auto& track : project.tracks)
    {
        auto meter = std::make_shared<std::atomic<float>>(0.0f);
        trackMeters[track.id.toStdString()] = meter;
        if (track.muted || (anySolo && !track.solo)) continue;
        std::vector<const ClipData*> orderedClips;
        orderedClips.reserve(track.clips.size());
        for (const auto& clip : track.clips) orderedClips.push_back(&clip);
        std::stable_sort(orderedClips.begin(), orderedClips.end(), [](const auto* left, const auto* right)
        {
            return left->startSeconds < right->startSeconds;
        });
        const auto count = orderedClips.size();
        std::vector<bool> connectedPrev(count, false);
        std::vector<bool> connectedNext(count, false);
        for (std::size_t index = 1; index < count; ++index)
            if (clipsConnected(*orderedClips[index - 1], *orderedClips[index]))
            {
                connectedPrev[index] = true;
                connectedNext[index - 1] = true;
            }
        const auto mergedMode = track.compose
            && track.pitchAlgorithm == PitchAlgorithm::nsfHifigan
            && track.renderOrder == RenderOrder::stretchSpliceThenPitch;
        struct PendingGroup
        {
            std::vector<const ClipData*> clips;
            std::vector<LoadedClip*> loaded;
        };
        std::vector<PendingGroup> pendingGroups;
        for (std::size_t clipIndex = 0; clipIndex < count; ++clipIndex)
        {
            const auto& clip = *orderedClips[clipIndex];
            if (clip.muted || !clip.sourceFile.existsAsFile()) continue;
            const auto sourceKey = clip.sourceFile.getFullPathName().toStdString();
            auto reader = readers[sourceKey];
            if (reader == nullptr)
            {
                reader.reset(formatManager.createReaderFor(clip.sourceFile));
                if (reader == nullptr) continue;
                readers[sourceKey] = reader;
            }
            const auto exclusiveNeuralPath = track.compose && !clip.notes.empty()
                && track.pitchAlgorithm == PitchAlgorithm::nsfHifigan
                && (track.stretchAlgorithm == StretchAlgorithm::variableMelHop
                    || track.stretchAlgorithm == StretchAlgorithm::nsfShiftThenSplice);
            const auto inMerged = mergedMode
                && (connectedPrev[clipIndex] || connectedNext[clipIndex]);
            const auto continuousNeuralSeam = inMerged && exclusiveNeuralPath;
            const auto previousGap = clipIndex > 0
                ? clip.startSeconds - (orderedClips[clipIndex - 1]->startSeconds
                    + orderedClips[clipIndex - 1]->durationSeconds)
                : std::numeric_limits<double>::infinity();
            const auto nextGap = clipIndex + 1 < count
                ? orderedClips[clipIndex + 1]->startSeconds
                    - (clip.startSeconds + clip.durationSeconds)
                : std::numeric_limits<double>::infinity();
            const auto joinedNeuralStart = exclusiveNeuralPath && connectedPrev[clipIndex]
                && std::abs(previousGap) <= 0.002;
            const auto joinedNeuralEnd = exclusiveNeuralPath && connectedNext[clipIndex]
                && std::abs(nextGap) <= 0.002;
            auto loaded = std::make_unique<LoadedClip>();
            loaded->clip = clip;
            if (joinedNeuralStart) loaded->clip.crossfadeInSeconds = 0.0;
            if (joinedNeuralEnd) loaded->clip.crossfadeOutSeconds = 0.0;
            loaded->trackId = track.id.toStdString();
            loaded->smoothOverlaps = track.smoothOverlaps;
            const auto compactDeclick = std::min(0.0025, loaded->clip.durationSeconds * 0.5);
            if (!(joinedNeuralStart || (continuousNeuralSeam && connectedPrev[clipIndex])))
                loaded->clip.fadeInSeconds = std::max(loaded->clip.fadeInSeconds, compactDeclick);
            if (!(joinedNeuralEnd || (continuousNeuralSeam && connectedNext[clipIndex])))
                loaded->clip.fadeOutSeconds = std::max(loaded->clip.fadeOutSeconds, compactDeclick);
            if (track.smoothOverlaps && clipIndex > 0)
            {
                const auto& previous = *orderedClips[clipIndex - 1];
                const auto overlap = previous.startSeconds + previous.durationSeconds - clip.startSeconds;
                if (overlap > 1.0e-6)
                    loaded->clip.fadeInSeconds = std::max(loaded->clip.fadeInSeconds,
                        std::min({ overlap, 0.1, loaded->clip.durationSeconds }));
                else if (std::abs(overlap) <= 0.002
                         && connectedPrev[clipIndex]
                         && !exclusiveNeuralPath
                         && clip.crossfadeInSeconds <= 1.0e-6)
                    loaded->clip.fadeInSeconds = std::max(loaded->clip.fadeInSeconds,
                        std::min(0.006, loaded->clip.durationSeconds * 0.5));
            }
            if (track.smoothOverlaps && clipIndex + 1 < count)
            {
                const auto& next = *orderedClips[clipIndex + 1];
                const auto overlap = clip.startSeconds + clip.durationSeconds - next.startSeconds;
                if (overlap > 1.0e-6)
                    loaded->clip.fadeOutSeconds = std::max(loaded->clip.fadeOutSeconds,
                        std::min({ overlap, 0.1, loaded->clip.durationSeconds }));
                else if (std::abs(overlap) <= 0.002
                         && connectedNext[clipIndex]
                         && !exclusiveNeuralPath
                         && clip.crossfadeOutSeconds <= 1.0e-6)
                    loaded->clip.fadeOutSeconds = std::max(loaded->clip.fadeOutSeconds,
                        std::min(0.006, loaded->clip.durationSeconds * 0.5));
            }
            loaded->trackGain = track.volume;
            loaded->trackPan = juce::jlimit(-1.0f, 1.0f, track.pan);
            loaded->meter = meter;
            loaded->reader = reader;
            // Every compose path must use a duration-preserving, formant-preserving render.
            // Until a selected external engine is present, the native mld5 renderer is the
            // deterministic model-free fallback rather than device-rate resampling, which
            // shifts both F0 and formants and creates the "old/child voice" failure mode.
            if (track.compose && !clip.notes.empty())
            {
                if (inMerged)
                {
                    loaded->rendered = std::make_shared<RenderedClip>();
                    if (connectedPrev[clipIndex] && !pendingGroups.empty()
                        && pendingGroups.back().clips.back() == orderedClips[clipIndex - 1])
                    {
                        pendingGroups.back().clips.push_back(&clip);
                        pendingGroups.back().loaded.push_back(loaded.get());
                    }
                    else
                    {
                        PendingGroup group;
                        group.clips.push_back(&clip);
                        group.loaded.push_back(loaded.get());
                        pendingGroups.push_back(std::move(group));
                    }
                }
                else
                {
                    const auto connectedPrevFlag = connectedPrev[clipIndex];
                    const auto connectedNextFlag = connectedNext[clipIndex];
                    const auto cacheKey = renderKey(clip, track,
                        hifiganModelDirectory, inferenceConfiguration,
                        connectedPrevFlag, connectedNextFlag,
                        connectedPrevFlag ? orderedClips[clipIndex - 1] : nullptr,
                        connectedNextFlag ? orderedClips[clipIndex + 1] : nullptr,
                        track.renderOrder);
                    activeRenderKeys.insert(cacheKey);
                    auto& state = renderCache[cacheKey];
                    if (state == nullptr) state = std::make_shared<RenderedClip>();
                    loaded->rendered = state;
                    if (!state->scheduled.exchange(true))
                    {
                        auto request = makeRenderRequest(clip, track,
                            hifiganModelDirectory, inferenceConfiguration,
                            connectedPrevFlag, connectedNextFlag);
                        if (track.pitchAlgorithm == PitchAlgorithm::nsfHifigan)
                        {
                            // A connected seam is covered by the mixer crossfade,
                            // so shrink the baked-in neural guard there to a bare
                            // de-click instead of a 3 ms level dip.
                            if (connectedPrevFlag) request.neuralGuardStartSeconds = 0.0005f;
                            if (connectedNextFlag) request.neuralGuardEndSeconds = 0.0005f;
                        }
                        renderService.renderMld5File(std::move(request), [state](backend::RenderedAudio result) mutable
                        {
                            if (result.buffer.getNumSamples() <= 0 || result.sampleRate <= 0.0)
                            {
                                state->finished.store(true, std::memory_order_release);
                                return;
                            }
                            state->buffer = std::move(result.buffer);
                            state->sampleRate = result.sampleRate;
                            state->backend = std::move(result.backend);
                            state->ready.store(true, std::memory_order_release);
                            state->finished.store(true, std::memory_order_release);
                        });
                    }
                }
            }
            // Playback only needs clip timing/gain after the render request is
            // created.  Drop duplicated contours here so large MPD projects do
            // not keep a second full copy of every analysis point per clip.
            loaded->clip.notes.clear();
            loaded->clip.notes.shrink_to_fit();
            loadedClips.push_back(std::move(loaded));
        }

        // Schedule the merged phrase renders for this track.  Each connected
        // group becomes one stretch-splice-then-pitch request whose output is
        // cut back into the per-element buffers the mixer expects.
        for (auto& group : pendingGroups)
        {
            if (group.clips.size() < 2) continue;
            const auto mergedKey = mergedRenderKey(group.clips, track,
                hifiganModelDirectory, inferenceConfiguration);
            activeRenderKeys.insert(mergedKey);
            auto& mergedEntry = renderCache[mergedKey];
            if (mergedEntry == nullptr) mergedEntry = std::make_shared<RenderedClip>();
            const auto sourceKey = group.clips.front()->sourceFile.getFullPathName().toStdString();
            const auto readerIt = readers.find(sourceKey);
            const auto fileRate = readerIt != readers.end() && readerIt->second != nullptr
                ? readerIt->second->sampleRate : outputSampleRate.load();
            std::vector<RenderedClip::SliceTarget> targets;
            targets.reserve(group.clips.size());
            double targetOffset = 0.0;
            for (std::size_t index = 0; index < group.clips.size(); ++index)
            {
                const auto start = static_cast<int>(std::llround(targetOffset * fileRate));
                targetOffset += group.clips[index]->durationSeconds;
                const auto end = static_cast<int>(std::llround(targetOffset * fileRate));
                targets.push_back({ group.loaded[index]->rendered, start, std::max(1, end - start) });
            }
            {
                const juce::ScopedLock sliceGuard(mergedEntry->sliceLock);
                if (mergedEntry->ready.load(std::memory_order_acquire))
                {
                    for (const auto& target : targets)
                        sliceInto(*mergedEntry, target);
                }
                else
                {
                    for (auto& target : targets)
                        mergedEntry->pendingSlices.push_back(std::move(target));
                }
            }
            if (!mergedEntry->scheduled.exchange(true))
            {
                auto request = makeMergedRenderRequest(group.clips, track,
                    hifiganModelDirectory, inferenceConfiguration);
                renderService.renderMld5File(std::move(request),
                    [mergedEntry, sliceInto](backend::RenderedAudio result) mutable
                    {
                        if (result.buffer.getNumSamples() <= 0 || result.sampleRate <= 0.0)
                        {
                            mergedEntry->finished.store(true, std::memory_order_release);
                            return;
                        }
                        mergedEntry->buffer = result.buffer;
                        mergedEntry->sampleRate = result.sampleRate;
                        mergedEntry->backend = std::move(result.backend);
                        {
                            const juce::ScopedLock sliceGuard(mergedEntry->sliceLock);
                            for (const auto& target : mergedEntry->pendingSlices)
                                sliceInto(*mergedEntry, target);
                            mergedEntry->pendingSlices.clear();
                            mergedEntry->ready.store(true, std::memory_order_release);
                            mergedEntry->finished.store(true, std::memory_order_release);
                        }
                    });
            }
        }
    }
    std::erase_if(renderCache, [&](const auto& item)
    {
        return !activeRenderKeys.contains(item.first);
    });
}

float AudioEngine::fadeEnvelope(const ClipData& clip, double localSeconds)
{
    auto gain = 1.0f;
    // Melodyne successive-join amplitude transitions are LINEAR complementary
    // fades (element amplitudeFadeIn/OutShapePow are always 1.0).  Each joined
    // element is back-to-back with its partner, so the fade-in of the following
    // element and the fade-out of the preceding element meet at the boundary
    // and together span the full MUSuccessiveJoin.amplitudeTransitionDuration.
    if (clip.crossfadeInSeconds > 1.0e-6)
    {
        const auto phase = static_cast<float>(juce::jlimit(0.0, 1.0,
            localSeconds / clip.crossfadeInSeconds));
        gain *= phase;
    }
    if (clip.crossfadeOutSeconds > 1.0e-6)
    {
        const auto phase = static_cast<float>(juce::jlimit(0.0, 1.0,
            (clip.durationSeconds - localSeconds) / clip.crossfadeOutSeconds));
        gain *= phase;
    }
    if (clip.fadeInSeconds > 1.0e-6)
    {
        const auto phase = static_cast<float>(juce::jlimit(0.0, 1.0,
            localSeconds / clip.fadeInSeconds));
        gain *= phase * phase * (3.0f - 2.0f * phase);
    }
    if (clip.fadeOutSeconds > 1.0e-6)
    {
        const auto phase = static_cast<float>(juce::jlimit(0.0, 1.0,
            (clip.durationSeconds - localSeconds) / clip.fadeOutSeconds));
        gain *= phase * phase * (3.0f - 2.0f * phase);
    }
    return gain;
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    if (!playing.load() || info.buffer == nullptr || info.numSamples <= 0) return;

    const auto sampleRate = outputSampleRate.load();
    const auto blockStartSample = timelineSample.load();
    const auto blockStart = static_cast<double>(blockStartSample) / sampleRate;
    const auto blockEnd = static_cast<double>(blockStartSample + info.numSamples) / sampleRate;
    const juce::ScopedReadLock guard(renderLock);

    for (const auto& [_, meter] : trackMeters)
        meter->store(meter->load(std::memory_order_relaxed) * 0.88f, std::memory_order_relaxed);

    if (auditionMode.load() && auditionReader != nullptr)
    {
        const auto readerRate = auditionReader->sampleRate;
        const auto firstSourcePosition = blockStart * readerRate;
        if (firstSourcePosition >= static_cast<double>(auditionReader->lengthInSamples))
        {
            playing.store(false);
            return;
        }
        const auto availableSeconds = (static_cast<double>(auditionReader->lengthInSamples)
                                       - firstSourcePosition) / readerRate;
        const auto outputCount = juce::jlimit(0, info.numSamples,
            static_cast<int>(std::ceil(availableSeconds * sampleRate)));
        const auto lastSourcePosition = firstSourcePosition
            + static_cast<double>(std::max(0, outputCount - 1)) * readerRate / sampleRate;
        const auto sourceBase = static_cast<juce::int64>(std::floor(firstSourcePosition));
        const auto sourceCount = std::max(2, static_cast<int>(std::ceil(lastSourcePosition))
                                            - static_cast<int>(sourceBase) + 2);
        const auto sourceChannels = juce::jlimit(1, 2, static_cast<int>(auditionReader->numChannels));
        auditionScratch.setSize(sourceChannels, sourceCount, false, false, true);
        auditionScratch.clear();
        auditionReader->read(&auditionScratch, 0, sourceCount, sourceBase, true, sourceChannels > 1);
        for (int outputOffset = 0; outputOffset < outputCount; ++outputOffset)
        {
            const auto sourcePosition = firstSourcePosition
                + static_cast<double>(outputOffset) * readerRate / sampleRate - static_cast<double>(sourceBase);
            const auto leftIndex = juce::jlimit(0, sourceCount - 1, static_cast<int>(std::floor(sourcePosition)));
            const auto rightIndex = juce::jmin(sourceCount - 1, leftIndex + 1);
            const auto fraction = static_cast<float>(sourcePosition - std::floor(sourcePosition));
            const auto interpolate = [&, leftIndex, rightIndex, fraction](int channel)
            {
                const auto* samples = auditionScratch.getReadPointer(channel);
                return samples[leftIndex] + (samples[rightIndex] - samples[leftIndex]) * fraction;
            };
            const auto left = interpolate(0);
            const auto right = sourceChannels > 1 ? interpolate(1) : left;
            info.buffer->setSample(0, info.startSample + outputOffset, left);
            if (info.buffer->getNumChannels() > 1)
                info.buffer->setSample(1, info.startSample + outputOffset, right);
        }
        timelineSample.fetch_add(outputCount);
        if (outputCount < info.numSamples) playing.store(false);
        if (!offlineRendering.load(std::memory_order_relaxed)) sendChangeMessage();
        return;
    }

    std::unordered_map<std::string, std::vector<float>> overlapEnvelopeSums;
    std::unordered_map<std::string, std::vector<unsigned short>> overlapCounts;
    for (const auto& loaded : loadedClips)
    {
        if (!loaded->smoothOverlaps) continue;
        const auto& clip = loaded->clip;
        const auto clipEnd = clip.startSeconds + clip.durationSeconds;
        const auto overlapStart = std::max(blockStart, clip.startSeconds);
        const auto overlapEnd = std::min(blockEnd, clipEnd);
        if (overlapEnd <= overlapStart) continue;
        auto& sums = overlapEnvelopeSums[loaded->trackId];
        auto& counts = overlapCounts[loaded->trackId];
        if (sums.empty()) sums.assign(static_cast<std::size_t>(info.numSamples), 0.0f);
        if (counts.empty()) counts.assign(static_cast<std::size_t>(info.numSamples), 0);
        const auto begin = juce::jlimit(0, info.numSamples,
            static_cast<int>(std::floor((overlapStart - blockStart) * sampleRate)));
        const auto end = juce::jlimit(begin, info.numSamples,
            static_cast<int>(std::ceil((overlapEnd - blockStart) * sampleRate)));
        for (auto output = begin; output < end; ++output)
        {
            const auto absoluteSeconds = blockStart + static_cast<double>(output) / sampleRate;
            sums[static_cast<std::size_t>(output)] += fadeEnvelope(
                clip, absoluteSeconds - clip.startSeconds);
            ++counts[static_cast<std::size_t>(output)];
        }
    }

    const auto smoothedGain = [&](const LoadedClip& loaded, double localSeconds,
                                  int blockOffset)
    {
        auto envelope = fadeEnvelope(loaded.clip, localSeconds);
        if (loaded.smoothOverlaps)
        {
            const auto sums = overlapEnvelopeSums.find(loaded.trackId);
            const auto counts = overlapCounts.find(loaded.trackId);
            if (sums != overlapEnvelopeSums.end() && counts != overlapCounts.end()
                && counts->second[static_cast<std::size_t>(blockOffset)] > 1)
            {
                const auto total = sums->second[static_cast<std::size_t>(blockOffset)];
                if (total > 1.0e-6f) envelope /= std::max(0.5f, total);
            }
        }
        return loaded.clip.gain * envelope;
    };

    for (auto& loaded : loadedClips)
    {
        const auto& clip = loaded->clip;
        const auto clipEnd = clip.startSeconds + clip.durationSeconds;
        const auto overlapStart = std::max(blockStart, clip.startSeconds);
        const auto overlapEnd = std::min(blockEnd, clipEnd);
        if (overlapEnd <= overlapStart) continue;

        const auto outputBegin = juce::jlimit(0, info.numSamples,
            static_cast<int>(std::floor((overlapStart - blockStart) * sampleRate)));
        const auto outputEnd = juce::jlimit(outputBegin, info.numSamples,
            static_cast<int>(std::ceil((overlapEnd - blockStart) * sampleRate)));
        const auto outputCount = outputEnd - outputBegin;
        if (outputCount <= 0) continue;

        if (loaded->rendered != nullptr
            && loaded->rendered->ready.load(std::memory_order_acquire)
            && loaded->rendered->buffer.getNumSamples() > 0)
        {
            const auto& rendered = loaded->rendered->buffer;
            const auto renderedRate = loaded->rendered->sampleRate;
            const auto renderedChannels = juce::jlimit(1, 2, rendered.getNumChannels());
            const auto firstPosition = (blockStart + static_cast<double>(outputBegin) / sampleRate
                                        - clip.startSeconds) * renderedRate;
            const auto [leftPan, rightPan] = panGains(loaded->trackPan, renderedChannels == 1);
            for (int outputOffset = 0; outputOffset < outputCount; ++outputOffset)
            {
                const auto position = firstPosition
                    + static_cast<double>(outputOffset) * renderedRate / sampleRate;
                const auto leftIndex = juce::jlimit(0, rendered.getNumSamples() - 1,
                                                     static_cast<int>(std::floor(position)));
                const auto rightIndex = std::min(rendered.getNumSamples() - 1, leftIndex + 1);
                const auto fraction = static_cast<float>(position - std::floor(position));
                const auto interpolate = [&](int channel)
                {
                    const auto* samples = rendered.getReadPointer(channel);
                    return samples[leftIndex] + (samples[rightIndex] - samples[leftIndex]) * fraction;
                };
                const auto sourceLeft = interpolate(0);
                const auto sourceRight = renderedChannels > 1 ? interpolate(1) : sourceLeft;
                const auto absoluteSeconds = blockStart
                    + static_cast<double>(outputBegin + outputOffset) / sampleRate;
                const auto gain = smoothedGain(*loaded,
                    absoluteSeconds - clip.startSeconds, outputBegin + outputOffset)
                    * loaded->trackGain;
                const auto destination = info.startSample + outputBegin + outputOffset;
                const auto renderedLeft = sourceLeft * gain * leftPan;
                const auto renderedRight = sourceRight * gain * rightPan;
                info.buffer->addSample(0, destination, renderedLeft);
                if (info.buffer->getNumChannels() > 1)
                    info.buffer->addSample(1, destination, renderedRight);
                if (loaded->meter != nullptr)
                {
                    const auto peak = std::max(std::abs(renderedLeft), std::abs(renderedRight));
                    loaded->meter->store(std::max(loaded->meter->load(std::memory_order_relaxed), peak),
                                         std::memory_order_relaxed);
                }
            }
            continue;
        }

        const auto readerRate = loaded->reader->sampleRate;
        const auto sourceDuration = clip.sourceDurationSeconds > 1.0e-9
            ? clip.sourceDurationSeconds : clip.durationSeconds;
        const auto playbackRate = sourceDuration / std::max(0.001, clip.durationSeconds);
        const auto firstSourcePosition = clip.sourceOffsetSeconds * readerRate
            + (blockStart + static_cast<double>(outputBegin) / sampleRate - clip.startSeconds)
                * playbackRate * readerRate;
        const auto lastSourcePosition = firstSourcePosition
            + static_cast<double>(outputCount - 1) * playbackRate * readerRate / sampleRate;
        const auto sourceBase = static_cast<juce::int64>(std::floor(firstSourcePosition));
        const auto sourceCount = static_cast<int>(std::ceil(lastSourcePosition))
            - static_cast<int>(sourceBase) + 2;
        if (sourceBase < 0 || sourceCount <= 1) continue;

        const auto sourceChannels = juce::jlimit(1, 2, static_cast<int>(loaded->reader->numChannels));
        loaded->scratch.setSize(sourceChannels, sourceCount, false, false, true);
        loaded->scratch.clear();
        loaded->reader->read(&loaded->scratch, 0, sourceCount, sourceBase, true, sourceChannels > 1);

        const auto [leftPan, rightPan] = panGains(loaded->trackPan, sourceChannels == 1);
        for (int outputOffset = 0; outputOffset < outputCount; ++outputOffset)
        {
            const auto sourcePosition = firstSourcePosition
                + static_cast<double>(outputOffset) * playbackRate * readerRate / sampleRate
                - static_cast<double>(sourceBase);
            const auto leftIndex = juce::jlimit(0, sourceCount - 1, static_cast<int>(std::floor(sourcePosition)));
            const auto rightIndex = juce::jmin(sourceCount - 1, leftIndex + 1);
            const auto fraction = static_cast<float>(sourcePosition - std::floor(sourcePosition));
            const auto interpolate = [&, leftIndex, rightIndex, fraction](int channel)
            {
                const auto* samples = loaded->scratch.getReadPointer(channel);
                return samples[leftIndex] + (samples[rightIndex] - samples[leftIndex]) * fraction;
            };
            const auto sourceLeft = interpolate(0);
            const auto sourceRight = sourceChannels > 1 ? interpolate(1) : sourceLeft;
            const auto absoluteSeconds = blockStart + static_cast<double>(outputBegin + outputOffset) / sampleRate;
            const auto gain = smoothedGain(*loaded,
                absoluteSeconds - clip.startSeconds, outputBegin + outputOffset)
                * loaded->trackGain;
            const auto destination = info.startSample + outputBegin + outputOffset;
            const auto renderedLeft = sourceLeft * gain * leftPan;
            const auto renderedRight = sourceRight * gain * rightPan;
            info.buffer->addSample(0, destination, renderedLeft);
            if (info.buffer->getNumChannels() > 1)
                info.buffer->addSample(1, destination, renderedRight);
            if (loaded->meter != nullptr)
            {
                const auto peak = std::max(std::abs(renderedLeft), std::abs(renderedRight));
                loaded->meter->store(std::max(loaded->meter->load(std::memory_order_relaxed), peak),
                                     std::memory_order_relaxed);
            }
        }
    }

    // A Melodyne project may contain several overlapping elements whose
    // individual gains are valid but whose sum exceeds full scale.  Apply one
    // linked sample envelope (fast attack, slow release) after mixing so block
    // boundaries cannot become gain steps or digital crack/burst artefacts.
    auto limiterGain = masterLimiterGain.load(std::memory_order_relaxed);
    const auto attackMemory = std::exp(-1.0f / static_cast<float>(
        std::max(1.0, sampleRate) * 0.0005));
    const auto releaseMemory = std::exp(-1.0f / static_cast<float>(
        std::max(1.0, sampleRate) * 0.18));
    for (int index = 0; index < info.numSamples; ++index)
    {
        auto linkedPeak = 0.0f;
        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            linkedPeak = std::max(linkedPeak, std::abs(info.buffer->getSample(
                channel, info.startSample + index)));
        const auto target = linkedPeak > 0.98f ? 0.98f / linkedPeak : 1.0f;
        const auto memory = target < limiterGain ? attackMemory : releaseMemory;
        limiterGain = target + (limiterGain - target) * memory;
        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
        {
            auto value = info.buffer->getSample(channel, info.startSample + index) * limiterGain;
            const auto magnitude = std::abs(value);
            if (magnitude > 0.98f)
                value = std::copysign(0.98f + 0.02f
                    * std::tanh((magnitude - 0.98f) / 0.02f), value);
            info.buffer->setSample(channel, info.startSample + index, value);
        }
    }
    masterLimiterGain.store(limiterGain, std::memory_order_relaxed);

    const auto nextSample = timelineSample.fetch_add(info.numSamples) + info.numSamples;
    if (static_cast<double>(nextSample) / sampleRate >= projectDurationSeconds.load())
        playing.store(false);
    if (!offlineRendering.load(std::memory_order_relaxed)) sendChangeMessage();
}

void AudioEngine::play()
{
    auto duration = projectDurationSeconds.load();
    {
        const juce::ScopedReadLock guard(renderLock);
        if (auditionMode.load() && auditionReader != nullptr)
            duration = static_cast<double>(auditionReader->lengthInSamples) / auditionReader->sampleRate;
    }
    if (duration > 0.0 && position() >= duration)
        setPosition(0.0);
    playing.store(true);
    sendChangeMessage();
}

void AudioEngine::stop()
{
    playing.store(false);
    masterLimiterGain.store(1.0f, std::memory_order_relaxed);
    {
        const juce::ScopedReadLock guard(renderLock);
        for (const auto& [_, meter] : trackMeters)
            meter->store(0.0f, std::memory_order_relaxed);
    }
    sendChangeMessage();
}

void AudioEngine::setPosition(double seconds)
{
    timelineSample.store(static_cast<juce::int64>(std::max(0.0, seconds) * outputSampleRate.load()));
    sendChangeMessage();
}

double AudioEngine::position() const
{
    return static_cast<double>(timelineSample.load()) / outputSampleRate.load();
}

float AudioEngine::trackPeak(const juce::String& trackId) const
{
    const juce::ScopedReadLock guard(renderLock);
    if (const auto found = trackMeters.find(trackId.toStdString()); found != trackMeters.end())
        return found->second->load(std::memory_order_relaxed);
    return 0.0f;
}

std::optional<double> AudioEngine::renderProgress() const
{
    const juce::ScopedReadLock guard(renderLock);
    int total = 0;
    int finished = 0;
    for (const auto& loaded : loadedClips)
        if (loaded->rendered != nullptr)
        {
            ++total;
            if (loaded->rendered->finished.load(std::memory_order_acquire)) ++finished;
        }
    if (total == 0 || finished >= total) return std::nullopt;
    return static_cast<double>(finished) / static_cast<double>(total);
}

juce::String AudioEngine::activeRenderBackends() const
{
    const juce::ScopedReadLock guard(renderLock);
    juce::StringArray names;
    for (const auto& loaded : loadedClips)
        if (loaded->rendered != nullptr
            && loaded->rendered->ready.load(std::memory_order_acquire)
            && loaded->rendered->backend.isNotEmpty())
            names.addIfNotAlreadyThere(loaded->rendered->backend);
    names.sort(true);
    return names.joinIntoString(" + ");
}

bool AudioEngine::exportWav(const juce::File& file, juce::String& error)
{
    {
        const juce::ScopedReadLock guard(renderLock);
        for (const auto& loaded : loadedClips)
            if (loaded->rendered != nullptr
                && !loaded->rendered->finished.load(std::memory_order_acquire))
            {
                error = "Pre-render is still running";
                return false;
            }
    }

    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
    {
        error = "Could not create " + file.getFullPathName();
        return false;
    }
    const auto sampleRate = juce::jlimit(8'000.0, 192'000.0, outputSampleRate.load());
    juce::WavAudioFormat format;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(format.createWriterFor(
        stream.release(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        error = "Could not create WAV writer";
        return false;
    }

    stop();
    deviceManager.removeAudioCallback(&sourcePlayer);
    const auto previousPosition = timelineSample.load();
    const auto previousAudition = auditionMode.exchange(false);
    offlineRendering.store(true, std::memory_order_release);
    timelineSample.store(0);
    masterLimiterGain.store(1.0f, std::memory_order_relaxed);
    playing.store(true);

    constexpr int blockSize = 2048;
    juce::AudioBuffer<float> block(2, blockSize);
    const auto totalSamples = static_cast<juce::int64>(std::ceil(
        projectDurationSeconds.load() * sampleRate));
    auto written = juce::int64(0);
    auto ok = true;
    while (written < totalSamples)
    {
        const auto count = static_cast<int>(std::min<juce::int64>(blockSize, totalSamples - written));
        block.clear();
        juce::AudioSourceChannelInfo info(&block, 0, count);
        getNextAudioBlock(info);
        if (!writer->writeFromAudioSampleBuffer(block, 0, count))
        {
            ok = false;
            error = "WAV write failed";
            break;
        }
        written += count;
    }

    writer.reset();
    playing.store(false);
    timelineSample.store(previousPosition);
    auditionMode.store(previousAudition);
    offlineRendering.store(false, std::memory_order_release);
    deviceManager.addAudioCallback(&sourcePlayer);
    sendChangeMessage();
    return ok;
}
}
