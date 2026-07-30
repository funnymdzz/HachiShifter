#include "WorldRenderer.h"

#include <world/cheaptrick.h>
#include <world/d4c.h>
#include <world/harvest.h>
#include <world/synthesis.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hachi::backend
{
namespace
{
double curveAt(const std::vector<float>& curve, double position, double fallback)
{
    if (curve.empty() || !std::isfinite(position) || position < 0.0) return fallback;
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left >= curve.size()) return fallback;
    const auto right = std::min(left + 1, curve.size() - 1);
    const auto amount = position - static_cast<double>(left);
    const auto a = static_cast<double>(curve[left]);
    const auto b = static_cast<double>(curve[right]);
    return std::isfinite(a) && std::isfinite(b) ? a + (b - a) * amount : fallback;
}

double sourceTimeAt(const std::vector<WorldTimeMapPoint>& map,
                    double targetSeconds, double sourceDuration,
                    double targetDuration)
{
    if (map.size() < 2)
        return targetDuration > 0.0
            ? std::clamp(targetSeconds / targetDuration * sourceDuration, 0.0, sourceDuration)
            : 0.0;
    const auto right = std::upper_bound(map.begin(), map.end(), targetSeconds,
        [](double time, const WorldTimeMapPoint& point) { return time < point.targetSeconds; });
    if (right == map.begin()) return std::clamp(right->sourceSeconds, 0.0, sourceDuration);
    if (right == map.end()) return std::clamp(map.back().sourceSeconds, 0.0, sourceDuration);
    const auto& a = *(right - 1);
    const auto& b = *right;
    const auto amount = b.targetSeconds > a.targetSeconds
        ? std::clamp((targetSeconds - a.targetSeconds)
                     / (b.targetSeconds - a.targetSeconds), 0.0, 1.0) : 0.0;
    return std::clamp(a.sourceSeconds + (b.sourceSeconds - a.sourceSeconds) * amount,
                      0.0, sourceDuration);
}

double matrixAt(const std::vector<std::vector<double>>& matrix,
                double framePosition, double binPosition, bool logarithmic)
{
    if (matrix.empty() || matrix.front().empty()) return logarithmic ? 1.0e-12 : 1.0;
    framePosition = std::clamp(framePosition, 0.0, static_cast<double>(matrix.size() - 1));
    binPosition = std::clamp(binPosition, 0.0, static_cast<double>(matrix.front().size() - 1));
    const auto f0 = static_cast<std::size_t>(std::floor(framePosition));
    const auto f1 = std::min(f0 + 1, matrix.size() - 1);
    const auto b0 = static_cast<std::size_t>(std::floor(binPosition));
    const auto b1 = std::min(b0 + 1, matrix.front().size() - 1);
    const auto ft = framePosition - static_cast<double>(f0);
    const auto bt = binPosition - static_cast<double>(b0);
    const auto transform = [logarithmic](double value)
    {
        return logarithmic ? std::log(std::max(1.0e-12, value)) : value;
    };
    const auto a = transform(matrix[f0][b0])
        + (transform(matrix[f0][b1]) - transform(matrix[f0][b0])) * bt;
    const auto b = transform(matrix[f1][b0])
        + (transform(matrix[f1][b1]) - transform(matrix[f1][b0])) * bt;
    const auto value = a + (b - a) * ft;
    return logarithmic ? std::exp(value) : value;
}

bool hasPitchDifference(const std::vector<float>& sourceMidi,
                        const std::vector<float>& targetMidi)
{
    const auto count = std::min(sourceMidi.size(), targetMidi.size());
    for (std::size_t index = 0; index < count; ++index)
        if (sourceMidi[index] > 0.0f && targetMidi[index] > 0.0f
            && std::abs(sourceMidi[index] - targetMidi[index]) > 1.0e-4f)
            return true;
    return false;
}
}

