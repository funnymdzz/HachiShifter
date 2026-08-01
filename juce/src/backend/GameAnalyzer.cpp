#include "GameAnalyzer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>

#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
#include <onnxruntime_cxx_api.h>
#endif

namespace hachi::backend
{
#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
namespace
{
constexpr std::size_t maxEncoderFrames = 5'000;

struct GameConfig
{
    int sampleRate = 16'000;
    float timestep = 0.01f;
    bool loop = true;
    std::size_t embeddingDimension = 256;
};

struct GameRegion
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float midi = 60.0f;
    bool rest = false;
};

struct DecodedAudio
{
    std::vector<float> mono;
    int sampleRate = 0;
};

DecodedAudio decodeMono(const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "GAME could not read audio: " + file.getFullPathName();
        return {};
    }
    const auto channels = std::max(1, static_cast<int>(reader->numChannels));
    const auto frames = static_cast<int>(std::min<juce::int64>(
        reader->lengthInSamples, std::numeric_limits<int>::max()));
    juce::AudioBuffer<float> buffer(channels, frames);
    if (!reader->read(&buffer, 0, frames, 0, true, true))
    {
        error = "GAME audio decode failed: " + file.getFullPathName();
        return {};
    }
    DecodedAudio decoded;
    decoded.sampleRate = static_cast<int>(std::llround(reader->sampleRate));
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

std::vector<float> resampleLinear(const std::vector<float>& input, int inputRate,
                                  int outputRate)
{
    if (input.empty() || inputRate <= 0 || outputRate <= 0) return {};
    if (inputRate == outputRate) return input;
    const auto ratio = static_cast<double>(outputRate) / inputRate;
    const auto outputSize = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::llround(static_cast<double>(input.size()) * ratio)));
    std::vector<float> output(outputSize);
    for (std::size_t index = 0; index < output.size(); ++index)
    {
        const auto source = static_cast<double>(index) / ratio;
        const auto left = std::min(input.size() - 1, static_cast<std::size_t>(source));
        const auto right = std::min(input.size() - 1, left + 1);
        const auto fraction = static_cast<float>(source - static_cast<double>(left));
        output[index] = input[left] + (input[right] - input[left]) * fraction;
    }
    return output;
}

GameConfig loadConfig(const juce::File& directory, juce::String& error)
{
    GameConfig config;
    const auto file = directory.getChildFile("config.json");
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (const auto* object = parsed.getDynamicObject())
    {
        config.sampleRate = static_cast<int>(object->getProperty("samplerate"));
        config.timestep = static_cast<float>(object->getProperty("timestep"));
        config.loop = static_cast<bool>(object->getProperty("loop"));
        const auto embedding = static_cast<int>(object->getProperty("embedding_dim"));
        if (embedding > 0) config.embeddingDimension = static_cast<std::size_t>(embedding);
    }
    if (config.sampleRate <= 0 || !std::isfinite(config.timestep) || config.timestep <= 0.0f)
        error = "GAME config.json contains an invalid samplerate or timestep";
    return config;
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
    static Ort::Env value(ORT_LOGGING_LEVEL_WARNING, "hachishifter-game");
    return value;
}

