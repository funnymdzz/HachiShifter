#include "FcpeAnalyzer.h"
#include "AudioFileReader.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <mutex>
#include <stdexcept>

#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
#include <onnxruntime_cxx_api.h>
#endif

namespace hachi::backend
{
#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
namespace
{
constexpr int sampleRate = 16'000;
constexpr int hop = 160;
constexpr int fftSize = 1'024;
constexpr int windowSize = 1'024;
constexpr int melBands = 128;
constexpr float minimumHz = 32.7f;
constexpr float maximumHz = 1'975.5f;

struct DecodedAudio
{
    std::vector<float> mono;
    int rate = 0;
};

DecodedAudio decodeMono(const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = createAudioReader(formats, file);
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = "FCPE could not read audio: " + file.getFullPathName();
        return {};
    }
    const auto channels = std::max(1, static_cast<int>(reader->numChannels));
    const auto frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(channels, frames);
    if (!reader->read(&buffer, 0, frames, 0, true, true))
    {
        error = "FCPE audio decode failed";
        return {};
    }
    DecodedAudio decoded;
    decoded.rate = static_cast<int>(std::llround(reader->sampleRate));
    decoded.mono.resize(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames; ++frame)
    {
        auto value = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            value += buffer.getSample(channel, frame);
        decoded.mono[static_cast<std::size_t>(frame)] = value / static_cast<float>(channels);
    }
    return decoded;
}

std::vector<float> resample(const std::vector<float>& input, int inputRate)
{
    if (input.empty() || inputRate <= 0) return {};
    if (inputRate == sampleRate) return input;
    const auto ratio = static_cast<double>(sampleRate) / inputRate;
    const auto size = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::llround(input.size() * ratio)));
    std::vector<float> output(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        const auto source = index / ratio;
        const auto left = std::min(input.size() - 1, static_cast<std::size_t>(source));
        const auto right = std::min(input.size() - 1, left + 1);
        const auto fraction = static_cast<float>(source - left);
        output[index] = input[left] + (input[right] - input[left]) * fraction;
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

std::vector<float> melFilterbank()
{
    constexpr auto frequencies = fftSize / 2 + 1;
    const auto melMinimum = hzToMel(0.0f);
    const auto melMaximum = hzToMel(sampleRate * 0.5f);
    std::vector<float> hzPoints(static_cast<std::size_t>(melBands + 2));
    for (int index = 0; index < melBands + 2; ++index)
        hzPoints[static_cast<std::size_t>(index)] = melToHz(melMinimum
            + (melMaximum - melMinimum) * static_cast<float>(index)
                / static_cast<float>(melBands + 1));
    std::vector<float> weights(static_cast<std::size_t>(melBands * frequencies));
    for (int band = 0; band < melBands; ++band)
    {
        const auto left = hzPoints[static_cast<std::size_t>(band)];
        const auto centre = hzPoints[static_cast<std::size_t>(band + 1)];
        const auto right = hzPoints[static_cast<std::size_t>(band + 2)];
        const auto leftWidth = std::max(1.0e-6f, centre - left);
        const auto rightWidth = std::max(1.0e-6f, right - centre);
        const auto normalization = 2.0f / std::max(1.0e-6f, right - left);
        for (int bin = 0; bin < frequencies; ++bin)
        {
            const auto frequency = static_cast<float>(bin * sampleRate) / fftSize;
            const auto lower = (frequency - left) / leftWidth;
            const auto upper = (right - frequency) / rightWidth;
            weights[static_cast<std::size_t>(band * frequencies + bin)]
                = std::max(0.0f, std::min(lower, upper)) * normalization;
        }
    }
    return weights;
}

std::pair<std::vector<float>, std::size_t> buildMel(const std::vector<float>& waveform,
                                                     NativeAnalyzer::Progress progress)
{
    const auto leftPad = static_cast<std::size_t>((windowSize - hop) / 2);
    const auto rightPad = static_cast<std::size_t>((windowSize - hop + 1) / 2);
    std::vector<float> padded(leftPad + waveform.size() + rightPad);
    for (std::size_t index = 0; index < padded.size(); ++index)
    {
        const auto source = static_cast<std::ptrdiff_t>(index)
            - static_cast<std::ptrdiff_t>(leftPad);
        padded[index] = waveform[reflectIndex(source, waveform.size())];
    }
    if (padded.size() < windowSize)
        return { std::vector<float>(melBands, std::log(1.0e-9f)), 1 };
    const auto frames = 1 + (padded.size() - windowSize) / hop;
    constexpr auto frequencies = fftSize / 2 + 1;
    const auto weights = melFilterbank();
    std::vector<float> window(windowSize);
    for (int index = 0; index < windowSize; ++index)
        window[static_cast<std::size_t>(index)] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * index / static_cast<float>(windowSize - 1));
    juce::dsp::FFT fft(10);
    std::vector<std::complex<float>> input(fftSize), spectrum(fftSize);
    std::vector<float> magnitude(frequencies);
    std::vector<float> mel(frames * melBands);
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        for (int index = 0; index < windowSize; ++index)
            input[static_cast<std::size_t>(index)] = {
                padded[frame * hop + static_cast<std::size_t>(index)]
                    * window[static_cast<std::size_t>(index)], 0.0f
            };
        fft.perform(input.data(), spectrum.data(), false);
        for (int bin = 0; bin < frequencies; ++bin)
            magnitude[static_cast<std::size_t>(bin)] = std::abs(
                spectrum[static_cast<std::size_t>(bin)]);
        for (int band = 0; band < melBands; ++band)
        {
            auto sum = 0.0f;
            const auto weightOffset = static_cast<std::size_t>(band * frequencies);
            for (int bin = 0; bin < frequencies; ++bin)
                sum += weights[weightOffset + static_cast<std::size_t>(bin)]
                    * magnitude[static_cast<std::size_t>(bin)];
            mel[frame * melBands + static_cast<std::size_t>(band)]
                = std::log(std::max(1.0e-9f, sum));
        }
        if (progress && frame % 32 == 0)
            progress(0.65 * static_cast<double>(frame) / std::max<std::size_t>(1, frames));
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
    static Ort::Env value(ORT_LOGGING_LEVEL_WARNING, "hachishifter-fcpe");
    return value;
}