juce::AudioBuffer<float> WorldRenderer::render(
    const juce::AudioBuffer<float>& source,
    int targetSamples,
    double sampleRate,
    double framePeriodMs,
    const std::vector<float>& sourceMidi,
    const std::vector<float>& targetMidi,
    const std::vector<float>& formantSemitones,
    const std::vector<WorldTimeMapPoint>& timeMap)
{
    const auto sourceSamples = source.getNumSamples();
    const auto channels = source.getNumChannels();
    if (sourceSamples <= 0 || targetSamples <= 0 || channels <= 0 || sampleRate <= 0.0)
        return {};

    auto timeChanged = sourceSamples != targetSamples;
    const auto sourceDuration = static_cast<double>(sourceSamples) / sampleRate;
    const auto targetDuration = static_cast<double>(targetSamples) / sampleRate;
    for (const auto& point : timeMap)
        timeChanged = timeChanged || std::abs(
            point.sourceSeconds / std::max(1.0e-9, sourceDuration)
            - point.targetSeconds / std::max(1.0e-9, targetDuration)) > 1.0e-6;
    const auto formantChanged = std::any_of(formantSemitones.begin(), formantSemitones.end(),
        [](float value) { return std::abs(value) > 1.0e-4f; });
    if (!timeChanged && !formantChanged && !hasPitchDifference(sourceMidi, targetMidi))
        return source;

    const auto fs = std::max(8'000, static_cast<int>(std::llround(sampleRate)));
    const auto period = std::clamp(framePeriodMs, 1.0, 20.0);
    HarvestOption harvestOption;
    InitializeHarvestOption(&harvestOption);
    harvestOption.frame_period = period;
    harvestOption.f0_floor = 40.0;
    harvestOption.f0_ceil = std::min(1'600.0, sampleRate * 0.45);
    const auto sourceFrames = GetSamplesForHarvest(fs, sourceSamples, period);
    const auto targetFrames = GetSamplesForHarvest(fs, targetSamples, period);
    if (sourceFrames <= 0 || targetFrames <= 0) return {};

    std::vector<double> analysisMono(static_cast<std::size_t>(sourceSamples), 0.0);
    for (int sample = 0; sample < sourceSamples; ++sample)
    {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel)
            sum += source.getSample(channel, sample);
        analysisMono[static_cast<std::size_t>(sample)] = sum / channels;
    }
    std::vector<double> temporalPositions(static_cast<std::size_t>(sourceFrames), 0.0);
    std::vector<double> analysedF0(static_cast<std::size_t>(sourceFrames), 0.0);
    Harvest(analysisMono.data(), sourceSamples, fs, &harvestOption,
            temporalPositions.data(), analysedF0.data());

    CheapTrickOption cheapTrickOption;
    InitializeCheapTrickOption(fs, &cheapTrickOption);
    cheapTrickOption.f0_floor = harvestOption.f0_floor;
    cheapTrickOption.fft_size = GetFFTSizeForCheapTrick(fs, &cheapTrickOption);
    const auto bins = cheapTrickOption.fft_size / 2 + 1;
    D4COption d4cOption;
    InitializeD4COption(&d4cOption);

    std::vector<double> targetF0(static_cast<std::size_t>(targetFrames), 0.0);
    std::vector<double> sourceFramePositions(static_cast<std::size_t>(targetFrames), 0.0);
    for (int frame = 0; frame < targetFrames; ++frame)
    {
        const auto targetTime = std::min(targetDuration, frame * period / 1000.0);
        const auto sourceTime = sourceTimeAt(timeMap, targetTime, sourceDuration, targetDuration);
        const auto sourceFrame = std::clamp(sourceTime * 1000.0 / period,
                                             0.0, static_cast<double>(sourceFrames - 1));
        sourceFramePositions[static_cast<std::size_t>(frame)] = sourceFrame;
        const auto left = static_cast<std::size_t>(std::floor(sourceFrame));
        const auto right = std::min(left + 1, analysedF0.size() - 1);
        const auto amount = sourceFrame - static_cast<double>(left);
        const auto sourceF0 = analysedF0[left] + (analysedF0[right] - analysedF0[left]) * amount;
        const auto desiredMidi = curveAt(targetMidi, targetTime * 1000.0 / period, 0.0);
        // Keep consonants, breaths and sibilants unvoiced even when a note blob
        // spans them.  Voiced frames follow the imported/edited contour exactly.
        if (sourceF0 > 0.0 && desiredMidi > 0.0)
            targetF0[static_cast<std::size_t>(frame)] =
                440.0 * std::pow(2.0, (desiredMidi - 69.0) / 12.0);
        else
            targetF0[static_cast<std::size_t>(frame)] = sourceF0 > 0.0 ? sourceF0 : 0.0;
    }

    juce::AudioBuffer<float> output(channels, targetSamples);
    output.clear();
    for (int channel = 0; channel < channels; ++channel)
    {
        std::vector<double> input(static_cast<std::size_t>(sourceSamples));
        for (int sample = 0; sample < sourceSamples; ++sample)
            input[static_cast<std::size_t>(sample)] = source.getSample(channel, sample);

        std::vector<std::vector<double>> spectrum(static_cast<std::size_t>(sourceFrames),
                                                   std::vector<double>(static_cast<std::size_t>(bins)));
        std::vector<std::vector<double>> aperiodicity(static_cast<std::size_t>(sourceFrames),
                                                       std::vector<double>(static_cast<std::size_t>(bins)));
        std::vector<double*> spectrumPointers(static_cast<std::size_t>(sourceFrames));
        std::vector<double*> aperiodicityPointers(static_cast<std::size_t>(sourceFrames));
        for (int frame = 0; frame < sourceFrames; ++frame)
        {
            spectrumPointers[static_cast<std::size_t>(frame)] = spectrum[static_cast<std::size_t>(frame)].data();
            aperiodicityPointers[static_cast<std::size_t>(frame)] = aperiodicity[static_cast<std::size_t>(frame)].data();
        }
        CheapTrick(input.data(), sourceSamples, fs, temporalPositions.data(), analysedF0.data(),
                   sourceFrames, &cheapTrickOption, spectrumPointers.data());
        D4C(input.data(), sourceSamples, fs, temporalPositions.data(), analysedF0.data(),
            sourceFrames, cheapTrickOption.fft_size, &d4cOption, aperiodicityPointers.data());

        std::vector<std::vector<double>> warpedSpectrum(static_cast<std::size_t>(targetFrames),
                                                         std::vector<double>(static_cast<std::size_t>(bins)));
        std::vector<std::vector<double>> warpedAperiodicity(static_cast<std::size_t>(targetFrames),
                                                             std::vector<double>(static_cast<std::size_t>(bins)));
        std::vector<const double*> warpedSpectrumPointers(static_cast<std::size_t>(targetFrames));
        std::vector<const double*> warpedAperiodicityPointers(static_cast<std::size_t>(targetFrames));
        for (int frame = 0; frame < targetFrames; ++frame)
        {
            const auto targetTime = std::min(targetDuration, frame * period / 1000.0);
            const auto formant = curveAt(formantSemitones, targetTime * 1000.0 / period, 0.0);
            const auto formantRatio = std::pow(2.0, formant / 12.0);
            for (int bin = 0; bin < bins; ++bin)
            {
                warpedSpectrum[static_cast<std::size_t>(frame)][static_cast<std::size_t>(bin)] =
                    matrixAt(spectrum, sourceFramePositions[static_cast<std::size_t>(frame)],
                             static_cast<double>(bin) / formantRatio, true);
                warpedAperiodicity[static_cast<std::size_t>(frame)][static_cast<std::size_t>(bin)] =
                    std::clamp(matrixAt(aperiodicity,
                                        sourceFramePositions[static_cast<std::size_t>(frame)],
                                        static_cast<double>(bin), false), 0.001, 0.999999999999);
            }
            warpedSpectrumPointers[static_cast<std::size_t>(frame)] =
                warpedSpectrum[static_cast<std::size_t>(frame)].data();
            warpedAperiodicityPointers[static_cast<std::size_t>(frame)] =
                warpedAperiodicity[static_cast<std::size_t>(frame)].data();
        }

        std::vector<double> synthesized(static_cast<std::size_t>(targetSamples), 0.0);
        Synthesis(targetF0.data(), targetFrames, warpedSpectrumPointers.data(),
                  warpedAperiodicityPointers.data(), cheapTrickOption.fft_size,
                  period, fs, targetSamples, synthesized.data());
        auto* destination = output.getWritePointer(channel);
        for (int sample = 0; sample < targetSamples; ++sample)
        {
            const auto value = synthesized[static_cast<std::size_t>(sample)];
            destination[sample] = std::isfinite(value) ? static_cast<float>(value) : 0.0f;
        }
    }
    return output;
}
}