std::string normalize(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character)
    {
        return character == '_' || character == '-' || character == '.' || character == ' ';
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

struct Names
{
    std::vector<std::string> owned;
    std::vector<const char*> pointers;
};

Names inputNames(const Ort::Session& session)
{
    Ort::AllocatorWithDefaultOptions allocator;
    Names result;
    const auto count = session.GetInputCount();
    result.owned.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.owned.emplace_back(session.GetInputNameAllocated(index, allocator).get());
    result.pointers.reserve(count);
    for (const auto& name : result.owned) result.pointers.push_back(name.c_str());
    return result;
}

Names outputNames(const Ort::Session& session)
{
    Ort::AllocatorWithDefaultOptions allocator;
    Names result;
    const auto count = session.GetOutputCount();
    result.owned.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.owned.emplace_back(session.GetOutputNameAllocated(index, allocator).get());
    result.pointers.reserve(count);
    for (const auto& name : result.owned) result.pointers.push_back(name.c_str());
    return result;
}

bool alias(const std::string& actual, std::initializer_list<const char*> alternatives)
{
    const auto key = normalize(actual);
    return std::any_of(alternatives.begin(), alternatives.end(), [&](const char* candidate)
    {
        return key == normalize(candidate);
    });
}

template <typename Type>
Ort::Value tensor(Type* data, std::size_t count, std::initializer_list<int64_t> shape)
{
    static const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<int64_t> dimensions(shape);
    return Ort::Value::CreateTensor<Type>(memory, data, count,
                                          dimensions.data(), dimensions.size());
}

template <typename Type>
Ort::Value tensor(Type* data, std::size_t count, const std::vector<int64_t>& shape)
{
    static const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    return Ort::Value::CreateTensor<Type>(memory, data, count, shape.data(), shape.size());
}

struct TensorSpec
{
    std::vector<const char*> aliases;
    std::function<Ort::Value()> make;
};

std::vector<Ort::Value> runMapped(Ort::Session& session,
                                  const std::vector<TensorSpec>& specs,
                                  Names& outputs,
                                  const char* stage)
{
    const auto inputs = inputNames(session);
    std::vector<Ort::Value> values;
    values.reserve(inputs.owned.size());
    for (const auto& actual : inputs.owned)
    {
        const auto found = std::find_if(specs.begin(), specs.end(), [&](const TensorSpec& spec)
        {
            return std::any_of(spec.aliases.begin(), spec.aliases.end(), [&](const char* candidate)
            {
                return normalize(actual) == normalize(candidate);
            });
        });
        if (found == specs.end())
        {
            juce::StringArray names;
            for (const auto& name : inputs.owned) names.add(name);
            throw std::runtime_error(std::string(stage) + " has unsupported input '" + actual
                + "'; model inputs=[" + names.joinIntoString(", ").toStdString() + "]");
        }
        values.push_back(found->make());
    }
    outputs = outputNames(session);
    return session.Run(Ort::RunOptions{ nullptr }, inputs.pointers.data(), values.data(),
                       values.size(), outputs.pointers.data(), outputs.pointers.size());
}

Ort::Value* findOutput(std::vector<Ort::Value>& values, const Names& names,
                       std::initializer_list<const char*> aliases,
                       std::size_t fallback = std::numeric_limits<std::size_t>::max())
{
    for (std::size_t index = 0; index < names.owned.size() && index < values.size(); ++index)
        if (alias(names.owned[index], aliases)) return &values[index];
    if (fallback < values.size()) return &values[fallback];
    return nullptr;
}

template <typename Type>
std::vector<Type> copyTensor(const Ort::Value& value)
{
    const auto info = value.GetTensorTypeAndShapeInfo();
    const auto count = info.GetElementCount();
    const auto* data = value.GetTensorData<Type>();
    return std::vector<Type>(data, data + count);
}

std::vector<int64_t> shapeOf(const Ort::Value& value)
{
    return value.GetTensorTypeAndShapeInfo().GetShape();
}

struct Sessions
{
    Sessions(const juce::File& directory, int threads)
        : config(loadConfig(directory, configError)),
          options(makeOptions(threads)),
          encoder(environment(), ortPath(directory.getChildFile("encoder.onnx")).c_str(), options),
          segmenter(environment(), ortPath(directory.getChildFile("segmenter.onnx")).c_str(), options),
          estimator(environment(), ortPath(directory.getChildFile("estimator.onnx")).c_str(), options),
          bd2dur(environment(), ortPath(directory.getChildFile("bd2dur.onnx")).c_str(), options)
    {
        if (configError.isNotEmpty()) throw std::runtime_error(configError.toStdString());
    }

    static Ort::SessionOptions makeOptions(int threads)
    {
        Ort::SessionOptions result;
        result.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (threads > 0) result.SetIntraOpNumThreads(threads);
        return result;
    }

    juce::String configError;
    GameConfig config;
    Ort::SessionOptions options;
    Ort::Session encoder, segmenter, estimator, bd2dur;
};

std::vector<GameRegion> processChunk(Sessions& sessions,
                                     const std::vector<float>& waveform,
                                     std::size_t start, std::size_t end)
{
    const auto sampleCount = end - start;
    if (sampleCount == 0) return {};
    std::vector<float> samples(waveform.begin() + static_cast<std::ptrdiff_t>(start),
                               waveform.begin() + static_cast<std::ptrdiff_t>(end));
    auto duration = static_cast<float>(sampleCount) / sessions.config.sampleRate;
    Names encoderNames;
    auto encoderOutput = runMapped(sessions.encoder, {
        { { "waveform", "audio", "input", "samples" }, [&]
            { return tensor(samples.data(), samples.size(), { 1, static_cast<int64_t>(samples.size()) }); } },
        { { "duration", "length", "audio_duration" }, [&]
            { return tensor(&duration, 1, { 1 }); } }
    }, encoderNames, "GAME encoder");
    auto* xSegValue = findOutput(encoderOutput, encoderNames, { "x_seg", "xseg" });
    auto* xEstValue = findOutput(encoderOutput, encoderNames, { "x_est", "xest" });
    auto* maskTValue = findOutput(encoderOutput, encoderNames, { "maskT", "mask_t", "time_mask" });
    if (xSegValue == nullptr || xEstValue == nullptr || maskTValue == nullptr)
        throw std::runtime_error("GAME encoder outputs must contain x_seg, x_est and maskT");
    const auto xShape = shapeOf(*xSegValue);
    auto xSeg = copyTensor<float>(*xSegValue);
    auto xEst = copyTensor<float>(*xEstValue);
    auto maskT = copyTensor<bool>(*maskTValue);
    const auto timeFrames = xShape.size() > 1 && xShape[1] > 0
        ? static_cast<std::size_t>(xShape[1]) : maskT.size();
    const auto channels = xShape.size() > 2 && xShape[2] > 0
        ? static_cast<std::size_t>(xShape[2]) : sessions.config.embeddingDimension;
    if (timeFrames == 0 || channels == 0 || xSeg.size() < timeFrames * channels
        || xEst.size() < timeFrames * channels)
        throw std::runtime_error("GAME encoder returned invalid feature shapes");
    maskT.resize(timeFrames, false);

    std::unique_ptr<bool[]> previous(new bool[timeFrames]{});
    std::unique_ptr<bool[]> known(new bool[timeFrames]{});
    std::unique_ptr<bool[]> timeMask(new bool[timeFrames]{});
    for (std::size_t index = 0; index < timeFrames; ++index) timeMask[index] = maskT[index];
    const auto steps = sessions.config.loop ? 8 : 1;
    auto segmentationThreshold = 0.20f;
    int64_t radius = 2;
    int64_t language = 0;
    for (int step = 0; step < steps; ++step)
    {
        auto diffusionTime = sessions.config.loop
            ? static_cast<float>(step) / static_cast<float>(steps) : 0.0f;
        Names segmentNames;
        auto output = runMapped(sessions.segmenter, {
            { { "x_seg", "xseg" }, [&] { return tensor(xSeg.data(), timeFrames * channels,
                { 1, static_cast<int64_t>(timeFrames), static_cast<int64_t>(channels) }); } },
            { { "maskT", "mask_t", "time_mask" }, [&] { return tensor(timeMask.get(), timeFrames,
                { 1, static_cast<int64_t>(timeFrames) }); } },
            { { "known_boundaries", "knownboundaries" }, [&] { return tensor(known.get(), timeFrames,
                { 1, static_cast<int64_t>(timeFrames) }); } },
            { { "prev_boundaries", "previous_boundaries", "prevboundaries" }, [&]
                { return tensor(previous.get(), timeFrames, { 1, static_cast<int64_t>(timeFrames) }); } },
            { { "threshold", "segmentation_threshold" }, [&]
                { return tensor(&segmentationThreshold, 1, {}); } },
            { { "radius", "segmentation_radius" }, [&] { return tensor(&radius, 1, {}); } },
            { { "t", "time", "timestep" }, [&] { return tensor(&diffusionTime, 1, { 1 }); } },
            { { "language", "lang" }, [&] { return tensor(&language, 1, { 1 }); } }
        }, segmentNames, "GAME segmenter");
        auto* boundaries = findOutput(output, segmentNames, { "boundaries", "boundary", "output" }, 0);
        if (boundaries == nullptr) throw std::runtime_error("GAME segmenter returned no boundary output");
        const auto type = boundaries->GetTensorTypeAndShapeInfo().GetElementType();
        if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL)
        {
            const auto values = copyTensor<bool>(*boundaries);
            for (std::size_t index = 0; index < timeFrames; ++index)
                previous[index] = index < values.size() && values[index];
        }
        else
        {
            const auto values = copyTensor<float>(*boundaries);
            for (std::size_t index = 0; index < timeFrames; ++index)
                previous[index] = index < values.size() && values[index] >= 0.5f;
        }
    }

    Names durationNames;
    auto durationOutput = runMapped(sessions.bd2dur, {
        { { "boundaries", "boundary", "boundary_mask" }, [&]
            { return tensor(previous.get(), timeFrames, { 1, static_cast<int64_t>(timeFrames) }); } },
        { { "maskT", "mask_t", "time_mask" }, [&]
            { return tensor(timeMask.get(), timeFrames, { 1, static_cast<int64_t>(timeFrames) }); } }
    }, durationNames, "GAME boundary-to-duration");
    auto* durationsValue = findOutput(durationOutput, durationNames,
                                      { "durations", "duration", "output" }, 0);
    if (durationsValue == nullptr) throw std::runtime_error("GAME bd2dur returned no durations");
    auto durations = copyTensor<float>(*durationsValue);
    std::vector<bool> noteMask(durations.size(), true);
    if (auto* value = findOutput(durationOutput, durationNames, { "maskN", "mask_n", "note_mask" }))
        noteMask = copyTensor<bool>(*value);
    const auto noteCount = std::min(durations.size(), noteMask.size());
    if (noteCount == 0) return {};
    std::unique_ptr<bool[]> maskN(new bool[noteCount]{});
    for (std::size_t index = 0; index < noteCount; ++index) maskN[index] = noteMask[index];
    auto estimationThreshold = 0.20f;
    Names estimatorNames;
    auto estimatorOutput = runMapped(sessions.estimator, {
        { { "x_est", "xest" }, [&] { return tensor(xEst.data(), timeFrames * channels,
            { 1, static_cast<int64_t>(timeFrames), static_cast<int64_t>(channels) }); } },
        { { "maskT", "mask_t", "time_mask" }, [&] { return tensor(timeMask.get(), timeFrames,
            { 1, static_cast<int64_t>(timeFrames) }); } },
        { { "boundaries", "boundary", "boundary_mask" }, [&] { return tensor(previous.get(), timeFrames,
            { 1, static_cast<int64_t>(timeFrames) }); } },
        { { "maskN", "mask_n", "note_mask" }, [&] { return tensor(maskN.get(), noteCount,
            { 1, static_cast<int64_t>(noteCount) }); } },
        { { "threshold", "estimation_threshold" }, [&]
            { return tensor(&estimationThreshold, 1, { 1 }); } }
    }, estimatorNames, "GAME estimator");
    auto* scoresValue = findOutput(estimatorOutput, estimatorNames,
                                   { "scores", "score", "midi", "output" }, 0);
    if (scoresValue == nullptr) throw std::runtime_error("GAME estimator returned no scores");
    const auto scores = copyTensor<float>(*scoresValue);
    std::vector<bool> presence(scores.size(), true);
    if (auto* value = findOutput(estimatorOutput, estimatorNames,
                                 { "presence", "present", "voiced" }))
        presence = copyTensor<bool>(*value);
    auto finalMask = noteMask;
    if (auto* value = findOutput(estimatorOutput, estimatorNames,
                                 { "maskN", "mask_n", "note_mask" }))
        finalMask = copyTensor<bool>(*value);

    const auto count = std::min({ durations.size(), scores.size(), presence.size(), finalMask.size() });
    std::vector<GameRegion> regions;
    regions.reserve(count);
    auto cursor = static_cast<double>(start) / sessions.config.sampleRate;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!finalMask[index]) break;
        const auto noteDuration = std::max(0.0, static_cast<double>(durations[index]));
        const auto finish = cursor + noteDuration;
        if (noteDuration > 0.001 && std::isfinite(scores[index]))
            regions.push_back({ cursor, finish, scores[index], !presence[index] });
        cursor = finish;
    }
    return regions;
}

