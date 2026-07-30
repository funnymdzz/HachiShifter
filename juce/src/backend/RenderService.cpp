#include "RenderService.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <signalsmith-stretch.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <utility>

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

float valueAt(const std::vector<float>& curve, double position, float fallback)
{
    if (curve.empty() || !std::isfinite(position) || position < 0.0) return fallback;
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left >= curve.size()) return fallback;
    const auto right = std::min(left + 1, curve.size() - 1);
    const auto amount = static_cast<float>(position - static_cast<double>(left));
    const auto a = curve[left];
    const auto b = curve[right];
    return std::isfinite(a) && std::isfinite(b) ? a + (b - a) * amount : fallback;
}

void applyMld5SpectralFinish(juce::AudioBuffer<float>& audio, double sampleRate,
                             double framePeriodMs,
                             const std::vector<float>& sourceMidi,
                             const std::vector<float>& targetMidi)
{
    if (audio.getNumSamples() <= 0 || sampleRate <= 0.0) return;
    const auto framePeriod = std::max(0.1, framePeriodMs);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto low = audio.getSample(channel, 0);
        auto smoothedAmount = 0.0f;
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto curvePosition = static_cast<double>(sample) / sampleRate
                * 1000.0 / framePeriod;
            const auto pitch = pitchAt(sourceMidi, targetMidi, curvePosition);
            const auto upward = pitch ? std::max(0.0f, pitch->second - pitch->first) : 0.0f;
            // Reconstructed Melodyne-style anti-alias/formant finish: high
            // upward shifts intentionally lose part of the brittle top band.
            // Neutral and downward notes keep their original air.
            const auto desiredAmount = upward > 0.75f
                ? juce::jlimit(0.0f, 0.72f, (upward - 0.75f) / 10.0f) : 0.0f;
            smoothedAmount += (desiredAmount - smoothedAmount) * 0.0025f;
            const auto cutoff = juce::jlimit(4'800.0, 12'500.0,
                12'500.0 - static_cast<double>(upward) * 680.0);
            const auto coefficient = static_cast<float>(1.0
                - std::exp(-juce::MathConstants<double>::twoPi * cutoff / sampleRate));
            const auto current = audio.getSample(channel, sample);
            low += coefficient * (current - low);
            audio.setSample(channel, sample,
                current + (low - current) * smoothedAmount);
        }
    }
}

void applyExpressionAndTension(juce::AudioBuffer<float>& audio, double sampleRate,
                               double framePeriodMs,
                               const std::vector<float>& targetMidi,
                               const std::vector<float>& noteGain,
                               const std::vector<float>& tension,
                               const std::vector<float>& breath)
{
    if (audio.getNumSamples() <= 0 || sampleRate <= 0.0) return;
    const auto framePeriod = std::max(0.1, framePeriodMs);
    const auto hasTension = std::any_of(tension.begin(), tension.end(),
        [](float value) { return std::abs(value) > 1.0e-4f; });

    // Tension is applied separately from gain/breath so its spectral tilt can
    // preserve loudness.  Combining both in one loop made +100% tension nearly
    // double the RMS even though the user only requested a timbre change.
    if (hasTension)
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            auto tiltLow = audio.getSample(channel, 0);
            auto smoothedTension = 0.0f;
            double inputPower = 0.0;
            double outputPower = 0.0;
            auto inputPeak = 0.0f;
            auto outputPeak = 0.0f;
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            {
                const auto curvePosition = static_cast<double>(sample) / sampleRate
                    * 1000.0 / framePeriod;
                const auto desiredTension = juce::jlimit(-1.0f, 1.0f,
                    valueAt(tension, curvePosition, 0.0f));
                const auto smoothing = static_cast<float>(1.0
                    - std::exp(-1.0 / (sampleRate * 0.008)));
                smoothedTension += (desiredTension - smoothedTension) * smoothing;

                const auto midi = valueAt(targetMidi, curvePosition, 60.0f);
                const auto f0 = midi > 0.0f
                    ? 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0)
                    : 220.0;
                const auto splitHz = juce::jlimit(260.0, 2'400.0, f0 * 2.0);
                const auto coefficient = static_cast<float>(1.0
                    - std::exp(-juce::MathConstants<double>::twoPi * splitHz / sampleRate));
                const auto current = audio.getSample(channel, sample);
                tiltLow += coefficient * (current - tiltLow);
                const auto tiltHigh = current - tiltLow;
                const auto highGain = std::pow(10.0f, smoothedTension * 8.0f / 20.0f);
                const auto lowGain = std::pow(10.0f, -smoothedTension * 1.5f / 20.0f);
                const auto tilted = tiltLow * lowGain + tiltHigh * highGain;
                audio.setSample(channel, sample, tilted);
                inputPower += static_cast<double>(current) * current;
                outputPower += static_cast<double>(tilted) * tilted;
                inputPeak = std::max(inputPeak, std::abs(current));
                outputPeak = std::max(outputPeak, std::abs(tilted));
            }

            const auto inputRms = std::sqrt(inputPower / audio.getNumSamples());
            const auto outputRms = std::sqrt(outputPower / audio.getNumSamples());
            auto correction = inputRms > 1.0e-8 && outputRms > 1.0e-8
                ? static_cast<float>(inputRms / outputRms) : 1.0f;
            correction = juce::jlimit(0.4f, 2.5f, correction);
            if (inputPeak > 1.0e-6f && outputPeak > 1.0e-6f)
                correction = std::min(correction, inputPeak * 1.6f / outputPeak);
            audio.applyGain(channel, 0, audio.getNumSamples(), correction);
        }

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto airLow = audio.getSample(channel, 0);
        const auto airCoefficient = static_cast<float>(1.0
            - std::exp(-juce::MathConstants<double>::twoPi * 3500.0 / sampleRate));
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto curvePosition = static_cast<double>(sample) / sampleRate
                * 1000.0 / framePeriod;
            const auto gain = juce::jlimit(0.0f, 4.0f,
                valueAt(noteGain, curvePosition, 1.0f));
            const auto air = juce::jlimit(0.0f, 1.0f,
                valueAt(breath, curvePosition, 0.0f));
            const auto current = audio.getSample(channel, sample);
            airLow += airCoefficient * (current - airLow);
            const auto airHigh = current - airLow;
            audio.setSample(channel, sample, (current + airHigh * air * 0.22f) * gain);
        }
    }
}

