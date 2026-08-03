#include "Llsm2Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

extern "C"
{
#include <llsm.h>
}

namespace hachi::backend
{
namespace
{
using ChunkPtr = std::unique_ptr<llsm_chunk, decltype(&llsm_delete_chunk)>;
using AnalysisOptionsPtr = std::unique_ptr<llsm_aoptions, decltype(&llsm_delete_aoptions)>;
using SynthesisOptionsPtr = std::unique_ptr<llsm_soptions, decltype(&llsm_delete_soptions)>;
using OutputPtr = std::unique_ptr<llsm_output, decltype(&llsm_delete_output)>;

float curveAt(const std::vector<float>& curve, double frame, float fallback = 0.0f)
{
    if (curve.empty() || !std::isfinite(frame) || frame < 0.0) return fallback;
    const auto left = static_cast<std::size_t>(std::floor(frame));
    if (left >= curve.size()) return fallback;
    const auto right = std::min(left + 1, curve.size() - 1);
    const auto amount = static_cast<float>(frame - static_cast<double>(left));
    const auto a = curve[left];
    const auto b = curve[right];
    return std::isfinite(a) && std::isfinite(b) ? a + (b - a) * amount : fallback;
}

float midiToHz(float midi)
{
    return midi > 0.0f && std::isfinite(midi)
        ? 440.0f * std::exp2((midi - 69.0f) / 12.0f) : 0.0f;
}

double sourceTimeAt(const std::vector<TimeMapPoint>& map, double targetSeconds,
                    double sourceDuration, double targetDuration)
{
    if (map.size() < 2)
        return targetDuration > 1.0e-9
            ? juce::jlimit(0.0, sourceDuration,
                           targetSeconds / targetDuration * sourceDuration)
            : 0.0;
    auto right = std::lower_bound(map.begin(), map.end(), targetSeconds,
        [](const TimeMapPoint& point, double time)
        {
            return point.targetSeconds < time;
        });
    if (right == map.begin()) return juce::jlimit(0.0, sourceDuration, right->sourceSeconds);
    if (right == map.end()) return juce::jlimit(0.0, sourceDuration, map.back().sourceSeconds);
    const auto& left = *(right - 1);
    const auto span = right->targetSeconds - left.targetSeconds;
    const auto amount = span > 1.0e-9
        ? juce::jlimit(0.0, 1.0, (targetSeconds - left.targetSeconds) / span) : 0.0;
    return juce::jlimit(0.0, sourceDuration,
        left.sourceSeconds + (right->sourceSeconds - left.sourceSeconds) * amount);
}

double targetTimeAtSource(const std::vector<TimeMapPoint>& map, double sourceSeconds,
                          double sourceDuration, double targetDuration)
{
    if (map.size() < 2)
        return sourceDuration > 1.0e-9
            ? juce::jlimit(0.0, targetDuration,
                           sourceSeconds / sourceDuration * targetDuration)
            : 0.0;
    auto right = std::lower_bound(map.begin(), map.end(), sourceSeconds,
        [](const TimeMapPoint& point, double time)
        {
            return point.sourceSeconds < time;
        });
    if (right == map.begin()) return juce::jlimit(0.0, targetDuration, right->targetSeconds);
    if (right == map.end()) return juce::jlimit(0.0, targetDuration, map.back().targetSeconds);
    const auto& left = *(right - 1);
    const auto span = right->sourceSeconds - left.sourceSeconds;
    const auto amount = span > 1.0e-9
        ? juce::jlimit(0.0, 1.0, (sourceSeconds - left.sourceSeconds) / span) : 0.0;
    return juce::jlimit(0.0, targetDuration,
        left.targetSeconds + (right->targetSeconds - left.targetSeconds) * amount);
}

float circularInterpolate(float a, float b, float amount)
{
    const auto x = std::cos(a) + (std::cos(b) - std::cos(a)) * amount;
    const auto y = std::sin(a) + (std::sin(b) - std::sin(a)) * amount;
    return std::atan2(y, x);
}

void interpolateNoise(llsm_nmframe* destination, const llsm_nmframe* source, float amount)
{
    if (destination == nullptr || source == nullptr) return;
    const auto psdCount = std::min(destination->npsd, source->npsd);
    for (int index = 0; index < psdCount; ++index)
        destination->psd[index] += (source->psd[index] - destination->psd[index]) * amount;
    const auto channels = std::min(destination->nchannel, source->nchannel);
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destinationEnvelope = destination->eenv[channel];
        const auto* sourceEnvelope = source->eenv[channel];
        if (destinationEnvelope == nullptr || sourceEnvelope == nullptr) continue;
        destination->edc[channel] += (source->edc[channel] - destination->edc[channel]) * amount;
        const auto common = std::min(destinationEnvelope->nhar, sourceEnvelope->nhar);
        for (int harmonic = 0; harmonic < common; ++harmonic)
        {
            destinationEnvelope->ampl[harmonic] +=
                (sourceEnvelope->ampl[harmonic] - destinationEnvelope->ampl[harmonic]) * amount;
            destinationEnvelope->phse[harmonic] = circularInterpolate(
                destinationEnvelope->phse[harmonic], sourceEnvelope->phse[harmonic], amount);
        }
    }
}

void interpolateLayer1(llsm_container* destination, const llsm_container* source, float amount)
{
    if (destination == nullptr || source == nullptr || amount <= 0.0f) return;
    auto* destinationF0 = static_cast<float*>(llsm_container_get(destination, LLSM_FRAME_F0));
    const auto* sourceF0 = static_cast<const float*>(
        llsm_container_get(const_cast<llsm_container*>(source), LLSM_FRAME_F0));
    auto* destinationRd = static_cast<float*>(llsm_container_get(destination, LLSM_FRAME_RD));
    const auto* sourceRd = static_cast<const float*>(
        llsm_container_get(const_cast<llsm_container*>(source), LLSM_FRAME_RD));
    auto* destinationPhase = static_cast<float*>(llsm_container_get(destination, LLSM_FRAME_VSPHSE));
    const auto* sourcePhase = static_cast<const float*>(
        llsm_container_get(const_cast<llsm_container*>(source), LLSM_FRAME_VSPHSE));
    auto* destinationEnvelope = static_cast<float*>(llsm_container_get(destination, LLSM_FRAME_VTMAGN));
    const auto* sourceEnvelope = static_cast<const float*>(
        llsm_container_get(const_cast<llsm_container*>(source), LLSM_FRAME_VTMAGN));

    if (destinationF0 != nullptr && sourceF0 != nullptr)
    {
        if (*destinationF0 > 0.0f && *sourceF0 > 0.0f)
            *destinationF0 += (*sourceF0 - *destinationF0) * amount;
        else if (*sourceF0 > 0.0f && amount >= 0.5f)
            *destinationF0 = *sourceF0;
        else if (*destinationF0 > 0.0f && amount > 0.5f)
            *destinationF0 = 0.0f;
    }
    if (destinationRd != nullptr && sourceRd != nullptr)
        *destinationRd += (*sourceRd - *destinationRd) * amount;
    if (destinationPhase != nullptr && sourcePhase != nullptr)
    {
        const auto count = std::min(llsm_fparray_length(destinationPhase),
                                    llsm_fparray_length(const_cast<float*>(sourcePhase)));
        for (int index = 0; index < count; ++index)
            destinationPhase[index] = circularInterpolate(destinationPhase[index],
                                                          sourcePhase[index], amount);
    }
    if (destinationEnvelope != nullptr && sourceEnvelope != nullptr)
    {
        const auto count = std::min(llsm_fparray_length(destinationEnvelope),
                                    llsm_fparray_length(const_cast<float*>(sourceEnvelope)));
        for (int index = 0; index < count; ++index)
            destinationEnvelope[index] += (sourceEnvelope[index] - destinationEnvelope[index]) * amount;
    }
    interpolateNoise(static_cast<llsm_nmframe*>(llsm_container_get(destination, LLSM_FRAME_NM)),
        static_cast<const llsm_nmframe*>(llsm_container_get(
            const_cast<llsm_container*>(source), LLSM_FRAME_NM)), amount);
}

void shiftVocalTract(float* envelope, float semitones)
{
    if (envelope == nullptr || std::abs(semitones) < 1.0e-4f) return;
    const auto count = llsm_fparray_length(envelope);
    if (count < 2) return;
    std::vector<float> original(envelope, envelope + count);
    const auto ratio = std::exp2(semitones / 12.0f);
    for (int index = 0; index < count; ++index)
    {
        const auto sourceIndex = static_cast<float>(index) / ratio;
        const auto left = juce::jlimit(0, count - 1, static_cast<int>(std::floor(sourceIndex)));
        const auto right = std::min(count - 1, left + 1);
        const auto amount = juce::jlimit(0.0f, 1.0f, sourceIndex - static_cast<float>(left));
        envelope[index] = original[static_cast<std::size_t>(left)]
            + (original[static_cast<std::size_t>(right)] - original[static_cast<std::size_t>(left)])
                * amount;
    }
}

void applySpectralTension(float* envelope, float tension)
{
    if (envelope == nullptr || std::abs(tension) < 1.0e-4f) return;
    const auto count = llsm_fparray_length(envelope);
    const auto amount = juce::jlimit(-1.0f, 1.0f, tension);
    for (int index = 1; index < count; ++index)
    {
        const auto normalized = static_cast<float>(index) / static_cast<float>(count - 1);
        envelope[index] += amount * (normalized - 0.25f) * 5.0f;
    }
}
}

