#include "NsfHifiganRenderer.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
#include <onnxruntime_cxx_api.h>
#endif

namespace hachi::backend
{
#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
namespace
{
struct Config
{
    int sampleRate = 44'100;
    int melBands = 128;
    int hop = 512;
    int fftSize = 2'048;
    int windowSize = 2'048;
    float minimumHz = 40.0f;
    float maximumHz = 16'000.0f;
};

struct ModelFiles
{
    juce::File onnx;
    juce::File config;
};

float curveAt(const std::vector<float>& curve, double position)
{
    if (curve.empty() || !std::isfinite(position) || position < 0.0) return 0.0f;
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left >= curve.size()) return 0.0f;
    const auto right = std::min(left + 1, curve.size() - 1);
    const auto amount = static_cast<float>(position - static_cast<double>(left));
    const auto a = curve[left];
    const auto b = curve[right];
    if (!(std::isfinite(a) && std::isfinite(b))) return 0.0f;
    const auto aVoiced = a > 0.0f;
    const auto bVoiced = b > 0.0f;
    // Zero is a voicing boundary, not MIDI 0.  Linear interpolation across
    // that boundary creates a brief false sub-bass F0 at note endings.
    if (aVoiced != bVoiced) return amount < 0.5f ? a : b;
    return aVoiced ? a + (b - a) * amount : 0.0f;
}

float scalarCurveAt(const std::vector<float>& curve, double position, float fallback = 0.0f)
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

float midiToHz(float midi)
{
    return std::isfinite(midi) && midi > 0.0f
        ? 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f) : 0.0f;
}

std::optional<ModelFiles> modelFiles(const juce::File& configured)
{
    const auto validate = [](juce::File directory) -> std::optional<ModelFiles>
    {
        if (directory.existsAsFile()) directory = directory.getParentDirectory();
        const auto onnx = directory.getChildFile("pc_nsf_hifigan.onnx");
        const auto config = directory.getChildFile("config.json");
        if (onnx.existsAsFile() && config.existsAsFile()) return ModelFiles { onnx, config };
        return std::nullopt;
    };
    if (configured != juce::File{})
        if (auto files = validate(configured)) return files;
    if (const auto value = juce::SystemStats::getEnvironmentVariable(
            "HACHISHIFTER_NSF_HIFIGAN_MODEL_DIR", {}); value.isNotEmpty())
        if (auto files = validate(juce::File(value))) return files;
    if (const auto value = juce::SystemStats::getEnvironmentVariable(
            "HACHISHIFTER_NSF_HIFIGAN_ONNX", {}); value.isNotEmpty())
    {
        const juce::File onnx(value);
        auto config = onnx.getSiblingFile("config.json");
        const auto configuredConfig = juce::SystemStats::getEnvironmentVariable(
            "HACHISHIFTER_NSF_HIFIGAN_CONFIG", {});
        if (configuredConfig.isNotEmpty()) config = juce::File(configuredConfig);
        if (onnx.existsAsFile() && config.existsAsFile()) return ModelFiles { onnx, config };
    }
    if (auto files = validate(juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory()
                .getChildFile("models").getChildFile("nsf_hifigan")))
        return files;
    return std::nullopt;
}

std::optional<Config> readConfig(const juce::File& file, juce::String& error)
{
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr)
    {
        error = "NSF-HiFiGAN config.json is not a JSON object";
        return std::nullopt;
    }
    Config result;
    const auto integer = [&](const char* name, int fallback)
    {
        const auto value = object->getProperty(name);
        return value.isInt() || value.isInt64() || value.isDouble()
            ? static_cast<int>(value) : fallback;
    };
    const auto number = [&](const char* name, float fallback)
    {
        const auto value = object->getProperty(name);
        return value.isInt() || value.isInt64() || value.isDouble()
            ? static_cast<float>(value) : fallback;
    };
    result.sampleRate = integer("sampling_rate", result.sampleRate);
    result.melBands = integer("num_mels", result.melBands);
    result.hop = integer("hop_size", result.hop);
    result.fftSize = integer("n_fft", result.fftSize);
    result.windowSize = integer("win_size", result.windowSize);
    result.minimumHz = number("fmin", result.minimumHz);
    result.maximumHz = number("fmax", result.maximumHz);
    const auto powerOfTwo = result.fftSize > 0
        && (result.fftSize & (result.fftSize - 1)) == 0;
    if (result.sampleRate < 8'000 || result.melBands <= 0 || result.hop <= 0
        || !powerOfTwo || result.windowSize <= 0 || result.windowSize > result.fftSize
        || result.minimumHz < 0.0f || result.maximumHz <= result.minimumHz)
    {
        error = "NSF-HiFiGAN config.json contains invalid audio parameters";
        return std::nullopt;
    }
    result.maximumHz = std::min(result.maximumHz, result.sampleRate * 0.5f);
    return result;
}

std::vector<float> resample(const float* input, int samples, int inputRate, int outputRate)
{
    if (input == nullptr || samples <= 0 || inputRate <= 0 || outputRate <= 0) return {};
    if (inputRate == outputRate) return { input, input + samples };
    const auto ratio = static_cast<double>(outputRate) / inputRate;
    const auto outputSamples = std::max(1, static_cast<int>(std::llround(samples * ratio)));
    std::vector<float> output(static_cast<std::size_t>(outputSamples));
    // Linear resampling noticeably tilts a vocal spectral envelope whenever a
    // model pack uses a rate different from the project.  That error is then
    // interpreted by the neural source/filter decoder as a smaller vocal
    // tract (the reported child-like colour).  A compact band-limited Lanczos
    // conversion keeps the Mel envelope stable without another DSP library.
    constexpr int radius = 12;
    const auto cutoff = std::min(1.0, ratio);
    const auto sinc = [](double x)
    {
        if (std::abs(x) < 1.0e-10) return 1.0;
        const auto angle = juce::MathConstants<double>::pi * x;
        return std::sin(angle) / angle;
    };
    for (int index = 0; index < outputSamples; ++index)
    {
        const auto position = static_cast<double>(index) / ratio;
        const auto centre = static_cast<int>(std::floor(position));
        auto value = 0.0;
        auto weightSum = 0.0;
        for (int tap = centre - radius + 1; tap <= centre + radius; ++tap)
        {
            const auto distance = position - static_cast<double>(tap);
            if (std::abs(distance) >= radius) continue;
            const auto weight = cutoff * sinc(distance * cutoff)
                * sinc(distance / static_cast<double>(radius));
            const auto sourceIndex = juce::jlimit(0, samples - 1, tap);
            value += static_cast<double>(input[sourceIndex]) * weight;
            weightSum += weight;
        }
        output[static_cast<std::size_t>(index)] = static_cast<float>(
            std::abs(weightSum) > 1.0e-12 ? value / weightSum : 0.0);
    }
    return output;
}

