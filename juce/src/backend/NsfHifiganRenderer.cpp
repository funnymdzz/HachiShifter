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
    for (int index = 0; index < outputSamples; ++index)
    {
        const auto position = static_cast<double>(index) / ratio;
        const auto left = juce::jlimit(0, samples - 1, static_cast<int>(std::floor(position)));
        const auto right = std::min(samples - 1, left + 1);
        const auto amount = static_cast<float>(position - std::floor(position));
        output[static_cast<std::size_t>(index)] = input[left]
            + (input[right] - input[left]) * amount;
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

MelData buildMel(const std::vector<float>& waveform, const Config& config)
{
    if (waveform.empty()) return {};
    const auto leftPad = static_cast<std::size_t>(std::max(0,
        (config.windowSize - config.hop) / 2));
    const auto rightPad = static_cast<std::size_t>(std::max(0,
        (config.windowSize - config.hop + 1) / 2));
    std::vector<float> padded(leftPad + waveform.size() + rightPad);
    for (std::size_t index = 0; index < padded.size(); ++index)
        padded[index] = waveform[reflectIndex(static_cast<std::ptrdiff_t>(index)
            - static_cast<std::ptrdiff_t>(leftPad), waveform.size())];
    if (padded.size() < static_cast<std::size_t>(config.windowSize))
        return { std::vector<float>(static_cast<std::size_t>(config.melBands),
                                    std::log(1.0e-9f)), 1 };
    const auto frames = 1 + (padded.size() - static_cast<std::size_t>(config.windowSize))
        / static_cast<std::size_t>(config.hop);
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
                padded[frame * static_cast<std::size_t>(config.hop)
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

std::shared_ptr<Ort::Session> session(const juce::File& model)
{
    static std::mutex mutex;
    static std::unordered_map<std::string, std::shared_ptr<Ort::Session>> sessions;
    std::scoped_lock lock(mutex);
    const auto key = (model.getFullPathName() + "#"
        + juce::String(model.getLastModificationTime().toMilliseconds()) + "#"
        + juce::String(model.getSize())).toStdString();
    if (const auto found = sessions.find(key); found != sessions.end())
        return found->second;
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(std::max(1, juce::SystemStats::getNumCpus() - 1));
    auto value = std::make_shared<Ort::Session>(environment(), ortPath(model).c_str(), options);
    sessions[key] = value;
    return value;
}

std::vector<float> infer(Ort::Session& model, const Config& config,
                         const MelData& mel, const std::vector<float>& f0)
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
    constexpr std::size_t contextFrames = 16;
    std::vector<float> output(mel.frames * static_cast<std::size_t>(config.hop));
    for (std::size_t coreStart = 0; coreStart < mel.frames; coreStart += coreFrames)
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
        auto rendered = model.Run(Ort::RunOptions{ nullptr }, inputNames, inputs.data(), inputs.size(),
                                  outputNames, 1);
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
        std::copy_n(values + static_cast<std::ptrdiff_t>(crop), available,
                    output.begin() + static_cast<std::ptrdiff_t>(
                        coreStart * static_cast<std::size_t>(config.hop)));
    }
    return output;
}
}
#endif

NsfHifiganRenderResult NsfHifiganRenderer::render(
    const juce::AudioBuffer<float>& source, double sampleRate, double framePeriodMs,
    const std::vector<float>& targetMidi, const juce::File& configuredModelDirectory)
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
        auto ortSession = session(files->onnx);
        if (ortSession == nullptr)
        {
            result.error = "NSF-HiFiGAN ONNX session could not be created";
            return result;
        }
        const auto channels = source.getNumChannels();
        const auto samples = source.getNumSamples();
        if (channels <= 0 || samples <= 0 || sampleRate <= 0.0)
        {
            result.error = "NSF-HiFiGAN received empty audio";
            return result;
        }
        result.buffer.setSize(channels, samples);
        result.buffer.clear();
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto modelInput = resample(source.getReadPointer(channel), samples,
                static_cast<int>(std::llround(sampleRate)), config->sampleRate);
            const auto mel = buildMel(modelInput, *config);
            if (mel.frames == 0)
            {
                result.error = "NSF-HiFiGAN mel extraction returned no frames";
                result.buffer.setSize(0, 0);
                return result;
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
            auto modelOutput = infer(*ortSession, *config, mel, f0);
            if (modelOutput.empty())
            {
                result.error = "NSF-HiFiGAN ONNX returned empty audio";
                result.buffer.setSize(0, 0);
                return result;
            }
            const auto output = resample(modelOutput.data(), static_cast<int>(modelOutput.size()),
                                          config->sampleRate,
                                          static_cast<int>(std::llround(sampleRate)));
            const auto wanted = static_cast<std::size_t>(samples);
            const auto maximumNaturalShortfall = static_cast<std::size_t>(std::ceil(
                static_cast<double>(config->hop) * sampleRate / config->sampleRate)) + 2;
            if (output.size() + maximumNaturalShortfall < wanted)
            {
                result.error = "NSF-HiFiGAN ONNX returned short audio";
                result.buffer.setSize(0, 0);
                return result;
            }
            auto* destination = result.buffer.getWritePointer(channel);
            std::copy_n(output.begin(), std::min(output.size(), wanted), destination);
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
    juce::ignoreUnused(source, sampleRate, framePeriodMs, targetMidi,
                       configuredModelDirectory);
    result.error = "NSF-HiFiGAN ONNX runtime is not included";
#endif
    return result;
}
}
