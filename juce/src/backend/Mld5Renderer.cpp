#include "Mld5Renderer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <optional>

namespace hachi::backend
{
namespace
{
float wrapPhase(float value)
{
    constexpr auto pi = juce::MathConstants<float>::pi;
    constexpr auto twoPi = juce::MathConstants<float>::twoPi;
    auto wrapped = std::fmod(value + pi, twoPi);
    if (wrapped < 0.0f) wrapped += twoPi;
    return wrapped - pi;
}

float reflected(const float* input, int length, int position)
{
    if (length <= 0) return 0.0f;
    if (length == 1) return input[0];
    const auto period = length * 2 - 2;
    auto phase = position % period;
    if (phase < 0) phase += period;
    const auto index = phase < length ? phase : period - phase;
    return input[index];
}

std::optional<float> curveAt(const std::vector<float>& curve, double position)
{
    if (curve.empty() || !std::isfinite(position) || position < 0.0) return std::nullopt;
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left >= curve.size()) return std::nullopt;
    const auto right = std::min(left + 1, curve.size() - 1);
    const auto fraction = static_cast<float>(position - static_cast<double>(left));
    const auto a = curve[left];
    const auto b = curve[right];
    if (!(std::isfinite(a) && std::isfinite(b) && a > 0.0f && b > 0.0f)) return std::nullopt;
    return a + (b - a) * fraction;
}
}

juce::AudioBuffer<float> Mld5Renderer::render(const Mld5RenderRequest& request) const
{
    if (request.input == nullptr || request.input->getNumSamples() == 0)
        return {};
    juce::AudioBuffer<float> result(request.input->getNumChannels(), request.input->getNumSamples());
    for (int channel = 0; channel < request.input->getNumChannels(); ++channel)
    {
        const auto rendered = renderMono(request.input->getReadPointer(channel),
                                         request.input->getNumSamples(), request.sampleRate,
                                         request.framePeriodMs, request.sourceMidi,
                                         request.targetMidi);
        result.copyFrom(channel, 0, rendered.data(), static_cast<int>(rendered.size()));
    }
    return result;
}

