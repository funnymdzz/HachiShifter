#include "RenderService.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <signalsmith-stretch.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace hachi::backend
{
namespace
{
std::optional<std::pair<float, float>> pitchAt(const std::vector<float>& sourceMidi,
                                                const std::vector<float>& targetMidi,
                                                double position)
{
    if (sourceMidi.empty() || targetMidi.empty() || !std::isfinite(position) || position < 0.0)
        return std::nullopt;
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left >= sourceMidi.size() || left >= targetMidi.size()) return std::nullopt;
    const auto right = std::min({ left + 1, sourceMidi.size() - 1, targetMidi.size() - 1 });
    const auto amount = static_cast<float>(position - static_cast<double>(left));
    const auto source = sourceMidi[left] + (sourceMidi[right] - sourceMidi[left]) * amount;
    const auto target = targetMidi[left] + (targetMidi[right] - targetMidi[left]) * amount;
    if (!(std::isfinite(source) && std::isfinite(target) && source > 0.0f && target > 0.0f))
        return std::nullopt;
    return std::pair { source, target };
}

juce::AudioBuffer<float> renderFormantPreserved(const juce::AudioBuffer<float>& source,
                                                 int targetSamples,
                                                 double sampleRate,
                                                 double framePeriodMs,
                                                 const std::vector<float>& sourceMidi,
                                                 const std::vector<float>& targetMidi)
{
    const auto sourceSamples = source.getNumSamples();
    const auto channels = source.getNumChannels();
    if (sourceSamples <= 0 || channels <= 0 || targetSamples <= 0 || sampleRate <= 0.0) return {};

    auto hasPitchEdit = false;
    const auto curveSize = std::min(sourceMidi.size(), targetMidi.size());
    for (std::size_t index = 0; index < curveSize; ++index)
        if (sourceMidi[index] > 0.0f && targetMidi[index] > 0.0f
            && std::abs(targetMidi[index] - sourceMidi[index]) > 1.0e-4f)
        {
            hasPitchEdit = true;
            break;
        }
    if (sourceSamples == targetSamples && !hasPitchEdit) return source;

    // Signalsmith's phase-coherent stretcher is used as the native model-free
    // component stage.  Formant compensation is explicitly enabled: pitch and
    // duration move independently while the vocal-tract envelope remains at
    // the source frequencies.  A fixed seed makes cache renders repeatable.
    signalsmith::stretch::SignalsmithStretch<float> stretch(0x48414348L);
    stretch.presetDefault(channels, static_cast<float>(sampleRate), false);
    stretch.setFormantFactor(1.0f, true);
    stretch.setFormantBase(200.0f / static_cast<float>(sampleRate));

    const auto inputLatency = stretch.inputLatency();
    const auto outputLatency = stretch.outputLatency();
    const auto totalInput = sourceSamples + inputLatency;
    const auto totalOutput = targetSamples + outputLatency;

    std::vector<std::vector<float>> paddedInput(static_cast<std::size_t>(channels));
    std::vector<std::vector<float>> allOutput(static_cast<std::size_t>(channels));
    for (int channel = 0; channel < channels; ++channel)
    {
        auto& input = paddedInput[static_cast<std::size_t>(channel)];
        input.resize(static_cast<std::size_t>(totalInput), 0.0f);
        std::copy_n(source.getReadPointer(channel), sourceSamples, input.begin());
        allOutput[static_cast<std::size_t>(channel)].reserve(
            static_cast<std::size_t>(totalOutput + outputLatency));
    }

    constexpr int blockSize = 512;
    auto inputConsumed = 0;
    auto outputProduced = 0;
    const auto framePeriod = std::max(0.1, framePeriodMs);
    while (inputConsumed < totalInput && outputProduced < totalOutput)
    {
        const auto blockInput = std::min(blockSize, totalInput - inputConsumed);
        const auto expectedNextOutput = static_cast<int>(std::llround(
            static_cast<double>(inputConsumed + blockInput) * totalOutput / totalInput));
        const auto blockOutput = std::max(1, expectedNextOutput - outputProduced);
        const auto audibleCentre = juce::jlimit(0.0, static_cast<double>(targetSamples),
            static_cast<double>(outputProduced + blockOutput / 2 - outputLatency));
        const auto curvePosition = audibleCentre / sampleRate * 1000.0 / framePeriod;
        const auto pitch = pitchAt(sourceMidi, targetMidi, curvePosition);
        if (pitch)
        {
            const auto semitones = juce::jlimit(-24.0f, 24.0f, pitch->second - pitch->first);
            stretch.setTransposeSemitones(semitones, 8'000.0f / static_cast<float>(sampleRate));
            const auto sourceHz = 440.0f * std::pow(2.0f, (pitch->first - 69.0f) / 12.0f);
            stretch.setFormantBase(juce::jlimit(45.0f, 1'200.0f, sourceHz)
                                   / static_cast<float>(sampleRate));
        }
        else
        {
            // Unvoiced, breath and sibilant regions keep their original spectrum.
            stretch.setTransposeSemitones(0.0f);
        }

        std::vector<const float*> inputPointers(static_cast<std::size_t>(channels));
        std::vector<std::vector<float>> blockBuffers(static_cast<std::size_t>(channels));
        std::vector<float*> outputPointers(static_cast<std::size_t>(channels));
        for (int channel = 0; channel < channels; ++channel)
        {
            inputPointers[static_cast<std::size_t>(channel)] =
                paddedInput[static_cast<std::size_t>(channel)].data() + inputConsumed;
            auto& block = blockBuffers[static_cast<std::size_t>(channel)];
            block.resize(static_cast<std::size_t>(blockOutput), 0.0f);
            outputPointers[static_cast<std::size_t>(channel)] = block.data();
        }
        stretch.process(inputPointers.data(), blockInput, outputPointers.data(), blockOutput);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& output = allOutput[static_cast<std::size_t>(channel)];
            const auto& block = blockBuffers[static_cast<std::size_t>(channel)];
            output.insert(output.end(), block.begin(), block.end());
        }
        inputConsumed += blockInput;
        outputProduced += blockOutput;
    }

    if (outputLatency > 0)
    {
        std::vector<std::vector<float>> flushBuffers(static_cast<std::size_t>(channels));
        std::vector<float*> flushPointers(static_cast<std::size_t>(channels));
        for (int channel = 0; channel < channels; ++channel)
        {
            flushBuffers[static_cast<std::size_t>(channel)].resize(
                static_cast<std::size_t>(outputLatency), 0.0f);
            flushPointers[static_cast<std::size_t>(channel)] =
                flushBuffers[static_cast<std::size_t>(channel)].data();
        }
        stretch.flush(flushPointers.data(), outputLatency);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& output = allOutput[static_cast<std::size_t>(channel)];
            const auto& flush = flushBuffers[static_cast<std::size_t>(channel)];
            output.insert(output.end(), flush.begin(), flush.end());
        }
    }

    juce::AudioBuffer<float> result(channels, targetSamples);
    result.clear();
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = result.getWritePointer(channel);
        const auto& output = allOutput[static_cast<std::size_t>(channel)];
        for (int sample = 0; sample < targetSamples; ++sample)
        {
            const auto sourceIndex = static_cast<std::size_t>(outputLatency + sample);
            destination[sample] = sourceIndex < output.size() && std::isfinite(output[sourceIndex])
                ? output[sourceIndex] : 0.0f;
        }
    }

    // The component stage is energy preserving.  Bound the correction so a
    // quiet consonant or a clipped source does not create a gain jump.
    double sourcePower = 0.0;
    double outputPower = 0.0;
    for (int channel = 0; channel < channels; ++channel)
    {
        for (int sample = 0; sample < sourceSamples; ++sample)
        {
            const auto value = source.getSample(channel, sample);
            sourcePower += static_cast<double>(value) * value;
        }
        for (int sample = 0; sample < targetSamples; ++sample)
        {
            const auto value = result.getSample(channel, sample);
            outputPower += static_cast<double>(value) * value;
        }
    }
    const auto sourceRms = std::sqrt(sourcePower / std::max(1, channels * sourceSamples));
    const auto outputRms = std::sqrt(outputPower / std::max(1, channels * targetSamples));
    if (sourceRms > 1.0e-7 && outputRms > 1.0e-7)
        result.applyGain(static_cast<float>(juce::jlimit(0.55, 1.8, sourceRms / outputRms)));
    return result;
}
}