std::size_t reflectIndex(std::ptrdiff_t index, std::size_t size)
{
    if (size <= 1) return 0;
    const auto period = static_cast<std::ptrdiff_t>(2 * (size - 1));
    auto reflected = index % period;
    if (reflected < 0) reflected += period;
    return reflected < static_cast<std::ptrdiff_t>(size)
        ? static_cast<std::size_t>(reflected)
        : static_cast<std::size_t>(period - reflected);
}

float hzToMel(float hz)
{
    constexpr auto spacing = 200.0f / 3.0f;
    constexpr auto logStartHz = 1'000.0f;
    constexpr auto logStartMel = logStartHz / spacing;
    const auto logStep = std::log(6.4f) / 27.0f;
    return hz >= logStartHz ? logStartMel + std::log(hz / logStartHz) / logStep
                            : hz / spacing;
}

float melToHz(float mel)
{
    constexpr auto spacing = 200.0f / 3.0f;
    constexpr auto logStartHz = 1'000.0f;
    constexpr auto logStartMel = logStartHz / spacing;
    const auto logStep = std::log(6.4f) / 27.0f;
    return mel >= logStartMel ? logStartHz * std::exp(logStep * (mel - logStartMel))
                              : spacing * mel;
}

std::vector<float> filterbank(const Config& config)
{
    const auto frequencies = config.fftSize / 2 + 1;
    const auto melMinimum = hzToMel(config.minimumHz);
    const auto melMaximum = hzToMel(config.maximumHz);
    std::vector<float> hzPoints(static_cast<std::size_t>(config.melBands + 2));
    for (int index = 0; index < config.melBands + 2; ++index)
        hzPoints[static_cast<std::size_t>(index)] = melToHz(melMinimum
            + (melMaximum - melMinimum) * static_cast<float>(index)
                / static_cast<float>(config.melBands + 1));
    std::vector<float> weights(static_cast<std::size_t>(config.melBands * frequencies));
    for (int band = 0; band < config.melBands; ++band)
    {
        const auto left = hzPoints[static_cast<std::size_t>(band)];
        const auto centre = hzPoints[static_cast<std::size_t>(band + 1)];
        const auto right = hzPoints[static_cast<std::size_t>(band + 2)];
        const auto normalization = 2.0f / std::max(1.0e-6f, right - left);
        for (int bin = 0; bin < frequencies; ++bin)
        {
            const auto frequency = static_cast<float>(bin * config.sampleRate) / config.fftSize;
            weights[static_cast<std::size_t>(band * frequencies + bin)] = std::max(0.0f,
                std::min((frequency - left) / std::max(1.0e-6f, centre - left),
                         (right - frequency) / std::max(1.0e-6f, right - centre)))
                * normalization;
        }
    }
    return weights;
}

struct MelData
{
    std::vector<float> values;
    std::size_t frames = 0;
};

// buildVariableHopSplicedMel applies the shift-first order before returning.
void shiftMelFormants(MelData& mel, const Config& config,
                      const std::vector<float>& formantSemitones,
                      double framePeriodMs, std::size_t frameOffset = 0);

MelData buildMelWithHop(const std::vector<float>& waveform, const Config& config,
                        int analysisHop)
{
    if (waveform.empty()) return {};
    analysisHop = std::max(1, analysisHop);
    auto leftPad = static_cast<std::size_t>(std::max(0,
        (config.windowSize - analysisHop) / 2));
    auto rightPad = static_cast<std::size_t>(std::max(0,
        (config.windowSize - analysisHop + 1) / 2));
    // A consonant/Attack segment can be shorter than one model hop.  The
    // training-style padding then still needs to provide a complete FFT
    // window; returning a synthetic silent Mel frame here caused a hard
    // spectral step at the consonant/vowel boundary.
    const auto paddedSamples = leftPad + waveform.size() + rightPad;
    if (paddedSamples < static_cast<std::size_t>(config.windowSize))
    {
        const auto missing = static_cast<std::size_t>(config.windowSize) - paddedSamples;
        leftPad += missing / 2;
        rightPad += missing - missing / 2;
    }
    std::vector<float> padded(leftPad + waveform.size() + rightPad);
    for (std::size_t index = 0; index < padded.size(); ++index)
        padded[index] = waveform[reflectIndex(static_cast<std::ptrdiff_t>(index)
            - static_cast<std::ptrdiff_t>(leftPad), waveform.size())];
    if (padded.size() < static_cast<std::size_t>(config.windowSize))
        return { std::vector<float>(static_cast<std::size_t>(config.melBands),
                                    std::log(1.0e-9f)), 1 };
    const auto frames = 1 + (padded.size() - static_cast<std::size_t>(config.windowSize))
        / static_cast<std::size_t>(analysisHop);
    const auto frequencies = config.fftSize / 2 + 1;
    const auto weights = filterbank(config);
    std::vector<float> window(static_cast<std::size_t>(config.windowSize));
    for (int index = 0; index < config.windowSize; ++index)
        window[static_cast<std::size_t>(index)] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * index
                / static_cast<float>(std::max(1, config.windowSize - 1)));
    auto order = 0;
    for (auto size = config.fftSize; size > 1; size >>= 1) ++order;
    juce::dsp::FFT fft(order);
    std::vector<std::complex<float>> input(static_cast<std::size_t>(config.fftSize));
    std::vector<std::complex<float>> spectrum(static_cast<std::size_t>(config.fftSize));
    std::vector<float> magnitude(static_cast<std::size_t>(frequencies));
    std::vector<float> mel(static_cast<std::size_t>(config.melBands) * frames);
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        std::fill(input.begin(), input.end(), std::complex<float>{});
        for (int index = 0; index < config.windowSize; ++index)
            input[static_cast<std::size_t>(index)] = {
                padded[frame * static_cast<std::size_t>(analysisHop)
                    + static_cast<std::size_t>(index)] * window[static_cast<std::size_t>(index)], 0.0f
            };
        fft.perform(input.data(), spectrum.data(), false);
        for (int bin = 0; bin < frequencies; ++bin)
            magnitude[static_cast<std::size_t>(bin)] = std::abs(
                spectrum[static_cast<std::size_t>(bin)]);
        for (int band = 0; band < config.melBands; ++band)
        {
            auto sum = 0.0f;
            for (int bin = 0; bin < frequencies; ++bin)
                sum += weights[static_cast<std::size_t>(band * frequencies + bin)]
                    * magnitude[static_cast<std::size_t>(bin)];
            // ONNX expects [batch, mel, time], so each band owns one contiguous row.
            mel[static_cast<std::size_t>(band) * frames + frame]
                = std::log(std::max(1.0e-9f, sum));
        }
    }
    return { std::move(mel), frames };
}