std::optional<std::pair<float, float>> pitchAt(const std::vector<NoteData>& notes,
                                                double seconds)
{
    for (const auto& note : notes)
    {
        const auto local = seconds - note.startSeconds;
        if (local < 0.0 || local > note.durationSeconds || note.contour.empty()) continue;
        const auto right = std::lower_bound(note.contour.begin(), note.contour.end(), local,
            [](const PitchPoint& point, double time) { return point.timeSeconds < time; });
        const auto rightIndex = right == note.contour.end() ? note.contour.size() - 1
            : static_cast<std::size_t>(std::distance(note.contour.begin(), right));
        const auto leftIndex = rightIndex > 0 && note.contour[rightIndex].timeSeconds > local
            ? rightIndex - 1 : rightIndex;
        const auto& left = note.contour[leftIndex];
        const auto& next = note.contour[rightIndex];
        if (!left.voiced || !next.voiced) return std::nullopt;
        const auto amount = next.timeSeconds > left.timeSeconds
            ? static_cast<float>(juce::jlimit(0.0, 1.0,
                (local - left.timeSeconds) / (next.timeSeconds - left.timeSeconds))) : 0.0f;
        const auto interpolate = [amount](float first, float second)
        {
            return first + (second - first) * amount;
        };
        const auto centre = note.sourceMidiCenter * 100.0f;
        return std::pair { centre + interpolate(left.relativeCents, next.relativeCents),
                           centre + interpolate(left.withoutVibratoCents,
                                                next.withoutVibratoCents) };
    }
    return std::nullopt;
}