struct Cache
{
    std::mutex mutex;
    juce::String path;
    int threads = 0;
    InferenceBackend inference = InferenceBackend::automatic;
    int deviceIndex = -1;
    juce::String activeInference { "cpu" };
    std::unique_ptr<Ort::Session> session;
};

Cache& cache()
{
    static Cache value;
    return value;
}

std::vector<FcpeFrame> run(const juce::File& modelFile,
                           std::vector<float> mel, std::size_t frames,
                           const OrtExecutionConfig& execution, juce::String& error,
                           NativeAnalyzer::Progress progress,
                           juce::String* activeInference)
{
    auto& shared = cache();
    std::scoped_lock lock(shared.mutex);
    const auto path = modelFile.getFullPathName();
    if (shared.session == nullptr || shared.path != path
        || shared.threads != execution.intraOpThreads
        || shared.inference != execution.requested
        || shared.deviceIndex != execution.deviceIndex)
    {
        auto options = makeOrtSessionOptions(execution, shared.activeInference);
        shared.session = std::make_unique<Ort::Session>(environment(),
            ortPath(modelFile).c_str(), options);
        shared.path = path;
        shared.threads = execution.intraOpThreads;
        shared.inference = execution.requested;
        shared.deviceIndex = execution.deviceIndex;
    }
    if (activeInference != nullptr) *activeInference = shared.activeInference;
    Ort::AllocatorWithDefaultOptions allocator;
    if (shared.session->GetInputCount() != 1 || shared.session->GetOutputCount() < 1)
    {
        error = "FCPE model must expose one mel input and one pitch output";
        return {};
    }
    const auto inputName = shared.session->GetInputNameAllocated(0, allocator);
    const auto outputName = shared.session->GetOutputNameAllocated(0, allocator);
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* inputNames[] { inputName.get() };
    const char* outputNames[] { outputName.get() };
    const auto centMinimum = 1'200.0 * std::log2(minimumHz / 10.0);
    const auto centMaximum = 1'200.0 * std::log2(maximumHz / 10.0);
    std::vector<FcpeFrame> result;
    result.reserve(frames);

    // Bound inference memory on long source files.  Context is evaluated on
    // both sides and discarded, so frames at a chunk boundary see the same
    // temporal neighbourhood as frames in a short, single-pass analysis.
    constexpr std::size_t coreFrames = 30'000; // five minutes
    constexpr std::size_t contextFrames = 128; // 1.28 seconds
    for (std::size_t coreStart = 0; coreStart < frames; coreStart += coreFrames)
    {
        const auto coreEnd = std::min(frames, coreStart + coreFrames);
        const auto inputStart = coreStart > contextFrames ? coreStart - contextFrames : 0;
        const auto inputEnd = std::min(frames, coreEnd + contextFrames);
        const auto inputFrames = inputEnd - inputStart;
        std::vector<float> chunk(mel.begin()
                + static_cast<std::ptrdiff_t>(inputStart * melBands),
            mel.begin() + static_cast<std::ptrdiff_t>(inputEnd * melBands));
        const std::array<int64_t, 3> shape {
            1, static_cast<int64_t>(inputFrames), melBands
        };
        auto input = Ort::Value::CreateTensor<float>(memory, chunk.data(), chunk.size(),
                                                      shape.data(), shape.size());
        auto output = shared.session->Run(Ort::RunOptions{ nullptr }, inputNames, &input, 1,
                                           outputNames, 1);
        if (output.empty())
        {
            error = "FCPE returned no pitch output";
            return {};
        }
        const auto info = output[0].GetTensorTypeAndShapeInfo();
        const auto dimensions = info.GetShape();
        if (dimensions.size() != 3 || dimensions[1] <= 0 || dimensions[2] <= 0)
        {
            error = "FCPE output is not [batch,time,bins]";
            return {};
        }
        const auto outputFrames = static_cast<std::size_t>(dimensions[1]);
        const auto bins = static_cast<std::size_t>(dimensions[2]);
        const auto* values = output[0].GetTensorData<float>();
        const auto keepOffset = coreStart - inputStart;
        const auto keepCount = std::min(coreEnd - coreStart,
            outputFrames > keepOffset ? outputFrames - keepOffset : 0);
        for (std::size_t kept = 0; kept < keepCount; ++kept)
        {
            const auto globalFrame = coreStart + kept;
            const auto* row = values + (keepOffset + kept) * bins;
            const auto best = static_cast<std::size_t>(std::distance(
                row, std::max_element(row, row + bins)));
            const auto confidence = row[best];
            FcpeFrame point;
            // The reflected STFT window is centred half a hop after the
            // nominal frame origin: 80 / 16 kHz = 5 ms.
            point.timeSeconds = (static_cast<double>(globalFrame * hop)
                + windowSize * 0.5 - (windowSize - hop) * 0.5) / sampleRate;
            point.confidence = confidence;
            point.voiced = std::isfinite(confidence) && confidence > 0.05f;
            if (point.voiced)
            {
                const auto first = best > 4 ? best - 4 : 0;
                const auto last = std::min(bins - 1, best + 4);
                auto weighted = 0.0;
                auto weight = 0.0;
                for (auto bin = first; bin <= last; ++bin)
                {
                    const auto cent = centMinimum + (centMaximum - centMinimum)
                        * static_cast<double>(bin) / std::max<std::size_t>(1, bins - 1);
                    weighted += cent * row[bin];
                    weight += row[bin];
                }
                if (weight > 1.0e-9)
                {
                    const auto hz = 10.0 * std::pow(2.0,
                        weighted / weight / 1'200.0);
                    point.midi = static_cast<float>(69.0
                        + 12.0 * std::log2(hz / 440.0));
                }
                else point.voiced = false;
            }
            result.push_back(point);
        }
        if (progress) progress(0.65 + 0.35 * static_cast<double>(coreEnd)
            / static_cast<double>(std::max<std::size_t>(1, frames)));
    }
    return result;
}
}
#endif