// Windowed Mel frame centred on an absolute source sample index.  Consecutive
// target frames may therefore use different source hops without segment-local
// padding or an additional time interpolation pass.
std::vector<float> melFrameAt(const std::vector<float>& waveform,
                              const Config& config, double centerSample,
                              const std::vector<float>& filterWeights,
                              const std::vector<float>& window)
{
    const auto size = static_cast<std::ptrdiff_t>(waveform.size());
    if (size <= 0 || config.windowSize <= 0 || config.fftSize <= 0)
        return {};
    auto order = 0;
    for (auto fft = config.fftSize; fft > 1; fft >>= 1) ++order;
    juce::dsp::FFT fft(order);
    std::vector<std::complex<float>> input(static_cast<std::size_t>(config.fftSize));
    std::vector<std::complex<float>> spectrum(static_cast<std::size_t>(config.fftSize));
    const auto windowStart = static_cast<std::ptrdiff_t>(std::llround(centerSample))
        - static_cast<std::ptrdiff_t>(config.windowSize) / 2;
    for (int index = 0; index < config.windowSize; ++index)
    {
        auto sourceIndex = windowStart + index;
        // Periodic reflection of the whole waveform, mirroring reflectIndex.
        if (size > 1)
        {
            const auto period = 2 * (size - 1);
            sourceIndex %= period;
            if (sourceIndex < 0) sourceIndex += period;
            if (sourceIndex >= size) sourceIndex = period - sourceIndex;
        }
        else sourceIndex = 0;
        input[static_cast<std::size_t>(index)] = {
            waveform[static_cast<std::size_t>(sourceIndex)]
                * window[static_cast<std::size_t>(index)], 0.0f };
    }
    fft.perform(input.data(), spectrum.data(), false);
    const auto frequencies = config.fftSize / 2 + 1;
    std::vector<float> mel(static_cast<std::size_t>(config.melBands));
    for (int band = 0; band < config.melBands; ++band)
    {
        auto sum = 0.0f;
        for (int bin = 0; bin < frequencies; ++bin)
            sum += filterWeights[static_cast<std::size_t>(band * frequencies + bin)]
                * std::abs(spectrum[static_cast<std::size_t>(bin)]);
        mel[static_cast<std::size_t>(band)] = std::log(std::max(1.0e-9f, sum));
    }
    return mel;
}

MelData interpolateMelTime(const MelData& source, int melBands,
                           std::size_t targetFrames)
{
    if (source.frames == 0 || targetFrames == 0 || melBands <= 0) return {};
    if (source.frames == targetFrames) return source;
    MelData result;
    result.frames = targetFrames;
    result.values.resize(static_cast<std::size_t>(melBands) * targetFrames);
    const auto scale = targetFrames <= 1 ? 0.0
        : static_cast<double>(source.frames - 1) / static_cast<double>(targetFrames - 1);
    for (int band = 0; band < melBands; ++band)
        for (std::size_t frame = 0; frame < targetFrames; ++frame)
        {
            const auto sourcePosition = static_cast<double>(frame) * scale;
            const auto left = std::min(source.frames - 1,
                static_cast<std::size_t>(std::floor(sourcePosition)));
            const auto right = std::min(source.frames - 1, left + 1);
            const auto amount = static_cast<float>(sourcePosition - std::floor(sourcePosition));
            const auto row = static_cast<std::size_t>(band) * source.frames;
            result.values[static_cast<std::size_t>(band) * targetFrames + frame]
                = source.values[row + left]
                + (source.values[row + right] - source.values[row + left]) * amount;
        }
    return result;
}

MelData spliceMelToTimeMap(const MelData& source, int melBands,
                           std::size_t targetFrames, double targetHopSeconds,
                           double sourceHopSeconds,
                           const std::vector<NsfHifiganTimeMapPoint>& timeMap)
{
    if (timeMap.size() < 2 || source.frames == 0 || targetFrames == 0
        || targetHopSeconds <= 0.0 || sourceHopSeconds <= 0.0)
        return interpolateMelTime(source, melBands, targetFrames);
    MelData result;
    result.frames = targetFrames;
    result.values.resize(static_cast<std::size_t>(melBands) * targetFrames);
    std::size_t segment = 0;
    for (std::size_t frame = 0; frame < targetFrames; ++frame)
    {
        const auto targetSeconds = static_cast<double>(frame) * targetHopSeconds;
        while (segment + 1 < timeMap.size()
               && targetSeconds > timeMap[segment + 1].targetSeconds)
            ++segment;
        const auto rightIndex = std::min(segment + 1, timeMap.size() - 1);
        const auto& leftPoint = timeMap[std::min(segment, timeMap.size() - 1)];
        const auto& rightPoint = timeMap[rightIndex];
        const auto duration = rightPoint.targetSeconds - leftPoint.targetSeconds;
        const auto amount = duration > 1.0e-9
            ? juce::jlimit(0.0, 1.0,
                (targetSeconds - leftPoint.targetSeconds) / duration)
            : 0.0;
        const auto sourceSeconds = leftPoint.sourceSeconds
            + (rightPoint.sourceSeconds - leftPoint.sourceSeconds) * amount;
        const auto sourcePosition = juce::jlimit(0.0,
            static_cast<double>(source.frames - 1), sourceSeconds / sourceHopSeconds);
        const auto sourceLeft = static_cast<std::size_t>(std::floor(sourcePosition));
        const auto sourceRight = std::min(source.frames - 1, sourceLeft + 1);
        const auto sourceAmount = static_cast<float>(sourcePosition - std::floor(sourcePosition));
        for (int band = 0; band < melBands; ++band)
        {
            const auto row = static_cast<std::size_t>(band) * source.frames;
            result.values[static_cast<std::size_t>(band) * targetFrames + frame]
                = source.values[row + sourceLeft]
                + (source.values[row + sourceRight] - source.values[row + sourceLeft])
                    * sourceAmount;
        }
    }
    return result;
}

