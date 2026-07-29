#include "AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace hachi
{
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
        for (const auto& clip : track.clips)
        {
            if (clip.muted || !clip.sourceFile.existsAsFile()) continue;
            const auto key = clip.sourceFile.getFullPathName().toStdString();
            auto reader = readers[key];
            if (reader == nullptr)
            {
                reader.reset(formatManager.createReaderFor(clip.sourceFile));
                if (reader == nullptr) continue;
                readers[key] = reader;
            }
            auto loaded = std::make_unique<LoadedClip>();
            loaded->clip = clip;
            loaded->trackGain = track.volume;
            loaded->trackPan = juce::jlimit(-1.0f, 1.0f, track.pan);
            loaded->meter = meter;
            loaded->reader = reader;
            loadedClips.push_back(std::move(loaded));
        }
    }
}

float AudioEngine::fadeGain(const ClipData& clip, double localSeconds)
{
    float gain = clip.gain;
    if (clip.fadeInSeconds > 1.0e-6)
        gain *= static_cast<float>(juce::jlimit(0.0, 1.0, localSeconds / clip.fadeInSeconds));
    if (clip.fadeOutSeconds > 1.0e-6)
        gain *= static_cast<float>(juce::jlimit(0.0, 1.0,
            (clip.durationSeconds - localSeconds) / clip.fadeOutSeconds));
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
        sendChangeMessage();
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

    timelineSample.fetch_add(info.numSamples);
    sendChangeMessage();
}

void AudioEngine::play()
{
    playing.store(true);
    sendChangeMessage();
}

void AudioEngine::stop()
{
    playing.store(false);
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
}