std::vector<FcpeFrame> FcpeAnalyzer::analyse(const juce::File& audioFile,
                                              const juce::File& modelFile,
                                              const OrtExecutionConfig& execution,
                                              juce::String& error,
                                              NativeAnalyzer::Progress progress,
                                              juce::String* activeInference)
{
#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
    try
    {
        auto audio = decodeMono(audioFile, error);
        if (audio.mono.empty()) return {};
        auto waveform = resample(audio.mono, audio.rate);
        if (waveform.empty())
        {
            error = "FCPE resampling produced no audio";
            return {};
        }
        auto [mel, frames] = buildMel(waveform, progress);
        auto result = run(modelFile, std::move(mel), frames,
                          execution, error, progress, activeInference);
        if (result.empty() && error.isEmpty()) error = "FCPE produced no F0 frames";
        return result;
    }
    catch (const Ort::Exception& exception)
    {
        error = "FCPE ONNX inference failed: " + juce::String(exception.what());
    }
    catch (const std::exception& exception)
    {
        error = "FCPE analysis failed: " + juce::String(exception.what());
    }
    return {};
#else
    juce::ignoreUnused(audioFile, modelFile, execution, progress, activeInference);
    error = "FCPE ONNX analysis runtime is not included";
    return {};
#endif
}
}