void normalizeMelToReference(MelData& mel, const MelData& reference, int melBands)
{
    if (mel.frames == 0 || mel.frames != reference.frames || melBands <= 0) return;
    std::vector<float> correction(mel.frames, 0.0f);
    for (std::size_t frame = 0; frame < mel.frames; ++frame)
    {
        auto current = 0.0;
        auto target = 0.0;
        for (int band = 0; band < melBands; ++band)
        {
            const auto offset = static_cast<std::size_t>(band) * mel.frames + frame;
            current += std::exp(static_cast<double>(mel.values[offset]));
            target += std::exp(static_cast<double>(reference.values[offset]));
        }
        if (current > 1.0e-10 && target > 1.0e-10)
            correction[frame] = static_cast<float>(std::clamp(
                std::log(target / current), -std::log(2.0), std::log(2.0)));
    }
    const auto raw = correction;
    constexpr std::ptrdiff_t radius = 2;
    for (std::size_t frame = 0; frame < mel.frames; ++frame)
    {
        auto sum = 0.0f;
        auto count = 0;
        const auto centre = static_cast<std::ptrdiff_t>(frame);
        for (auto offset = -radius; offset <= radius; ++offset)
        {
            const auto index = centre + offset;
            if (index < 0 || index >= static_cast<std::ptrdiff_t>(mel.frames)) continue;
            sum += raw[static_cast<std::size_t>(index)];
            ++count;
        }
        correction[frame] = count > 0 ? sum / static_cast<float>(count) : 0.0f;
    }
    for (int band = 0; band < melBands; ++band)
        for (std::size_t frame = 0; frame < mel.frames; ++frame)
            mel.values[static_cast<std::size_t>(band) * mel.frames + frame]
                += correction[frame];
}

MelData buildVariableHopSplicedMel(const std::vector<float>& waveform,
                                   const Config& config,
                                   std::size_t targetFrames,
                                   const std::vector<NsfHifiganTimeMapPoint>& timeMap,
                                   bool shiftBeforeSplice = false,
                                   const std::vector<float>* formantSemitones = nullptr,
                                   double framePeriodMs = 5.0)
{
    if (targetFrames == 0 || waveform.empty()) return {};
    const auto targetHopSeconds = static_cast<double>(config.hop) / config.sampleRate;
    if (timeMap.size() < 2)
        return interpolateMelTime(buildMelWithHop(waveform, config, config.hop),
                                  config.melBands, targetFrames);
    MelData result;
    result.frames = targetFrames;
    result.values.assign(static_cast<std::size_t>(config.melBands) * targetFrames,
                         std::log(1.0e-9f));

    // Analyse one non-uniformly spaced source window for each target Mel frame.
    // Melodyne time maps can be much denser than the model hop (for example,
    // 173 anchors for about 30 target frames).  Quantising every anchor pair to
    // targetBegin/targetEnd made five or six source segments overwrite the same
    // target frame, leaving hard spectral jumps that the NSF decoder amplified
    // into full-scale bursts.  Sampling the complete map once per target frame
    // preserves every timing edit without creating overlapping Mel segments.
    std::vector<float> window(static_cast<std::size_t>(config.windowSize));
    for (int index = 0; index < config.windowSize; ++index)
        window[static_cast<std::size_t>(index)] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * index
                / static_cast<float>(std::max(1, config.windowSize - 1)));
    const auto filterWeights = filterbank(config);
    std::size_t segment = 0;
    for (std::size_t frame = 0; frame < targetFrames; ++frame)
    {
        const auto targetSeconds = (static_cast<double>(frame) + 0.5)
            * targetHopSeconds;
        while (segment + 1 < timeMap.size()
               && targetSeconds > timeMap[segment + 1].targetSeconds)
            ++segment;
        const auto rightIndex = std::min(segment + 1, timeMap.size() - 1);
        const auto& leftPoint = timeMap[std::min(segment, timeMap.size() - 1)];
        const auto& rightPoint = timeMap[rightIndex];
        const auto duration = rightPoint.targetSeconds - leftPoint.targetSeconds;
        const auto amount = duration > 1.0e-9
            ? juce::jlimit(0.0, 1.0,
                (targetSeconds - leftPoint.targetSeconds) / duration)
            : 0.0;
        const auto sourceSeconds = leftPoint.sourceSeconds
            + (rightPoint.sourceSeconds - leftPoint.sourceSeconds) * amount;
        const auto centerSample = juce::jlimit(0.0,
            static_cast<double>(waveform.size() - 1),
            sourceSeconds * config.sampleRate);
        const auto frameMel = melFrameAt(waveform, config, centerSample,
                                         filterWeights, window);
        if (frameMel.size() != static_cast<std::size_t>(config.melBands)) continue;
        for (int band = 0; band < config.melBands; ++band)
            result.values[static_cast<std::size_t>(band) * targetFrames + frame]
                = frameMel[static_cast<std::size_t>(band)];
    }

    // Formant shifting is pointwise in target Mel time.  With one mapped frame
    // per target frame there is no interpolation/crossfade for it to commute
    // across, so applying it here is the shift-then-splice order exactly.
    if (shiftBeforeSplice && formantSemitones != nullptr
        && !formantSemitones->empty())
        shiftMelFormants(result, config, *formantSemitones, framePeriodMs);
    return result;
}