float median(std::vector<float> values, float fallback)
{
    if (values.empty()) return fallback;
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

std::vector<NoteData> combineRegions(const std::vector<GameRegion>& regions,
                                     const std::vector<NoteData>& acoustic)
{
    std::vector<NoteData> notes;
    for (const auto& region : regions)
    {
        if (region.rest || region.endSeconds <= region.startSeconds) continue;
        NoteData note;
        note.id = "note_game_" + juce::Uuid().toString().removeCharacters("-");
        note.startSeconds = region.startSeconds;
        note.durationSeconds = region.endSeconds - region.startSeconds;
        note.midiNote = std::isfinite(region.midi)
            ? juce::jlimit(0.0f, 127.0f, region.midi) : 60.0f;
        std::vector<std::optional<std::pair<float, float>>> pitches;
        std::vector<float> absolute;
        for (double local = 0.0; local < note.durationSeconds + 0.0025; local += 0.005)
        {
            auto pitch = pitchAt(acoustic, note.startSeconds
                + std::min(local, note.durationSeconds));
            if (pitch) absolute.push_back(pitch->first);
            pitches.push_back(pitch);
        }
        const auto centreCents = median(absolute, note.midiNote * 100.0f);
        note.sourceMidiCenter = juce::jlimit(0.0f, 127.0f, centreCents / 100.0f);
        if (!std::isfinite(region.midi)) note.midiNote = note.sourceMidiCenter;
        auto firstVoiced = note.durationSeconds;
        for (std::size_t index = 0; index < pitches.size(); ++index)
        {
            PitchPoint point;
            point.timeSeconds = std::min(note.durationSeconds,
                                         static_cast<double>(index) * 0.005);
            point.voiced = pitches[index].has_value();
            if (pitches[index])
            {
                point.relativeCents = pitches[index]->first - centreCents;
                point.withoutVibratoCents = pitches[index]->second - centreCents;
                firstVoiced = std::min(firstVoiced, point.timeSeconds);
            }
            note.contour.push_back(point);
        }
        note.consonantSeconds = juce::jlimit(0.0, note.durationSeconds, firstVoiced);
        for (const auto& source : acoustic)
            for (const auto marker : source.sibilantMarkers)
            {
                const auto absoluteMarker = source.startSeconds + marker;
                if (absoluteMarker >= note.startSeconds && absoluteMarker <= region.endSeconds)
                    note.sibilantMarkers.push_back(absoluteMarker - note.startSeconds);
            }
        notes.push_back(std::move(note));
    }
    return notes;
}

struct SessionCache
{
    std::mutex mutex;
    juce::String path;
    int threads = 0;
    std::unique_ptr<Sessions> sessions;
};

SessionCache& cache()
{
    static SessionCache value;
    return value;
}
}
#endif