class RenderService::RenderJob final : public juce::ThreadPoolJob
{
public:
    RenderJob(Mld5RenderRequest requestToUse, Completion completionToUse)
        : ThreadPoolJob("mld5-render"), request(std::move(requestToUse)), completion(std::move(completionToUse))
    {
        if (request.input != nullptr)
        {
            ownedInput = *request.input;
            request.input = &ownedInput;
        }
    }

    JobStatus runJob() override
    {
        if (shouldExit()) return jobHasFinished;
        auto output = renderer.render(request);
        if (shouldExit()) return jobHasFinished;
        juce::MessageManager::callAsync([callback = std::move(completion), result = std::move(output)]() mutable
        {
            if (callback) callback(std::move(result));
        });
        return jobHasFinished;
    }

private:
    juce::AudioBuffer<float> ownedInput;
    Mld5RenderRequest request;
    Completion completion;
    Mld5Renderer renderer;
};

class RenderService::FileRenderJob final : public juce::ThreadPoolJob
{
public:
    FileRenderJob(Mld5FileRenderRequest requestToUse, FileCompletion completionToUse)
        : ThreadPoolJob("mld5-file-render"), request(std::move(requestToUse)),
          completion(std::move(completionToUse))
    {
    }

    JobStatus runJob() override
    {
        if (shouldExit()) return jobHasFinished;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(request.sourceFile));
        if (reader == nullptr || reader->sampleRate <= 0.0) return jobHasFinished;
        const auto sourceStart = juce::jlimit<juce::int64>(0, reader->lengthInSamples,
            static_cast<juce::int64>(std::llround(request.sourceOffsetSeconds * reader->sampleRate)));
        const auto requestedSourceSamples = static_cast<juce::int64>(std::llround(
            std::max(0.001, request.sourceDurationSeconds) * reader->sampleRate));
        const auto sourceSamples64 = std::max<juce::int64>(1,
            std::min(requestedSourceSamples, reader->lengthInSamples - sourceStart));
        const auto sourceSamples = static_cast<int>(std::min<juce::int64>(sourceSamples64,
                                                                          std::numeric_limits<int>::max()));
        const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
        juce::AudioBuffer<float> source(channels, sourceSamples);
        source.clear();
        reader->read(&source, 0, sourceSamples, sourceStart, true, channels > 1);
        if (shouldExit()) return jobHasFinished;