std::vector<float> stableMonoInput(const juce::AudioBuffer<float>& source)
{
    const auto channels = source.getNumChannels();
    const auto samples = source.getNumSamples();
    std::vector<float> mono(static_cast<std::size_t>(std::max(0, samples)));
    if (channels <= 0 || samples <= 0) return mono;
    if (channels == 1)
    {
        std::copy_n(source.getReadPointer(0), samples, mono.begin());
        return mono;
    }

    const auto* left = source.getReadPointer(0);
    const auto* right = source.getReadPointer(1);
    auto leftPower = 0.0;
    auto rightPower = 0.0;
    auto crossPower = 0.0;
    for (int sample = 0; sample < samples; ++sample)
    {
        leftPower += static_cast<double>(left[sample]) * left[sample];
        rightPower += static_cast<double>(right[sample]) * right[sample];
        crossPower += static_cast<double>(left[sample]) * right[sample];
    }
    const auto denominator = std::sqrt(leftPower * rightPower);
    const auto correlation = denominator > 1.0e-12 ? crossPower / denominator : 1.0;
    if (correlation >= 0.35)
        for (int sample = 0; sample < samples; ++sample)
            mono[static_cast<std::size_t>(sample)] = 0.5f * (left[sample] + right[sample]);
    else
    {
        // Phase-widened stereo vocals can lose their low/mid spectral envelope
        // when averaged, which the decoder presents as a smaller/child-like
        // vocal tract.  Use the more energetic intact channel in that case.
        const auto* selected = leftPower >= rightPower ? left : right;
        std::copy_n(selected, samples, mono.begin());
    }
    return mono;
}

void addModelEdgeContext(MelData& mel, std::vector<float>& f0, int melBands,
                         std::size_t contextFrames)
{
    if (mel.frames == 0 || f0.size() != mel.frames || melBands <= 0
        || contextFrames == 0) return;
    const auto originalFrames = mel.frames;
    MelData padded;
    padded.frames = originalFrames + contextFrames * 2;
    padded.values.resize(static_cast<std::size_t>(melBands) * padded.frames);
    std::vector<float> paddedF0(padded.frames);
    for (std::size_t frame = 0; frame < padded.frames; ++frame)
    {
        const auto sourceFrame = reflectIndex(
            static_cast<std::ptrdiff_t>(frame) - static_cast<std::ptrdiff_t>(contextFrames),
            originalFrames);
        paddedF0[frame] = f0[sourceFrame];
        for (int band = 0; band < melBands; ++band)
            padded.values[static_cast<std::size_t>(band) * padded.frames + frame]
                = mel.values[static_cast<std::size_t>(band) * originalFrames + sourceFrame];
    }
    mel = std::move(padded);
    f0 = std::move(paddedF0);
}

void shiftMelFormants(MelData& mel, const Config& config,
                      const std::vector<float>& formantSemitones,
                      double framePeriodMs, std::size_t frameOffset)
{
    if (mel.frames == 0 || formantSemitones.empty()) return;
    const auto melMinimum = hzToMel(std::max(0.0f, config.minimumHz));
    const auto melMaximum = hzToMel(std::max(config.minimumHz + 1.0f, config.maximumHz));
    const auto melRange = std::max(1.0e-9f, melMaximum - melMinimum);
    const auto bands = static_cast<float>(config.melBands);
    const auto silence = std::log(1.0e-9f);
    std::vector<float> centres(static_cast<std::size_t>(config.melBands));
    std::vector<float> column(static_cast<std::size_t>(config.melBands));
    for (int band = 0; band < config.melBands; ++band)
        centres[static_cast<std::size_t>(band)] = melToHz(melMinimum
            + (static_cast<float>(band) + 1.0f) * melRange / (bands + 1.0f));
    const auto hopSeconds = static_cast<double>(config.hop) / config.sampleRate;
    const auto curvePeriod = std::max(0.1, framePeriodMs);
    for (std::size_t frame = 0; frame < mel.frames; ++frame)
    {
        const auto shift = scalarCurveAt(formantSemitones,
            static_cast<double>(frameOffset + frame) * hopSeconds * 1000.0 / curvePeriod);
        if (!std::isfinite(shift) || std::abs(shift) < 5.0e-4f) continue;
        const auto ratio = std::exp2(shift / 12.0f);
        if (!std::isfinite(ratio) || ratio <= 0.0f) continue;
        for (int band = 0; band < config.melBands; ++band)
            column[static_cast<std::size_t>(band)] = mel.values[
                static_cast<std::size_t>(band) * mel.frames + frame];
        for (int band = 0; band < config.melBands; ++band)
        {
            const auto sourceHz = centres[static_cast<std::size_t>(band)] / ratio;
            const auto sourceBin = (hzToMel(std::max(0.0f, sourceHz)) - melMinimum)
                / melRange * (bands + 1.0f) - 1.0f;
            const auto left = static_cast<int>(std::floor(sourceBin));
            auto value = silence;
            if (left >= 0 && left < config.melBands)
            {
                if (left == config.melBands - 1)
                    value = column[static_cast<std::size_t>(left)];
                else
                {
                    const auto amount = juce::jlimit(0.0f, 1.0f,
                        sourceBin - static_cast<float>(left));
                    value = column[static_cast<std::size_t>(left)]
                        + (column[static_cast<std::size_t>(left + 1)]
                           - column[static_cast<std::size_t>(left)]) * amount;
                }
            }
            mel.values[static_cast<std::size_t>(band) * mel.frames + frame] = value;
        }
    }
}

