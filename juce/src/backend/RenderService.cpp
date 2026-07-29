#include "RenderService.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>

namespace hachi::backend
{
namespace
{
float wrappedPhase(float value)
{
    constexpr auto pi = juce::MathConstants<float>::pi;
    constexpr auto twoPi = juce::MathConstants<float>::twoPi;
    auto wrapped = std::fmod(value + pi, twoPi);
    if (wrapped < 0.0f) wrapped += twoPi;
    return wrapped - pi;
}

float reflectedSample(const float* input, int length, int position)
{
    if (length <= 0) return 0.0f;
    if (length == 1) return input[0];
    const auto period = length * 2 - 2;
    auto phase = position % period;
    if (phase < 0) phase += period;
    return input[phase < length ? phase : period - phase];
}

juce::AudioBuffer<float> stretchPreservingPitch(const juce::AudioBuffer<float>& source,
                                                 int targetSamples)
{
    const auto sourceSamples = source.getNumSamples();
    const auto channels = source.getNumChannels();
    if (sourceSamples <= 0 || channels <= 0 || targetSamples <= 0) return {};
    if (sourceSamples == targetSamples) return source;
    constexpr int fftOrder = 11;
    constexpr int fftSize = 1 << fftOrder;
    constexpr int analysisHop = fftSize / 8;
    constexpr int half = fftSize / 2;
    constexpr int pad = fftSize / 2;
    const auto ratio = static_cast<double>(targetSamples) / static_cast<double>(sourceSamples);
    const auto frameCount = (sourceSamples + analysisHop - 1) / analysisHop + 1;
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> window(static_cast<std::size_t>(fftSize));
    for (int index = 0; index < fftSize; ++index)
        window[static_cast<std::size_t>(index)] = std::sqrt(std::max(0.0f,
            0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                  * static_cast<float>(index) / static_cast<float>(fftSize))));
    juce::AudioBuffer<float> result(channels, targetSamples);
    result.clear();
    using Complex = std::complex<float>;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* input = source.getReadPointer(channel);
        std::vector<Complex> frameData(static_cast<std::size_t>(fftSize));
        std::vector<Complex> spectrum(static_cast<std::size_t>(fftSize));
        std::vector<Complex> synthesisSpectrum(static_cast<std::size_t>(fftSize));
        std::vector<Complex> inverse(static_cast<std::size_t>(fftSize));
        std::vector<float> previousPhase(static_cast<std::size_t>(half + 1));
        std::vector<float> synthesisPhase(static_cast<std::size_t>(half + 1));
        std::vector<float> previousMagnitude(static_cast<std::size_t>(half + 1));
        std::vector<float> output(static_cast<std::size_t>(targetSamples + fftSize));
        std::vector<float> normalisation(output.size());
        for (int frame = 0; frame < frameCount; ++frame)
        {
            const auto analysisCentre = frame * analysisHop;
            for (int index = 0; index < fftSize; ++index)
                frameData[static_cast<std::size_t>(index)] = Complex(
                    reflectedSample(input, sourceSamples, analysisCentre - pad + index)
                        * window[static_cast<std::size_t>(index)], 0.0f);
            fft.perform(frameData.data(), spectrum.data(), false);
            auto energy = 1.0e-9f;
            auto positiveFlux = 0.0f;
            for (int bin = 0; bin <= half; ++bin)
            {
                const auto magnitude = std::abs(spectrum[static_cast<std::size_t>(bin)]);
                energy += magnitude;
                positiveFlux += std::max(0.0f, magnitude - previousMagnitude[static_cast<std::size_t>(bin)]);
            }
            const auto transient = frame == 0 || positiveFlux / energy > 0.34f;
            std::fill(synthesisSpectrum.begin(), synthesisSpectrum.end(), Complex {});
            for (int bin = 0; bin <= half; ++bin)
            {
                const auto magnitude = std::abs(spectrum[static_cast<std::size_t>(bin)]);
                const auto phase = std::arg(spectrum[static_cast<std::size_t>(bin)]);
                const auto expected = juce::MathConstants<float>::twoPi
                    * static_cast<float>(bin * analysisHop) / static_cast<float>(fftSize);
                const auto delta = wrappedPhase(phase - previousPhase[static_cast<std::size_t>(bin)] - expected);
                synthesisPhase[static_cast<std::size_t>(bin)] = transient
                    ? phase
                    : wrappedPhase(synthesisPhase[static_cast<std::size_t>(bin)]
                                   + (expected + delta) * static_cast<float>(ratio));
                synthesisSpectrum[static_cast<std::size_t>(bin)] = std::polar(
                    magnitude, synthesisPhase[static_cast<std::size_t>(bin)]);
                previousPhase[static_cast<std::size_t>(bin)] = phase;
                previousMagnitude[static_cast<std::size_t>(bin)] = magnitude;
            }
            for (int bin = 1; bin < half; ++bin)
                synthesisSpectrum[static_cast<std::size_t>(fftSize - bin)] =
                    std::conj(synthesisSpectrum[static_cast<std::size_t>(bin)]);
            fft.perform(synthesisSpectrum.data(), inverse.data(), true);
            const auto synthesisCentre = static_cast<int>(std::llround(static_cast<double>(frame * analysisHop)
                                                                        * ratio));
            for (int index = 0; index < fftSize; ++index)
            {
                const auto destination = synthesisCentre + index;
                if (destination >= static_cast<int>(output.size())) break;
                const auto weight = window[static_cast<std::size_t>(index)];
                output[static_cast<std::size_t>(destination)] += inverse[static_cast<std::size_t>(index)].real()
                    * weight;
                normalisation[static_cast<std::size_t>(destination)] += weight * weight;
            }
        }
        auto* destination = result.getWritePointer(channel);
        for (int sample = 0; sample < targetSamples; ++sample)
        {
            const auto padded = sample + pad;
            const auto norm = normalisation[static_cast<std::size_t>(padded)];
            destination[sample] = norm > 1.0e-6f
                ? output[static_cast<std::size_t>(padded)] / norm : 0.0f;
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
        auto stretched = stretchPreservingPitch(source, targetSamples);
        if (shouldExit()) return jobHasFinished;
        Mld5RenderRequest renderRequest;
        renderRequest.input = &stretched;
        renderRequest.sampleRate = reader->sampleRate;
        renderRequest.framePeriodMs = request.framePeriodMs;
        renderRequest.sourceMidi = std::move(request.sourceMidi);
        renderRequest.targetMidi = std::move(request.targetMidi);
        RenderedAudio result { renderer.render(renderRequest), reader->sampleRate };
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
    Mld5Renderer renderer;
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