std::vector<float> Mld5Renderer::renderMono(const float* input, int inputLength,
                                            double sampleRate, double framePeriodMs,
                                            const std::vector<float>& sourceMidi,
                                            const std::vector<float>& targetMidi)
{
    if (inputLength < 64 || sampleRate <= 0.0)
        return { input, input + std::max(0, inputLength) };

    const auto fftOrder = sampleRate >= 32'000.0 ? 11 : 10;
    const auto fftSize = 1 << fftOrder;
    const auto hop = fftSize / 8;
    const auto half = fftSize / 2;
    const auto pad = fftSize / 2;
    const auto frameCount = (inputLength + hop - 1) / hop + 1;
    const auto framePeriod = std::max(0.1, framePeriodMs);
    juce::dsp::FFT fft(fftOrder);

    std::vector<float> window(static_cast<std::size_t>(fftSize));
    for (int index = 0; index < fftSize; ++index)
        window[static_cast<std::size_t>(index)] = std::sqrt(std::max(0.0f,
            0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                  * static_cast<float>(index) / static_cast<float>(fftSize))));

    using Complex = std::complex<float>;
    std::vector<Complex> timeDomain(static_cast<std::size_t>(fftSize));
    std::vector<Complex> spectrum(static_cast<std::size_t>(fftSize));
    std::vector<Complex> shifted(static_cast<std::size_t>(fftSize));
    std::vector<Complex> inverse(static_cast<std::size_t>(fftSize));
    std::vector<float> previousPhase(static_cast<std::size_t>(half + 1));
    std::vector<float> synthesisPhase(static_cast<std::size_t>(half + 1));
    std::vector<float> previousMagnitude(static_cast<std::size_t>(half + 1));
    std::vector<float> magnitude(static_cast<std::size_t>(half + 1));
    std::vector<float> harmonicMagnitude(static_cast<std::size_t>(half + 1));
    std::vector<float> residualMagnitude(static_cast<std::size_t>(half + 1));
    std::vector<float> mappedMagnitude(static_cast<std::size_t>(half + 1));
    std::vector<float> phase(static_cast<std::size_t>(half + 1));
    std::vector<float> logEnvelope(static_cast<std::size_t>(half + 1));
    std::vector<float> prefix(static_cast<std::size_t>(half + 2));
    std::vector<float> output(static_cast<std::size_t>(inputLength + fftSize));
    std::vector<float> normalisation(output.size());
    const auto expectedScale = juce::MathConstants<float>::twoPi
        * static_cast<float>(hop) / static_cast<float>(fftSize);
    auto smoothedFrameGain = 1.0f;

    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto centre = frame * hop;
        const auto inputStart = centre - pad;
        for (int index = 0; index < fftSize; ++index)
            timeDomain[static_cast<std::size_t>(index)] = Complex(
                reflected(input, inputLength, inputStart + index) * window[static_cast<std::size_t>(index)], 0.0f);
        fft.perform(timeDomain.data(), spectrum.data(), false);
        for (int bin = 0; bin <= half; ++bin)
        {
            magnitude[static_cast<std::size_t>(bin)] = std::max(1.0e-9f, std::abs(spectrum[static_cast<std::size_t>(bin)]));
            phase[static_cast<std::size_t>(bin)] = std::arg(spectrum[static_cast<std::size_t>(bin)]);
        }

        // Keep the smoothing width in Hz.  The main JS/Rust renderer uses a
        // compact 180 Hz log envelope: a much wider envelope blurred nearby
        // partials together and produced the hollow/echoed native result.
        const auto envelopeRadius = juce::jlimit(4, 18,
            static_cast<int>(std::round(180.0 * static_cast<double>(fftSize) / sampleRate)));
        prefix[0] = 0.0f;
        for (int bin = 0; bin <= half; ++bin)
            prefix[static_cast<std::size_t>(bin + 1)] = prefix[static_cast<std::size_t>(bin)]
                + std::log(magnitude[static_cast<std::size_t>(bin)]);
        for (int bin = 0; bin <= half; ++bin)
        {
            const auto begin = std::max(0, bin - envelopeRadius);
            const auto end = std::min(half + 1, bin + envelopeRadius + 1);
            logEnvelope[static_cast<std::size_t>(bin)] =
                (prefix[static_cast<std::size_t>(end)] - prefix[static_cast<std::size_t>(begin)])
                / static_cast<float>(std::max(1, end - begin));
        }
        for (int bin = 0; bin <= half; ++bin)
        {
            const auto frequency = static_cast<float>(bin) * static_cast<float>(sampleRate)
                / static_cast<float>(fftSize);
            const auto highBand = juce::jlimit(0.0f, 1.0f, (frequency - 3'200.0f) / 4'800.0f);
            const auto residualFloorRatio = 0.20f + 0.42f * highBand * highBand;
            const auto envelope = std::max(1.0e-9f, std::exp(logEnvelope[static_cast<std::size_t>(bin)]));
            const auto totalPower = magnitude[static_cast<std::size_t>(bin)] * magnitude[static_cast<std::size_t>(bin)];
            const auto residualPower = std::min(totalPower,
                envelope * envelope * residualFloorRatio * residualFloorRatio);
            residualMagnitude[static_cast<std::size_t>(bin)] = std::sqrt(residualPower);
            harmonicMagnitude[static_cast<std::size_t>(bin)] = std::sqrt(std::max(0.0f, totalPower - residualPower));
        }

        const auto absoluteSeconds = static_cast<double>(std::min(centre, inputLength)) / sampleRate;
        const auto curvePosition = absoluteSeconds * 1000.0 / framePeriod;
        const auto source = curveAt(sourceMidi, curvePosition);
        const auto target = curveAt(targetMidi, curvePosition);
        const auto semitones = source && target ? juce::jlimit(-24.0f, 24.0f, *target - *source) : 0.0f;
        const auto ratio = juce::jlimit(0.25f, 4.0f, std::pow(2.0f, semitones / 12.0f));
        const auto energy = std::max(1.0e-9f,
            std::accumulate(magnitude.begin(), magnitude.end(), 0.0f));
        auto positiveFlux = 0.0f;
        for (int bin = 0; bin <= half; ++bin)
            positiveFlux += std::max(0.0f, magnitude[static_cast<std::size_t>(bin)]
                                           - previousMagnitude[static_cast<std::size_t>(bin)]);
        const auto transient = frame == 0 || positiveFlux / energy > 0.34f;
        std::fill(shifted.begin(), shifted.end(), Complex {});
        std::fill(mappedMagnitude.begin(), mappedMagnitude.end(), 0.0f);

        for (int outputBin = 0; outputBin <= half; ++outputBin)
        {
            const auto sourceBinPosition = static_cast<float>(outputBin) / ratio;
            if (sourceBinPosition > static_cast<float>(half)) continue;
            const auto sourceLeft = static_cast<int>(std::floor(sourceBinPosition));
            const auto sourceRight = std::min(half, sourceLeft + 1);
            const auto fraction = sourceBinPosition - static_cast<float>(sourceLeft);
            const auto interpolate = [fraction](float left, float right) { return left + (right - left) * fraction; };
            const auto sourceHarmonic = interpolate(harmonicMagnitude[static_cast<std::size_t>(sourceLeft)],
                                                    harmonicMagnitude[static_cast<std::size_t>(sourceRight)]);
            const auto sourceEnvelope = interpolate(logEnvelope[static_cast<std::size_t>(sourceLeft)],
                                                     logEnvelope[static_cast<std::size_t>(sourceRight)]);
            const auto targetEnvelope = logEnvelope[static_cast<std::size_t>(outputBin)];
            mappedMagnitude[static_cast<std::size_t>(outputBin)] = sourceHarmonic
                * juce::jlimit(0.18f, 5.5f, std::exp(targetEnvelope - sourceEnvelope));
            const auto sourcePhase = phase[static_cast<std::size_t>(sourceLeft)]
                + wrapPhase(phase[static_cast<std::size_t>(sourceRight)]
                            - phase[static_cast<std::size_t>(sourceLeft)]) * fraction;
            const auto previous = previousPhase[static_cast<std::size_t>(sourceLeft)]
                + wrapPhase(previousPhase[static_cast<std::size_t>(sourceRight)]
                            - previousPhase[static_cast<std::size_t>(sourceLeft)]) * fraction;
            const auto expected = expectedScale * sourceBinPosition;
            const auto instantaneous = (expected + wrapPhase(sourcePhase - previous - expected)) * ratio;
            synthesisPhase[static_cast<std::size_t>(outputBin)] = transient
                ? sourcePhase
                : wrapPhase(synthesisPhase[static_cast<std::size_t>(outputBin)] + instantaneous);
        }

        if (transient)
        {
            std::copy_n(spectrum.begin(), half + 1, shifted.begin());
            smoothedFrameGain = 1.0f;
        }
        else
        {
            for (int bin = 0; bin <= half; ++bin)
            {
                const auto tonal = std::polar(mappedMagnitude[static_cast<std::size_t>(bin)],
                                              synthesisPhase[static_cast<std::size_t>(bin)]);
                const auto residualScale = residualMagnitude[static_cast<std::size_t>(bin)]
                    / std::max(1.0e-9f, magnitude[static_cast<std::size_t>(bin)]);
                shifted[static_cast<std::size_t>(bin)] = tonal
                    + spectrum[static_cast<std::size_t>(bin)] * residualScale;
            }
            auto sourcePower = 0.0f;
            auto renderedPower = 0.0f;
            for (int bin = 0; bin <= half; ++bin)
            {
                sourcePower += magnitude[static_cast<std::size_t>(bin)] * magnitude[static_cast<std::size_t>(bin)];
                renderedPower += std::norm(shifted[static_cast<std::size_t>(bin)]);
            }
            const auto frameGain = sourcePower > 1.0e-12f && renderedPower > 1.0e-12f
                ? juce::jlimit(0.55f, 1.8f, std::sqrt(sourcePower / renderedPower)) : 1.0f;
            smoothedFrameGain = smoothedFrameGain * 0.72f + frameGain * 0.28f;
            for (int bin = 0; bin <= half; ++bin)
                shifted[static_cast<std::size_t>(bin)] *= smoothedFrameGain;
        }
        for (int bin = 1; bin < half; ++bin)
            shifted[static_cast<std::size_t>(fftSize - bin)] = std::conj(shifted[static_cast<std::size_t>(bin)]);
        fft.perform(shifted.data(), inverse.data(), true);
        for (int index = 0; index < fftSize; ++index)
        {
            const auto destination = centre + index;
            if (destination >= static_cast<int>(output.size())) break;
            const auto weight = window[static_cast<std::size_t>(index)];
            output[static_cast<std::size_t>(destination)] += inverse[static_cast<std::size_t>(index)].real()
                * weight;
            normalisation[static_cast<std::size_t>(destination)] += weight * weight;
        }
        previousPhase = phase;
        previousMagnitude = magnitude;
    }

    std::vector<float> cropped(static_cast<std::size_t>(inputLength));
    for (int index = 0; index < inputLength; ++index)
    {
        const auto sourceIndex = index + pad;
        const auto norm = normalisation[static_cast<std::size_t>(sourceIndex)];
        const auto wet = norm > 1.0e-6f ? output[static_cast<std::size_t>(sourceIndex)] / norm : input[index];
        const auto curvePosition = static_cast<double>(index) / sampleRate * 1000.0 / framePeriod;
        const auto source = curveAt(sourceMidi, curvePosition);
        const auto target = curveAt(targetMidi, curvePosition);
        const auto shift = source && target ? std::abs(*target - *source) : 0.0f;
        const auto blend = juce::jlimit(0.0f, 1.0f, shift / 0.20f);
        cropped[static_cast<std::size_t>(index)] = input[index] + (wet - input[index]) * blend;
    }
    return cropped;
}
}