void conditionNeuralBoundary(float* samples, int count, double sampleRate)
{
    if (samples == nullptr || count <= 0 || sampleRate <= 0.0) return;
    // Neural segments can carry a small DC step even when their Mel/F0 inputs
    // are continuous.  Remove it before the clip enters the project mixer.
    const auto pole = static_cast<float>(std::exp(
        -juce::MathConstants<double>::twoPi * 22.0 / sampleRate));
    auto previousInput = samples[0];
    auto previousOutput = 0.0f;
    for (int index = 0; index < count; ++index)
    {
        const auto input = samples[index];
        const auto output = input - previousInput + pole * previousOutput;
        samples[index] = std::isfinite(output) ? output : 0.0f;
        previousInput = input;
        previousOutput = samples[index];
    }
    // Repair isolated decoder impulses while leaving genuine multi-sample
    // consonant attacks untouched.  An NSF pop is a 1-3 sample broadband
    // spike: its value deviates far from the local mean while the immediate
    // neighbours stay smooth (a real attack ramps, it does not jump-and-hold).
    // A relative threshold makes the repair catch mid-level pops too, not only
    // near-full-scale single-sample clicks.
    for (int index = 2; index + 2 < count; ++index)
    {
        const auto localMean = 0.25f * (samples[index - 2] + samples[index - 1]
                                      + samples[index + 1] + samples[index + 2]);
        const auto localRms = std::sqrt(std::max(1.0e-9f,
            0.25f * (samples[index - 2] * samples[index - 2]
                   + samples[index - 1] * samples[index - 1]
                   + samples[index + 1] * samples[index + 1]
                   + samples[index + 2] * samples[index + 2])));
        const auto deviation = std::abs(samples[index] - localMean);
        const auto surroundingSmooth =
               std::abs(samples[index + 1] - samples[index - 1]) < 0.18f
            && std::abs(samples[index + 2] - samples[index - 2]) < 0.32f;
        if (deviation > std::max(0.16f, 2.5f * localRms) && surroundingSmooth)
            samples[index] = localMean;
    }
    // Clip boundaries are not guaranteed to coincide with a decoder source
    // phase.  A 3 ms equal-power guard suppresses that isolated discontinuity
    // without smearing attacks or creating an audible long crossfade.
    const auto fade = std::min(count / 2, std::max(1,
        static_cast<int>(std::llround(sampleRate * 0.003))));
    for (int index = 0; index < fade; ++index)
    {
        const auto phase = (static_cast<double>(index) + 1.0)
            / (static_cast<double>(fade) + 1.0);
        const auto gainIn = static_cast<float>(std::sin(
            juce::MathConstants<double>::halfPi * phase));
        const auto gainOut = static_cast<float>(std::sin(
            juce::MathConstants<double>::halfPi * (1.0 - phase)));
        samples[index] *= gainIn;
        samples[count - 1 - index] *= gainOut;
    }
}

#if JUCE_WINDOWS
using OrtPath = std::wstring;
OrtPath ortPath(const juce::File& file) { return file.getFullPathName().toWideCharPointer(); }
#else
using OrtPath = std::string;
OrtPath ortPath(const juce::File& file) { return file.getFullPathName().toStdString(); }
#endif

Ort::Env& environment()
{
    static Ort::Env value(ORT_LOGGING_LEVEL_WARNING, "hachishifter-nsf-hifigan");
    return value;
}

struct SessionEntry
{
    std::shared_ptr<Ort::Session> model;
    std::shared_ptr<std::mutex> runMutex;
    juce::String activeInference;
};

SessionEntry session(const juce::File& model, const OrtExecutionConfig& execution)
{
    static std::mutex mutex;
    static std::unordered_map<std::string, SessionEntry> sessions;
    std::scoped_lock lock(mutex);
    const auto key = (model.getFullPathName() + "#"
        + juce::String(model.getLastModificationTime().toMilliseconds()) + "#"
        + juce::String(model.getSize()) + "#"
        + inferenceBackendName(execution.requested) + "#"
        + juce::String(execution.deviceIndex) + "#"
        + juce::String(execution.intraOpThreads)).toStdString();
    if (const auto found = sessions.find(key); found != sessions.end())
        return found->second;
    SessionEntry value;
    auto options = makeOrtSessionOptions(execution, value.activeInference);
    value.model = std::make_shared<Ort::Session>(
        environment(), ortPath(model).c_str(), options);
    if (value.activeInference.startsWith("directml"))
        value.runMutex = std::make_shared<std::mutex>();
    sessions[key] = value;
    return value;
}