bool GameAnalyzer::runtimeAvailable()
{
#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
    return true;
#else
    return false;
#endif
}

std::vector<NoteData> GameAnalyzer::analyse(const juce::File& audioFile,
                                             const juce::File& modelDirectory,
                                             const Options& options,
                                             juce::String& error,
                                             NativeAnalyzer::Progress progress)
{
#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
    juce::String acousticError;
    auto acoustic = NativeAnalyzer::analyse(audioFile, acousticError, [&](double value)
    {
        if (progress) progress(value * 0.35);
    });
    auto decoded = decodeMono(audioFile, error);
    if (decoded.mono.empty()) return {};
    try
    {
        auto& shared = cache();
        std::scoped_lock lock(shared.mutex);
        const auto path = modelDirectory.getFullPathName();
        if (shared.sessions == nullptr || shared.path != path || shared.threads != options.intraOpThreads)
        {
            shared.sessions = std::make_unique<Sessions>(modelDirectory, options.intraOpThreads);
            shared.path = path;
            shared.threads = options.intraOpThreads;
        }
        auto waveform = resampleLinear(decoded.mono, decoded.sampleRate,
                                       shared.sessions->config.sampleRate);
        const auto samplesPerFrame = std::max<std::size_t>(1,
            static_cast<std::size_t>(std::llround(shared.sessions->config.sampleRate
                                                   * shared.sessions->config.timestep)));
        const auto chunkSamples = maxEncoderFrames * samplesPerFrame;
        std::vector<GameRegion> regions;
        for (std::size_t start = 0; start < waveform.size(); start += chunkSamples)
        {
            const auto end = std::min(waveform.size(), start + chunkSamples);
            auto chunk = processChunk(*shared.sessions, waveform, start, end);
            regions.insert(regions.end(), chunk.begin(), chunk.end());
            if (progress) progress(0.35 + 0.60 * static_cast<double>(end)
                / static_cast<double>(waveform.size()));
        }
        std::sort(regions.begin(), regions.end(), [](const auto& left, const auto& right)
        {
            return left.startSeconds < right.startSeconds;
        });
        auto notes = combineRegions(regions, acoustic);
        if (notes.empty()) error = "GAME produced no voiced syllable";
        else error.clear();
        if (progress) progress(1.0);
        return notes;
    }
    catch (const Ort::Exception& exception)
    {
        error = "GAME ONNX inference failed: " + juce::String(exception.what());
    }
    catch (const std::exception& exception)
    {
        error = "GAME inference failed: " + juce::String(exception.what());
    }
    return {};
#else
    juce::ignoreUnused(audioFile, modelDirectory, options, progress);
    error = "GAME ONNX analysis runtime is not included";
    return {};
#endif
}
}