        const auto targetSamples = std::max(1, static_cast<int>(std::llround(
            std::max(0.001, request.targetDurationSeconds) * reader->sampleRate)));
        auto rendered = renderFormantPreserved(source, targetSamples, reader->sampleRate,
                                               request.framePeriodMs, request.sourceMidi,
                                               request.targetMidi);
        RenderedAudio result { std::move(rendered), reader->sampleRate };
        if (shouldExit()) return jobHasFinished;
        juce::MessageManager::callAsync([callback = std::move(completion), result = std::move(result)]() mutable
        {
            if (callback) callback(std::move(result));
        });
        return jobHasFinished;
    }

private:
    Mld5FileRenderRequest request;
    FileCompletion completion;
};

RenderService::RenderService()
    : pool(std::max(1, juce::SystemStats::getNumCpus() - 1))
{
}

RenderService::~RenderService()
{
    cancelAll();
}

void RenderService::renderMld5(Mld5RenderRequest request, Completion completion)
{
    pool.addJob(new RenderJob(std::move(request), std::move(completion)), true);
}

void RenderService::renderMld5File(Mld5FileRenderRequest request, FileCompletion completion)
{
    pool.addJob(new FileRenderJob(std::move(request), std::move(completion)), true);
}

void RenderService::cancelAll()
{
    pool.removeAllJobs(true, 10'000);
}
}