std::vector<float> infer(Ort::Session& model, const Config& config,
                         const MelData& mel, const std::vector<float>& f0,
                         std::mutex* runMutex)
{
    Ort::AllocatorWithDefaultOptions allocator;
    if (model.GetInputCount() < 2 || model.GetOutputCount() < 1
        || mel.frames == 0 || f0.size() != mel.frames
        || mel.values.size() != static_cast<std::size_t>(config.melBands) * mel.frames)
        return {};
    const auto melName = model.GetInputNameAllocated(0, allocator);
    const auto f0Name = model.GetInputNameAllocated(1, allocator);
    const auto outputName = model.GetOutputNameAllocated(0, allocator);
    const char* inputNames[] { melName.get(), f0Name.get() };
    const char* outputNames[] { outputName.get() };
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    constexpr std::size_t coreFrames = 4'096;
    constexpr std::size_t contextFrames = 32;
    constexpr std::size_t overlapFrames = 16;
    constexpr std::size_t stepFrames = coreFrames - overlapFrames;
    std::vector<float> output(mel.frames * static_cast<std::size_t>(config.hop));
    std::vector<float> weight(output.size());
    for (std::size_t coreStart = 0; coreStart < mel.frames; coreStart += stepFrames)
    {
        const auto coreEnd = std::min(mel.frames, coreStart + coreFrames);
        const auto inputStart = coreStart > contextFrames ? coreStart - contextFrames : 0;
        const auto inputEnd = std::min(mel.frames, coreEnd + contextFrames);
        const auto inputFrames = inputEnd - inputStart;
        std::vector<float> melChunk(static_cast<std::size_t>(config.melBands) * inputFrames);
        for (int band = 0; band < config.melBands; ++band)
            std::copy_n(mel.values.begin() + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(band) * mel.frames + inputStart), inputFrames,
                melChunk.begin() + static_cast<std::ptrdiff_t>(
                    static_cast<std::size_t>(band) * inputFrames));
        std::vector<float> f0Chunk(f0.begin() + static_cast<std::ptrdiff_t>(inputStart),
                                   f0.begin() + static_cast<std::ptrdiff_t>(inputEnd));
        const std::array<int64_t, 3> melShape {
            1, config.melBands, static_cast<int64_t>(inputFrames)
        };
        const std::array<int64_t, 2> f0Shape { 1, static_cast<int64_t>(inputFrames) };
        auto melTensor = Ort::Value::CreateTensor<float>(memory, melChunk.data(), melChunk.size(),
                                                          melShape.data(), melShape.size());
        auto f0Tensor = Ort::Value::CreateTensor<float>(memory, f0Chunk.data(), f0Chunk.size(),
                                                         f0Shape.data(), f0Shape.size());
        std::array<Ort::Value, 2> inputs { std::move(melTensor), std::move(f0Tensor) };
        std::unique_lock<std::mutex> runLock;
        if (runMutex != nullptr) runLock = std::unique_lock<std::mutex>(*runMutex);
        auto rendered = model.Run(Ort::RunOptions{ nullptr }, inputNames, inputs.data(),
                                  inputs.size(), outputNames, 1);
        if (rendered.empty()) return {};
        const auto info = rendered[0].GetTensorTypeAndShapeInfo();
        const auto count = info.GetElementCount();
        const auto* values = rendered[0].GetTensorData<float>();
        const auto crop = (coreStart - inputStart) * static_cast<std::size_t>(config.hop);
        const auto wanted = (coreEnd - coreStart) * static_cast<std::size_t>(config.hop);
        const auto available = count > crop ? std::min(wanted, count - crop) : 0;
        // A short tensor would otherwise leave a zero-filled hole while still
        // claiming that the neural backend succeeded.  Treat an incompatible
        // model/output shape as a render failure so the established fallback
        // remains audible for the complete clip.
        if (available != wanted) return {};
        const auto overlapSamples = overlapFrames * static_cast<std::size_t>(config.hop);
        const auto outputOffset = coreStart * static_cast<std::size_t>(config.hop);
        for (std::size_t sample = 0; sample < available; ++sample)
        {
            auto blend = 1.0f;
            if (coreStart > 0 && sample < overlapSamples)
            {
                const auto phase = (static_cast<double>(sample) + 0.5)
                    / static_cast<double>(overlapSamples);
                blend *= static_cast<float>(std::sin(
                    juce::MathConstants<double>::halfPi * phase));
            }
            if (coreEnd < mel.frames && available - sample <= overlapSamples)
            {
                const auto phase = (static_cast<double>(available - sample) - 0.5)
                    / static_cast<double>(overlapSamples);
                blend *= static_cast<float>(std::sin(
                    juce::MathConstants<double>::halfPi * phase));
            }
            const auto destination = outputOffset + sample;
            output[destination] += values[crop + sample] * blend;
            weight[destination] += blend;
        }
        if (coreEnd == mel.frames) break;
    }
    for (std::size_t sample = 0; sample < output.size(); ++sample)
        if (weight[sample] > 1.0e-8f) output[sample] /= weight[sample];
    return output;
}
}
#endif

