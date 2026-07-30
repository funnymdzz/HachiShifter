#include "AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

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
    return std::pair { left.relativeCents + (next.relativeCents - left.relativeCents) * amount,
                       left.withoutVibratoCents
                           + (next.withoutVibratoCents - left.withoutVibratoCents) * amount };
}

backend::Mld5FileRenderRequest makeRenderRequest(const ClipData& clip, const TrackData& track)
{
    backend::Mld5FileRenderRequest request;
    request.sourceFile = clip.sourceFile;
    request.sourceOffsetSeconds = clip.sourceOffsetSeconds;
    request.sourceDurationSeconds = clip.sourceDurationSeconds > 1.0e-9
        ? clip.sourceDurationSeconds : clip.durationSeconds;
    request.targetDurationSeconds = clip.durationSeconds;
    request.pitchAlgorithm = static_cast<int>(track.pitchAlgorithm);
    request.stretchAlgorithm = static_cast<int>(track.stretchAlgorithm);
    request.timeMap.push_back({ 0.0, 0.0 });
    const auto sourceDuration = request.sourceDurationSeconds;
    for (const auto& note : clip.notes)
    {
        if (note.consonantSeconds <= 1.0e-6 || note.attackSpeed <= 1.0e-6f) continue;
        const auto noteTargetStart = juce::jlimit(0.0, clip.durationSeconds, note.startSeconds);
        const auto targetAttack = juce::jlimit(noteTargetStart, clip.durationSeconds,
            noteTargetStart + note.consonantSeconds);
        const auto sourceStart = clip.durationSeconds > 1.0e-9
            ? noteTargetStart / clip.durationSeconds * sourceDuration : 0.0;
        const auto sourceAttack = juce::jlimit(sourceStart, sourceDuration,
            sourceStart + note.consonantSeconds * static_cast<double>(note.attackSpeed));
        if (targetAttack > request.timeMap.back().targetSeconds + 1.0e-7
            && sourceAttack > request.timeMap.back().sourceSeconds + 1.0e-7
            && targetAttack < clip.durationSeconds - 1.0e-7
            && sourceAttack < sourceDuration - 1.0e-7)
            request.timeMap.push_back({ targetAttack, sourceAttack });
    }
    request.timeMap.push_back({ clip.durationSeconds, sourceDuration });
    constexpr auto framePeriodSeconds = 0.005;
    request.framePeriodMs = framePeriodSeconds * 1000.0;
    const auto frameCount = std::max(2, static_cast<int>(std::ceil(clip.durationSeconds
                                                                   / framePeriodSeconds)) + 1);
    request.sourceMidi.resize(static_cast<std::size_t>(frameCount), 0.0f);
    request.targetMidi.resize(static_cast<std::size_t>(frameCount), 0.0f);
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto time = std::min(clip.durationSeconds, static_cast<double>(frame) * framePeriodSeconds);
        for (const auto& note : clip.notes)
        {
            const auto local = time - note.startSeconds;
            if (local < -1.0e-9 || local > note.durationSeconds + 1.0e-9) continue;
            const auto cents = contourAt(note, juce::jlimit(0.0, note.durationSeconds, local));
            if (!cents) break;
            const auto sourceCenter = note.sourceMidiCenter >= 0.0f ? note.sourceMidiCenter : note.midiNote;
            request.sourceMidi[static_cast<std::size_t>(frame)] = sourceCenter + cents->first / 100.0f;
            request.targetMidi[static_cast<std::size_t>(frame)] = note.midiNote
                + note.drift * cents->second / 100.0f
                + note.modulation * (cents->first - cents->second) / 100.0f;
            break;
        }
    }

    for (const auto& joinedNote : clip.notes)
    {
        if (!joinedNote.connectedToPrevious) continue;
        const NoteData* previousNote = nullptr;
        auto previousEnd = -std::numeric_limits<double>::infinity();
        const auto joinedStart = clip.startSeconds + joinedNote.startSeconds;
        for (const auto& candidateClip : track.clips)
            for (const auto& candidate : candidateClip.notes)
            {
                const auto end = candidateClip.startSeconds + candidate.startSeconds
                    + candidate.durationSeconds;
                if (end <= joinedStart + 0.002
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
                ? previousNote->drift * previousCents->second / 100.0f
                    + previousNote->modulation * (previousCents->first - previousCents->second) / 100.0f
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

std::string renderKey(const ClipData& clip, const TrackData& track)
{
    juce::MemoryOutputStream stream;
    const auto path = clip.sourceFile.getFullPathName().toUTF8();
    stream.write(path.getAddress(), path.sizeInBytes());
    stream.writeInt64(clip.sourceFile.getLastModificationTime().toMilliseconds());
    stream.writeDouble(clip.sourceOffsetSeconds);
    stream.writeDouble(clip.sourceDurationSeconds);
    stream.writeDouble(clip.durationSeconds);
    stream.writeInt(static_cast<int>(track.pitchAlgorithm));
    stream.writeInt(static_cast<int>(track.stretchAlgorithm));
    for (const auto& note : clip.notes)
    {
        stream.writeDouble(note.startSeconds);
        stream.writeDouble(note.durationSeconds);
        stream.writeFloat(note.midiNote);
        stream.writeFloat(note.sourceMidiCenter);
        stream.writeDouble(note.consonantSeconds);
        stream.writeFloat(note.attackSpeed);
        stream.writeByte(static_cast<char>(note.connectedToPrevious ? 1 : 0));
        stream.writeByte(static_cast<char>(note.connectedToNext ? 1 : 0));
        stream.writeFloat(note.modulation);
        stream.writeFloat(note.drift);
        for (const auto& point : note.contour)
        {
            stream.writeDouble(point.timeSeconds);
            stream.writeFloat(point.relativeCents);
            stream.writeFloat(point.withoutVibratoCents);
            stream.writeByte(static_cast<char>(point.voiced ? 1 : 0));
        }
    }
    return std::string(static_cast<const char*>(stream.getData()), stream.getDataSize());
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

void AudioEngine::rebuildLoadedClips(const ProjectData& project)
{
    loadedClips.clear();
    trackMeters.clear();
    std::unordered_map<std::string, std::shared_ptr<juce::AudioFormatReader>> readers;
    const auto anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                     [](const auto& track) { return track.solo; });
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
        for (std::size_t clipIndex = 0; clipIndex < orderedClips.size(); ++clipIndex)
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
            auto loaded = std::make_unique<LoadedClip>();
            loaded->clip = clip;
            const auto compactDeclick = std::min(0.0025, loaded->clip.durationSeconds * 0.5);
            loaded->clip.fadeInSeconds = std::max(loaded->clip.fadeInSeconds, compactDeclick);
            loaded->clip.fadeOutSeconds = std::max(loaded->clip.fadeOutSeconds, compactDeclick);
            if (clipIndex > 0)
            {
                const auto& previous = *orderedClips[clipIndex - 1];
                const auto overlap = previous.startSeconds + previous.durationSeconds - clip.startSeconds;
                if (overlap > 1.0e-6)
                    loaded->clip.fadeInSeconds = std::max(loaded->clip.fadeInSeconds,
                        std::min({ overlap, 0.1, loaded->clip.durationSeconds }));
                else if (std::abs(overlap) <= 0.002
                         && !clip.notes.empty() && clip.notes.front().connectedToPrevious)
                    loaded->clip.fadeInSeconds = std::max(loaded->clip.fadeInSeconds,
                        std::min(0.006, loaded->clip.durationSeconds * 0.5));
            }
            if (clipIndex + 1 < orderedClips.size())
            {
                const auto& next = *orderedClips[clipIndex + 1];
                const auto overlap = clip.startSeconds + clip.durationSeconds - next.startSeconds;
                if (overlap > 1.0e-6)
                    loaded->clip.fadeOutSeconds = std::max(loaded->clip.fadeOutSeconds,
                        std::min({ overlap, 0.1, loaded->clip.durationSeconds }));
                else if (std::abs(overlap) <= 0.002
                         && !clip.notes.empty() && clip.notes.back().connectedToNext)
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
                const auto cacheKey = renderKey(clip, track);
                auto& state = renderCache[cacheKey];
                if (state == nullptr) state = std::make_shared<RenderedClip>();
                loaded->rendered = state;
                if (!state->scheduled.exchange(true))
                {
                    auto request = makeRenderRequest(clip, track);
                    renderService.renderMld5File(std::move(request), [state](backend::RenderedAudio result) mutable
                    {
                        if (result.buffer.getNumSamples() <= 0 || result.sampleRate <= 0.0)
                        {
                            state->finished.store(true, std::memory_order_release);
                            return;
                        }
                        state->buffer = std::move(result.buffer);
                        state->sampleRate = result.sampleRate;
                        state->ready.store(true, std::memory_order_release);
                        state->finished.store(true, std::memory_order_release);
                    });
                }
            }
            loadedClips.push_back(std::move(loaded));
        }
    }
}

float AudioEngine::fadeGain(const ClipData& clip, double localSeconds)
{
    float gain = clip.gain;
    if (clip.fadeInSeconds > 1.0e-6)
    {
        const auto phase = static_cast<float>(juce::jlimit(0.0, 1.0,
            localSeconds / clip.fadeInSeconds));
        gain *= std::sin(juce::MathConstants<float>::halfPi * phase);
    }
    if (clip.fadeOutSeconds > 1.0e-6)
    {
        const auto phase = static_cast<float>(juce::jlimit(0.0, 1.0,
            (clip.durationSeconds - localSeconds) / clip.fadeOutSeconds));
        gain *= std::sin(juce::MathConstants<float>::halfPi * phase);
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
            const auto leftPan = std::sqrt(0.5f * (1.0f - loaded->trackPan));
            const auto rightPan = std::sqrt(0.5f * (1.0f + loaded->trackPan));
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
                const auto gain = fadeGain(clip, absoluteSeconds - clip.startSeconds) * loaded->trackGain;
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

        const auto leftPan = std::sqrt(0.5f * (1.0f - loaded->trackPan));
        const auto rightPan = std::sqrt(0.5f * (1.0f + loaded->trackPan));
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
            const auto gain = fadeGain(clip, absoluteSeconds - clip.startSeconds) * loaded->trackGain;
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
    // linked, block-lookahead gain (fast attack, slow release) after mixing so
    // those joins do not turn into digital crack/burst artefacts.
    auto mixedPeak = 0.0f;
    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
        mixedPeak = std::max(mixedPeak, info.buffer->getMagnitude(
            channel, info.startSample, info.numSamples));
    const auto targetLimiterGain = mixedPeak > 0.98f ? 0.98f / mixedPeak : 1.0f;
    auto limiterGain = masterLimiterGain.load(std::memory_order_relaxed);
    if (targetLimiterGain < limiterGain)
        limiterGain = targetLimiterGain;
    else
    {
        const auto release = 1.0f - std::exp(-static_cast<float>(info.numSamples)
            / static_cast<float>(std::max(1.0, sampleRate) * 0.18));
        limiterGain += (1.0f - limiterGain) * release;
    }
    masterLimiterGain.store(limiterGain, std::memory_order_relaxed);
    if (limiterGain < 0.99999f)
        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            info.buffer->applyGain(channel, info.startSample, info.numSamples, limiterGain);

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