juce::AudioBuffer<float> Llsm2Renderer::render(
    const juce::AudioBuffer<float>& source, int targetSamples, double sampleRate,
    double framePeriodMs, const std::vector<float>& sourceMidi,
    const std::vector<float>& targetMidi, const std::vector<float>& formantSemitones,
    const std::vector<float>& tension, const std::vector<TimeMapPoint>& timeMap)
{
    juce::AudioBuffer<float> empty;
    if (source.getNumSamples() < 8 || targetSamples < 1 || sampleRate <= 0.0)
        return empty;

    const auto hopSeconds = std::max(0.001, framePeriodMs / 1000.0);
    const auto sourceDuration = static_cast<double>(source.getNumSamples()) / sampleRate;
    const auto targetDuration = static_cast<double>(targetSamples) / sampleRate;
    const auto sourceFrames = std::max(2, static_cast<int>(std::ceil(sourceDuration / hopSeconds)) + 1);
    const auto targetFrames = std::max(2, static_cast<int>(std::ceil(targetDuration / hopSeconds)) + 1);

    std::vector<float> mono(static_cast<std::size_t>(source.getNumSamples()), 0.0f);
    for (int sample = 0; sample < source.getNumSamples(); ++sample)
    {
        double sum = 0.0;
        for (int channel = 0; channel < source.getNumChannels(); ++channel)
            sum += source.getSample(channel, sample);
        mono[static_cast<std::size_t>(sample)] = static_cast<float>(
            sum / static_cast<double>(std::max(1, source.getNumChannels())));
    }

    std::vector<float> analysisF0(static_cast<std::size_t>(sourceFrames), 0.0f);
    for (int frame = 0; frame < sourceFrames; ++frame)
    {
        const auto sourceTime = std::min(sourceDuration, frame * hopSeconds);
        const auto targetTime = targetTimeAtSource(timeMap, sourceTime, sourceDuration, targetDuration);
        analysisF0[static_cast<std::size_t>(frame)] = midiToHz(
            curveAt(sourceMidi, targetTime / hopSeconds));
    }

    AnalysisOptionsPtr analysis(llsm_create_aoptions(), llsm_delete_aoptions);
    SynthesisOptionsPtr synthesis(llsm_create_soptions(static_cast<float>(sampleRate)),
                                  llsm_delete_soptions);
    if (analysis == nullptr || synthesis == nullptr) return empty;
    analysis->thop = static_cast<float>(hopSeconds);
    analysis->f0_refine = 0;
    analysis->hm_method = LLSM_AOPTION_HMCZT;
    ChunkPtr analysed(llsm_analyze(analysis.get(), mono.data(), source.getNumSamples(),
                                   static_cast<float>(sampleRate), analysisF0.data(),
                                   sourceFrames, nullptr), llsm_delete_chunk);
    if (analysed == nullptr) return empty;
    llsm_chunk_tolayer1(analysed.get(), 2048);
    llsm_chunk_phasepropagate(analysed.get(), -1);

    auto* targetConfiguration = llsm_copy_container(analysed->conf);
    if (targetConfiguration == nullptr) return empty;
    if (auto* frameCount = static_cast<int*>(llsm_container_get(targetConfiguration, LLSM_CONF_NFRM)))
        *frameCount = targetFrames;
    ChunkPtr transformed(llsm_create_chunk(targetConfiguration, 0), llsm_delete_chunk);
    llsm_delete_container(targetConfiguration);
    if (transformed == nullptr) return empty;

    for (int frame = 0; frame < targetFrames; ++frame)
    {
        const auto targetTime = std::min(targetDuration, frame * hopSeconds);
        const auto sourceTime = sourceTimeAt(timeMap, targetTime, sourceDuration, targetDuration);
        const auto mapped = juce::jlimit(0.0, static_cast<double>(sourceFrames - 1),
                                         sourceTime / hopSeconds);
        const auto left = juce::jlimit(0, sourceFrames - 1, static_cast<int>(std::floor(mapped)));
        const auto right = std::min(sourceFrames - 1, left + 1);
        transformed->frames[frame] = llsm_copy_container(analysed->frames[left]);
        if (transformed->frames[frame] == nullptr) return empty;
        interpolateLayer1(transformed->frames[frame], analysed->frames[right],
                          static_cast<float>(mapped - left));

        auto* f0 = static_cast<float*>(llsm_container_get(transformed->frames[frame], LLSM_FRAME_F0));
        const auto oldF0 = f0 != nullptr ? *f0 : 0.0f;
        const auto targetF0 = midiToHz(curveAt(targetMidi, targetTime / hopSeconds));
        if (f0 != nullptr)
            *f0 = oldF0 > 0.0f && targetF0 > 0.0f ? std::max(20.0f, targetF0) : 0.0f;
        llsm_container_attach(transformed->frames[frame], LLSM_FRAME_HM, nullptr, nullptr, nullptr);

        auto* envelope = static_cast<float*>(
            llsm_container_get(transformed->frames[frame], LLSM_FRAME_VTMAGN));
        if (envelope != nullptr && oldF0 > 0.0f && targetF0 > 0.0f)
        {
            const auto compensation = 20.0f * std::log10(targetF0 / oldF0);
            const auto count = llsm_fparray_length(envelope);
            for (int bin = 0; bin < count; ++bin) envelope[bin] -= compensation;
        }
        shiftVocalTract(envelope, curveAt(formantSemitones, targetTime / hopSeconds));
        applySpectralTension(envelope, curveAt(tension, targetTime / hopSeconds));
    }

    llsm_chunk_tolayer0(transformed.get());
    llsm_chunk_phasepropagate(transformed.get(), 1);
    OutputPtr output(llsm_synthesize(synthesis.get(), transformed.get()), llsm_delete_output);
    if (output == nullptr || output->y == nullptr || output->ny < 1) return empty;

    juce::AudioBuffer<float> rendered(std::max(1, source.getNumChannels()), targetSamples);
    rendered.clear();
    const auto copySamples = std::min(targetSamples, output->ny);
    for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
        std::copy_n(output->y, copySamples, rendered.getWritePointer(channel));
    return rendered;
}
}