NsfHifiganRenderResult NsfHifiganRenderer::render(
    const juce::AudioBuffer<float>& source, double sampleRate, int targetSamples,
    double framePeriodMs, const std::vector<float>& targetMidi,
    const std::vector<float>& formantSemitones,
    const std::vector<NsfHifiganTimeMapPoint>& timeMap,
    const juce::File& configuredModelDirectory,
    const OrtExecutionConfig& execution, NsfHifiganStretchOrder stretchOrder,
    bool normalizeVolume)
{
    NsfHifiganRenderResult result;
#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
    try
    {
        const auto files = modelFiles(configuredModelDirectory);
        if (!files)
        {
            result.error = "NSF-HiFiGAN model pack not found";
            return result;
        }
        result.modelFile = files->onnx;
        auto config = readConfig(files->config, result.error);
        if (!config) return result;
        auto ortSession = session(files->onnx, execution);
        result.activeInference = ortSession.activeInference;
        if (ortSession.model == nullptr)
        {
            result.error = "NSF-HiFiGAN ONNX session could not be created";
            return result;
        }
        const auto channels = source.getNumChannels();
        const auto sourceSamples = source.getNumSamples();
        if (channels <= 0 || sourceSamples <= 0 || targetSamples <= 0 || sampleRate <= 0.0)
        {
            result.error = "NSF-HiFiGAN received empty audio";
            return result;
        }
        result.buffer.setSize(channels, targetSamples);
        result.buffer.clear();
        // The public NSF model is mono.  Use a correlation-aware fold-down so
        // phase-widened stereo material keeps its vocal-tract envelope.
        auto mono = stableMonoInput(source);
        const auto modelInput = resample(mono.data(), sourceSamples,
            static_cast<int>(std::llround(sampleRate)), config->sampleRate);
        const auto targetModelSamples = std::max<std::size_t>(1,
            static_cast<std::size_t>(std::llround(static_cast<double>(targetSamples)
                * config->sampleRate / sampleRate)));
        const auto targetFrames = std::max<std::size_t>(1,
            (targetModelSamples + static_cast<std::size_t>(config->hop) - 1)
                / static_cast<std::size_t>(config->hop));
        auto referenceMel = normalizeVolume
            ? spliceMelToTimeMap(buildMelWithHop(modelInput, *config, config->hop),
                config->melBands, targetFrames,
                static_cast<double>(config->hop) / config->sampleRate,
                static_cast<double>(config->hop) / config->sampleRate, timeMap)
            : MelData{};
        auto mel = stretchOrder == NsfHifiganStretchOrder::spliceThenShift
            ? buildVariableHopSplicedMel(modelInput, *config, targetFrames, timeMap)
            : stretchOrder == NsfHifiganStretchOrder::shiftThenSplice
                ? buildVariableHopSplicedMel(modelInput, *config, targetFrames, timeMap,
                    true, &formantSemitones, framePeriodMs)
                : spliceMelToTimeMap(buildMelWithHop(modelInput, *config, config->hop),
                    config->melBands, targetFrames,
                    static_cast<double>(config->hop) / config->sampleRate,
                    static_cast<double>(config->hop) / config->sampleRate, timeMap);
        if (mel.frames == 0)
        {
            result.error = "NSF-HiFiGAN mel extraction returned no frames";
            result.buffer.setSize(0, 0);
            return result;
        }
        // Splice-first and fixed-hop orders shift the whole joined Mel once,
        // after the segments are blended; the shift-first order already applied
        // the formant curve inside buildVariableHopSplicedMel per segment.
        if (stretchOrder != NsfHifiganStretchOrder::shiftThenSplice)
            shiftMelFormants(mel, *config, formantSemitones, framePeriodMs);
        if (normalizeVolume)
        {
            shiftMelFormants(referenceMel, *config, formantSemitones, framePeriodMs);
            normalizeMelToReference(mel, referenceMel, config->melBands);
        }
        std::vector<float> f0(mel.frames);
        const auto hopSeconds = static_cast<double>(config->hop) / config->sampleRate;
        const auto framePeriod = std::max(0.1, framePeriodMs);
        for (std::size_t frame = 0; frame < mel.frames; ++frame)
        {
            const auto seconds = static_cast<double>(frame) * hopSeconds;
            f0[frame] = midiToHz(curveAt(targetMidi,
                seconds * 1000.0 / framePeriod));
        }
        // A hard voiced->unvoiced f0 step (e.g. 240 Hz -> 0 in one frame) makes
        // the NSF source filter switch its excitation abruptly and pops at the
        // vowel/consonant boundary.  Taper the frame that touches the edge so
        // the drop/rise is a short glide instead of an impulse.
        for (std::size_t frame = 1; frame + 1 < f0.size(); ++frame)
        {
            if (f0[frame] > 0.0f && f0[frame + 1] <= 0.0f && f0[frame - 1] > 0.0f)
                f0[frame] *= 0.55f;
            else if (f0[frame] <= 0.0f && f0[frame + 1] > 0.0f && f0[frame - 1] <= 0.0f)
                f0[frame] = f0[frame + 1] * 0.55f;
        }
        // A hard voiced->voiced f0 step (e.g. a Melodyne hard-disconnect split:
        // AudioEngine only glides connected notes) jumps the NSF excitation by
        // a musical interval in one hop, which the decoder turns into a click.
        // Glide the single transition frame to the geometric midpoint so the
        // interval becomes a ~2 frame portamento without smearing a real
        // glissando (neighbours are read from a snapshot to avoid cascading).
        const auto f0Snapshot = f0;
        for (std::size_t frame = 1; frame + 1 < f0.size(); ++frame)
        {
            const auto a = f0Snapshot[frame - 1];
            const auto b = f0Snapshot[frame];
            const auto c = f0Snapshot[frame + 1];
            if (!(a > 0.0f && b > 0.0f && c > 0.0f)) continue;
            const auto step = std::abs(12.0 * std::log2(c / a));
            if (step <= 1.5) continue;
            // A real glissando has the middle frame mid-slope (b roughly the
            // geometric mean of a and c), so it is flat against neither
            // neighbour.  Only a hard step sits flat against one side.
            const auto leftGap = std::abs(12.0 * std::log2(b / a));
            const auto rightGap = std::abs(12.0 * std::log2(c / b));
            if (leftGap < 0.5 || rightGap < 0.5)
                f0[frame] = std::sqrt(a * c);
        }
        constexpr std::size_t edgeContextFrames = 16;
        addModelEdgeContext(mel, f0, config->melBands, edgeContextFrames);
        auto modelOutput = infer(*ortSession.model, *config, mel, f0,
                                 ortSession.runMutex.get());
        if (modelOutput.empty())
        {
            result.error = "NSF-HiFiGAN ONNX returned empty audio";
            result.buffer.setSize(0, 0);
            return result;
        }
        const auto contextSamples = edgeContextFrames * static_cast<std::size_t>(config->hop);
        const auto unpaddedSamples = targetFrames * static_cast<std::size_t>(config->hop);
        if (modelOutput.size() < contextSamples + unpaddedSamples)
        {
            result.error = "NSF-HiFiGAN ONNX returned short context audio";
            result.buffer.setSize(0, 0);
            return result;
        }
        std::vector<float> croppedModelOutput(unpaddedSamples);
        std::copy_n(modelOutput.begin() + static_cast<std::ptrdiff_t>(contextSamples),
                    unpaddedSamples, croppedModelOutput.begin());
        const auto output = resample(croppedModelOutput.data(),
                                      static_cast<int>(croppedModelOutput.size()),
                                      config->sampleRate,
                                      static_cast<int>(std::llround(sampleRate)));
        const auto wanted = static_cast<std::size_t>(targetSamples);
        const auto maximumNaturalShortfall = static_cast<std::size_t>(std::ceil(
            static_cast<double>(config->hop) * sampleRate / config->sampleRate)) + 2;
        if (output.size() + maximumNaturalShortfall < wanted)
        {
            result.error = "NSF-HiFiGAN ONNX returned short audio";
            result.buffer.setSize(0, 0);
            return result;
        }
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* destination = result.buffer.getWritePointer(channel);
            const auto copied = std::min(output.size(), wanted);
            std::copy_n(output.begin(), copied, destination);
            if (copied < wanted)
                std::fill(destination + static_cast<std::ptrdiff_t>(copied),
                          destination + static_cast<std::ptrdiff_t>(wanted), 0.0f);
            conditionNeuralBoundary(destination, targetSamples, sampleRate);
        }
        result.usedModel = true;
        return result;
    }
    catch (const Ort::Exception& exception)
    {
        result.error = "NSF-HiFiGAN ONNX inference failed: " + juce::String(exception.what());
    }
    catch (const std::exception& exception)
    {
        result.error = "NSF-HiFiGAN render failed: " + juce::String(exception.what());
    }
#else
    juce::ignoreUnused(source, sampleRate, targetSamples, framePeriodMs, targetMidi,
                       formantSemitones, timeMap, configuredModelDirectory, execution,
                       stretchOrder, normalizeVolume);
    result.error = "NSF-HiFiGAN ONNX runtime is not included";
#endif
    return result;
}
}