juce::AudioBuffer<float> renderFormantPreserved(const juce::AudioBuffer<float>& source,
                                                 int targetSamples,
                                                 double sampleRate,
                                                 double framePeriodMs,
                                                 const std::vector<float>& sourceMidi,
                                                 const std::vector<float>& targetMidi,
                                                 const std::vector<float>& formantSemitones,
                                                 const std::vector<float>& noteGain,
                                                 const std::vector<float>& tension,
                                                 const std::vector<float>& breath,
                                                 const std::vector<TimeMapPoint>& timeMap,
                                                 PitchRenderBackend pitchBackend,
                                                 int stretchAlgorithm)
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
    auto hasTimeWarp = sourceSamples != targetSamples;
    for (const auto& point : timeMap)
        if (targetSamples > 0 && sourceSamples > 0
            && std::abs(point.sourceSeconds * sampleRate / sourceSamples
                        - point.targetSeconds * sampleRate / targetSamples) > 1.0e-6)
        {
            hasTimeWarp = true;
            break;
        }
    auto hasFormantEdit = false;
    auto hasLevelEdit = false;
    for (const auto value : formantSemitones)
        hasFormantEdit = hasFormantEdit || std::abs(value) > 1.0e-4f;
    for (const auto value : noteGain)
        hasLevelEdit = hasLevelEdit || std::abs(value - 1.0f) > 1.0e-4f;
    for (const auto value : tension)
        hasLevelEdit = hasLevelEdit || std::abs(value) > 1.0e-4f;
    for (const auto value : breath)
        hasLevelEdit = hasLevelEdit || value > 1.0e-4f;
    if (!hasTimeWarp && !hasPitchEdit && !hasFormantEdit && !hasLevelEdit) return source;

    // Gain and breath are amplitude/spectral expression controls.  Sending an
    // otherwise untouched note through an identity phase vocoder created the
    // audible doubled/echoed voice reported on the native rewrite.  Keep that
    // path sample-exact and apply expression below without a phase transform.
    const auto needsSpectralRender = hasTimeWarp || hasPitchEdit || hasFormantEdit;
    juce::AudioBuffer<float> result;
    if (!needsSpectralRender)
    {
        result = source;
        applyExpressionAndTension(result, sampleRate, framePeriodMs, targetMidi,
                                  noteGain, tension, breath);
        return result;
    }

    // Signalsmith's phase-coherent stretcher is used as the native model-free
    // component stage.  Formant compensation is explicitly enabled: pitch and
    // duration move independently while the vocal-tract envelope remains at
    // the source frequencies.  A fixed seed makes cache renders repeatable.
    signalsmith::stretch::SignalsmithStretch<float> stretch(0x48414348L);
    // Keep both mld5 and the model-free nsf-hifigan fallback on the same proven
    // single-pass, low-latency phase clock.  The former presetDefault route had
    // a much longer analysis window and audibly pre/post-rang short vocal
    // elements, which was perceived as an echo.  mld5-specific treatment is
    // deliberately applied only once after this common render below.
    const auto stableVocalClock = pitchBackend == PitchRenderBackend::mld5
        || pitchBackend == PitchRenderBackend::nsfHifigan;
    if (stableVocalClock || stretchAlgorithm != 0)
        stretch.presetCheaper(channels, static_cast<float>(sampleRate), false);
    else
        stretch.presetDefault(channels, static_cast<float>(sampleRate), false);
    stretch.setFormantSemitones(valueAt(formantSemitones, 0.0, 0.0f), true);
    stretch.setFormantBase(200.0f / static_cast<float>(sampleRate));

    const auto inputLatency = stretch.inputLatency();
    const auto outputLatency = stretch.outputLatency();
    struct StretchChunk { int sourceStart; int sourceFrames; int targetStart; int targetFrames; };
    std::vector<StretchChunk> chunks;
    auto previousSource = 0;
    auto previousTarget = 0;
    for (std::size_t index = 1; index < timeMap.size(); ++index)
    {
        const auto nextSource = juce::jlimit(previousSource, sourceSamples,
            static_cast<int>(std::llround(timeMap[index].sourceSeconds * sampleRate)));
        const auto nextTarget = juce::jlimit(previousTarget, targetSamples,
            static_cast<int>(std::llround(timeMap[index].targetSeconds * sampleRate)));
        if (nextSource > previousSource && nextTarget > previousTarget)
            chunks.push_back({ previousSource, nextSource - previousSource,
                               previousTarget, nextTarget - previousTarget });
        previousSource = nextSource;
        previousTarget = nextTarget;
    }
    if (previousSource < sourceSamples && previousTarget < targetSamples)
        chunks.push_back({ previousSource, sourceSamples - previousSource,
                           previousTarget, targetSamples - previousTarget });
    if (chunks.empty())
        chunks.push_back({ 0, sourceSamples, 0, targetSamples });
    else
    {
        auto& last = chunks.back();
        last.sourceFrames += sourceSamples - (last.sourceStart + last.sourceFrames);
        last.targetFrames += targetSamples - (last.targetStart + last.targetFrames);
    }
    std::vector<std::vector<float>> paddedInput(static_cast<std::size_t>(channels));
    std::vector<std::vector<float>> allOutput(static_cast<std::size_t>(channels));
    for (int channel = 0; channel < channels; ++channel)
    {
        auto& input = paddedInput[static_cast<std::size_t>(channel)];
        input.resize(static_cast<std::size_t>(sourceSamples + inputLatency), 0.0f);
        std::copy_n(source.getReadPointer(channel), sourceSamples, input.begin());
        allOutput[static_cast<std::size_t>(channel)].reserve(
            static_cast<std::size_t>(targetSamples + outputLatency));
    }

    // Signalsmith reads inputLatency samples ahead of its processing clock.
    // Seed that look-ahead once, then process a stream shifted by exactly that
    // amount and discard only outputLatency.  Including both latencies in the
    // stretch ratio (the previous implementation) made short notes approach
    // 1x and displaced attacks/F0 relative to the requested duration.
    if (const auto initialPitch = pitchAt(sourceMidi, targetMidi, 0.0))
    {
        stretch.setTransposeSemitones(
            juce::jlimit(-24.0f, 24.0f, initialPitch->second - initialPitch->first),
            8'000.0f / static_cast<float>(sampleRate));
        const auto sourceHz = 440.0f * std::pow(2.0f, (initialPitch->first - 69.0f) / 12.0f);
        stretch.setFormantBase(juce::jlimit(45.0f, 1'200.0f, sourceHz)
                               / static_cast<float>(sampleRate));
    }
    // Prime Signalsmith once with the analysis look-ahead, then advance the
    // input stream by exactly that amount.  This mirrors the proven JS/Rust
    // wrapper.  Feeding sample zero again after priming creates two phase
    // histories for the same vowel (heard as a short echo), while cropping an
    // additional input-latency interval moves the waveform away from its F0.
    if (inputLatency > 0)
    {
        std::vector<const float*> seekPointers(static_cast<std::size_t>(channels));
        for (int channel = 0; channel < channels; ++channel)
            seekPointers[static_cast<std::size_t>(channel)] =
                paddedInput[static_cast<std::size_t>(channel)].data();
        const auto& firstChunk = chunks.front();
        const auto playbackRate = static_cast<double>(firstChunk.sourceFrames)
            / std::max(1, firstChunk.targetFrames);
        stretch.seek(seekPointers.data(), inputLatency, playbackRate);
    }
    // These are separate render clocks, rather than four UI aliases for the
    // same block scheduler.  Variable-mel follows edits more densely; the loop
    // path favours stable repeated vowels; SoundTouch-style operation updates
    // in longer grains.  Melodyne Hybrid stays at the proven vocal setting.
    const auto blockSize = stretchAlgorithm == 1 ? 64
        : stretchAlgorithm == 2 ? 256
        : stretchAlgorithm == 3 ? 512 : 128;
    auto outputProduced = 0;
    const auto framePeriod = std::max(0.1, framePeriodMs);
    for (const auto& chunk : chunks)
    {
      auto chunkInput = 0;
      auto chunkOutput = 0;
      while (chunkInput < chunk.sourceFrames && chunkOutput < chunk.targetFrames)
      {
        const auto blockInput = std::min(blockSize, chunk.sourceFrames - chunkInput);
        const auto expectedNextOutput = static_cast<int>(std::llround(
            static_cast<double>(chunkInput + blockInput) * chunk.targetFrames / chunk.sourceFrames));
        const auto blockOutput = std::min(chunk.targetFrames - chunkOutput,
            std::max(1, expectedNextOutput - chunkOutput));
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
        stretch.setFormantSemitones(valueAt(formantSemitones, curvePosition, 0.0f), true);

        std::vector<const float*> inputPointers(static_cast<std::size_t>(channels));
        std::vector<std::vector<float>> blockBuffers(static_cast<std::size_t>(channels));
        std::vector<float*> outputPointers(static_cast<std::size_t>(channels));
        for (int channel = 0; channel < channels; ++channel)
        {
            inputPointers[static_cast<std::size_t>(channel)] =
                paddedInput[static_cast<std::size_t>(channel)].data()
                    + inputLatency + chunk.sourceStart + chunkInput;
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
        chunkInput += blockInput;
        chunkOutput += blockOutput;
        outputProduced += blockOutput;
      }
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

    if (needsSpectralRender)
    {
        result.setSize(channels, targetSamples);
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
    if (needsSpectralRender && sourceRms > 1.0e-7 && outputRms > 1.0e-7)
        result.applyGain(static_cast<float>(juce::jlimit(0.55, 1.8, sourceRms / outputRms)));

    applyExpressionAndTension(result, sampleRate, framePeriodMs, targetMidi,
                              noteGain, tension, breath);

    // Component clips are cut at analysis element boundaries, which are not
    // guaranteed zero crossings.  Melodyne applies a compact amplitude/phase
    // hand-off there even when no user fade is visible.  Keep this de-click
    // shorter than a consonant so it removes the impulse without smearing it.
    const auto edgeSamples = std::min(targetSamples / 2,
        std::max(1, static_cast<int>(std::llround(sampleRate * 0.0025))));
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* samples = result.getWritePointer(channel);
        for (int index = 0; index < edgeSamples; ++index)
        {
            const auto phase = static_cast<float>(index + 1)
                / static_cast<float>(edgeSamples + 1);
            const auto gain = std::sin(juce::MathConstants<float>::halfPi * phase);
            samples[index] *= gain;
            samples[targetSamples - 1 - index] *= gain;
        }
    }
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
        juce::AudioBuffer<float> rendered;
        if (request.pitchBackend == PitchRenderBackend::mld5)
        {
            // The file/playback entry must be one persistent render pass.  The
            // previous neutral stretch + component pass processed every vowel
            // twice and was the actual source of the reported mld5 echo.  The
            // NSF fallback already demonstrated the correct clock and envelope
            // behaviour, so mld5 now shares that proven base and adds its own
            // high-band finish below.
            rendered = renderFormantPreserved(source, targetSamples, reader->sampleRate,
                request.framePeriodMs, request.sourceMidi, request.targetMidi,
                request.formantSemitones, request.noteGain, request.tension, request.breath,
                request.timeMap,
                request.pitchBackend, request.stretchAlgorithm);
            applyMld5SpectralFinish(rendered, reader->sampleRate, request.framePeriodMs,
                                    request.sourceMidi, request.targetMidi);
        }
        else
            rendered = renderFormantPreserved(source, targetSamples, reader->sampleRate,
                request.framePeriodMs, request.sourceMidi, request.targetMidi,
                request.formantSemitones, request.noteGain, request.tension, request.breath,
                request.timeMap,
                request.pitchBackend, request.stretchAlgorithm);
        auto backend = request.pitchBackend == PitchRenderBackend::mld5
            ? juce::String("mld5-single-pass-stable-clock")
            : request.pitchBackend == PitchRenderBackend::nsfHifigan ? juce::String("nsf-hifigan")
            : request.pitchBackend == PitchRenderBackend::world ? juce::String("WORLD")
            : juce::String("vslib");
        backend += request.stretchAlgorithm == 1 ? "+variable-mel-hop"
            : request.stretchAlgorithm == 2 ? "+loop"
            : request.stretchAlgorithm == 3 ? "+soundtouch"
            : "+melodyne-hybrid";
        RenderedAudio result { std::move(rendered), reader->sampleRate, backend };
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
