#include "AnalysisService.h"
#include "GameAnalyzer.h"
#include <algorithm>
#include <array>
#include <map>
#include <optional>

namespace hachi::backend
{
namespace
{
constexpr std::array<const char*, 5> gameFiles {
    "encoder.onnx", "segmenter.onnx", "estimator.onnx", "bd2dur.onnx", "config.json"
};

bool isGameDirectory(const juce::File& directory)
{
    if (!directory.isDirectory()) return false;
    return std::all_of(gameFiles.begin(), gameFiles.end(), [&directory](const char* name)
    {
        return directory.getChildFile(name).existsAsFile();
    });
}

juce::File executableDirectory()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory();
}

juce::File environmentFile(const char* name)
{
    const auto value = juce::SystemStats::getEnvironmentVariable(name, {}).trim()
        .unquoted();
    return value.isEmpty() ? juce::File{} : juce::File(value);
}

InferenceBackend inferenceFromId(int id)
{
    if (id == 2) return InferenceBackend::cpu;
    if (id == 3) return InferenceBackend::directML;
    if (id == 4) return InferenceBackend::cuda;
    if (id == 5) return InferenceBackend::coreML;
    return InferenceBackend::automatic;
}

std::optional<std::pair<float, float>> absolutePitchAt(
    const std::vector<NoteData>& notes, double sourceSeconds)
{
    for (const auto& note : notes)
    {
        const auto local = sourceSeconds - note.startSeconds;
        if (local < -1.0e-6 || local > note.durationSeconds + 1.0e-6
            || note.contour.empty())
            continue;
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

float median(std::vector<float> values)
{
    if (values.empty()) return 6'000.0f;
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}
}

AnalysisConfig AnalysisService::configFromProperties(const juce::PropertiesFile* properties)
{
    if (properties == nullptr) return configFromEnvironment();
    AnalysisConfig config;
    const auto game = properties->getValue("algorithm.gamePath").trim();
    const auto fcpe = properties->getValue("algorithm.fcpePath").trim();
    if (game.isNotEmpty()) config.gameModelDirectory = juce::File(game);
    if (fcpe.isNotEmpty()) config.fcpeModelPath = juce::File(fcpe);
    config.performanceMode = properties->getValue("algorithm.gameModel", "large") == "small";
    config.inference = inferenceFromId(properties->getIntValue("algorithm.inference", 1));
    const auto device = properties->getIntValue("algorithm.device", 1);
    config.deviceIndex = device <= 1 ? -1 : device - 2;
    return config;
}

AnalysisConfig AnalysisService::configFromEnvironment()
{
    AnalysisConfig config;
    config.performanceMode = juce::SystemStats::getEnvironmentVariable(
        "HACHISHIFTER_GAME_MODEL", "large").equalsIgnoreCase("small");
    config.gameModelDirectory = environmentFile(config.performanceMode
        ? "HACHISHIFTER_GAME_SMALL_MODEL_DIR" : "HACHISHIFTER_GAME_MODEL_DIR");
    auto fcpe = environmentFile("HACHISHIFTER_FCPE_ONNX");
    if (fcpe == juce::File{})
    {
        const auto directory = environmentFile("HACHISHIFTER_FCPE_MODEL_DIR");
        if (directory != juce::File{}) fcpe = directory.getChildFile("fcpe.onnx");
    }
    config.fcpeModelPath = fcpe;
    const auto inference = juce::SystemStats::getEnvironmentVariable(
        "HACHISHIFTER_INFERENCE", "auto").toLowerCase();
    config.inference = inference == "cpu" ? InferenceBackend::cpu
        : inference == "directml" ? InferenceBackend::directML
        : inference == "cuda" ? InferenceBackend::cuda
        : inference == "coreml" ? InferenceBackend::coreML
        : InferenceBackend::automatic;
    config.deviceIndex = juce::SystemStats::getEnvironmentVariable(
        "HACHISHIFTER_DEVICE", "-1").getIntValue();
    return config;
}

juce::File AnalysisService::resolveGameDirectory(const AnalysisConfig& config)
{
    const auto variant = config.performanceMode ? "small" : "large";
    const auto resolveRoot = [&](const juce::File& root) -> juce::File
    {
        if (root == juce::File{}) return {};
        if (isGameDirectory(root)) return root;
        auto candidate = root.getChildFile(variant);
        if (isGameDirectory(candidate)) return candidate;
        // Existing model packs commonly use models/game as the large folder
        // and models/game/small for performance mode.
        if (!config.performanceMode)
        {
            candidate = root.getChildFile("game");
            if (isGameDirectory(candidate)) return candidate;
            candidate = root.getChildFile("game").getChildFile("large");
            if (isGameDirectory(candidate)) return candidate;
        }
        else
        {
            candidate = root.getChildFile("game").getChildFile("small");
            if (isGameDirectory(candidate)) return candidate;
        }
        return {};
    };

    if (auto selected = resolveRoot(config.gameModelDirectory); selected != juce::File{})
        return selected;
    const auto portable = executableDirectory().getChildFile("models").getChildFile("game");
    if (auto selected = resolveRoot(portable); selected != juce::File{}) return selected;
    return {};
}

juce::File AnalysisService::resolveFcpePath(const AnalysisConfig& config,
                                             const juce::File& gameDirectory)
{
    const auto normalize = [](juce::File value)
    {
        if (value.isDirectory()) value = value.getChildFile("fcpe.onnx");
        return value.existsAsFile() ? value : juce::File{};
    };
    if (auto selected = normalize(config.fcpeModelPath); selected != juce::File{})
        return selected;
    if (gameDirectory != juce::File{})
    {
        auto parent = gameDirectory.getParentDirectory();
        if (parent.getFileName().equalsIgnoreCase("game")) parent = parent.getParentDirectory();
        if (auto selected = normalize(parent.getChildFile("fcpe")); selected != juce::File{})
            return selected;
    }
    return normalize(executableDirectory().getChildFile("models").getChildFile("fcpe"));
}

AnalysisStatus AnalysisService::status(const AnalysisConfig& config)
{
    AnalysisStatus result;
    result.performanceMode = config.performanceMode;
    result.requestedInference = inferenceBackendName(config.inference);
    result.activeInference = inferenceBackendName(resolvedInferenceBackend(config.inference));
    result.gameModelDirectory = resolveGameDirectory(config);
    result.fcpeModelPath = resolveFcpePath(config, result.gameModelDirectory);
    result.gameModelReady = result.gameModelDirectory != juce::File{};
    result.fcpeModelReady = result.fcpeModelPath != juce::File{};
    result.onnxRuntimeReady = GameAnalyzer::runtimeAvailable();
    if (result.gameModelReady && result.onnxRuntimeReady)
    {
        result.activeBackend = result.fcpeModelReady ? "GAME+FCPE" : "GAME+native-hq";
        result.message = "GAME " + juce::String(config.performanceMode ? "small" : "large")
            + (result.fcpeModelReady ? " ready; FCPE model ready"
                                     : " ready; FCPE model missing")
            + "; inference=" + result.activeInference;
    }
    else
    {
        juce::StringArray missing;
        if (!result.gameModelReady) missing.add("GAME " + juce::String(
            config.performanceMode ? "small" : "large") + " model pack");
        if (!result.fcpeModelReady) missing.add("FCPE model");
        if (!result.onnxRuntimeReady) missing.add("ONNX analysis runtime");
        result.message = "native-hq fallback; missing: " + missing.joinIntoString(", ");
        if (config.inference != resolvedInferenceBackend(config.inference))
            result.message << "; requested " << result.requestedInference
                           << " is not packaged, using CPU";
    }
    return result;
}

AnalysisResult AnalysisService::analyse(const juce::File& file,
                                        const AnalysisConfig& config,
                                        juce::String& error,
                                        Progress progress)
{
    AnalysisResult result;
    result.status = status(config);
    if (result.status.gameModelReady && result.status.onnxRuntimeReady)
    {
        GameAnalyzer::Options options;
        options.performanceMode = config.performanceMode;
        options.intraOpThreads = std::max(1, juce::SystemStats::getNumCpus());
        options.inference = config.inference;
        options.deviceIndex = config.deviceIndex;
        juce::String gameError;
        auto game = GameAnalyzer::analyse(file, result.status.gameModelDirectory,
                                          result.status.fcpeModelPath,
                                          options, gameError, progress);
        result.notes = std::move(game.notes);
        if (!result.notes.empty())
        {
            result.status.activeBackend = game.fcpeUsed ? "GAME+FCPE" : "GAME+native-hq";
            result.status.activeInference = game.activeInference;
            result.warning = game.warning;
            if (!game.fcpeUsed && !result.status.fcpeModelReady)
                result.warning = "FCPE model missing; GAME uses native-hq source F0";
            error.clear();
            return result;
        }
        result.warning = gameError;
    }
    result.notes = NativeAnalyzer::analyse(file, error, std::move(progress));
    result.status.activeBackend = "native-hq";
    if (result.warning.isEmpty() && result.status.gameModelReady
        && !result.status.onnxRuntimeReady)
        result.warning = result.status.message;
    return result;
}

bool AnalysisService::reanalyseProjectSourcePitch(ProjectData& project,
                                                   const AnalysisConfig& config,
                                                   juce::String& error,
                                                   Progress progress,
                                                   AnalysisStatus* usedStatus)
{
    std::map<juce::String, juce::File> files;
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
            if (clip.sourceFile.existsAsFile())
                files.try_emplace(clip.sourceFile.getFullPathName(), clip.sourceFile);
    if (files.empty())
    {
        error = "Source-pitch reanalysis has no readable media";
        return false;
    }
    std::map<juce::String, std::vector<NoteData>> analyses;
    juce::StringArray failures;
    auto fileIndex = std::size_t(0);
    AnalysisStatus current = status(config);
    for (const auto& [path, file] : files)
    {
        juce::String localError;
        auto analysed = analyse(file, config, localError, [&](double value)
        {
            if (progress) progress((static_cast<double>(fileIndex) + value)
                                   / static_cast<double>(files.size()));
        });
        if (!analysed.notes.empty())
        {
            current = analysed.status;
            analyses.emplace(path, std::move(analysed.notes));
        }
        else failures.add(file.getFileName() + ": " + localError);
        ++fileIndex;
    }
    auto updatedNotes = std::size_t(0);
    for (auto& track : project.tracks)
        for (auto& clip : track.clips)
        {
            const auto found = analyses.find(clip.sourceFile.getFullPathName());
            if (found == analyses.end()) continue;
            const auto sourceDuration = clip.sourceDurationSeconds > 1.0e-9
                ? clip.sourceDurationSeconds : clip.durationSeconds;
            for (auto& note : clip.notes)
            {
                if (note.contour.empty())
                {
                    for (double local = 0.0; local < note.durationSeconds; local += 0.005)
                    {
                        PitchPoint point;
                        point.timeSeconds = local;
                        note.contour.push_back(point);
                    }
                    PitchPoint end;
                    end.timeSeconds = note.durationSeconds;
                    note.contour.push_back(end);
                }
                std::vector<std::optional<std::pair<float, float>>> samples;
                samples.reserve(note.contour.size());
                std::vector<float> voicedPitch;
                for (const auto& point : note.contour)
                {
                    const auto clipLocal = note.startSeconds + point.timeSeconds;
                    const auto sourceSeconds = clip.sourceOffsetSeconds
                        + juce::jlimit(0.0, 1.0,
                            clipLocal / std::max(0.001, clip.durationSeconds)) * sourceDuration;
                    auto pitch = absolutePitchAt(found->second, sourceSeconds);
                    if (pitch) voicedPitch.push_back(pitch->first);
                    samples.push_back(pitch);
                }
                if (voicedPitch.size() < 2) continue;
                const auto centreCents = median(std::move(voicedPitch));
                note.sourceMidiCenter = juce::jlimit(0.0f, 127.0f, centreCents / 100.0f);
                for (std::size_t index = 0; index < note.contour.size(); ++index)
                {
                    auto& point = note.contour[index];
                    const auto& pitch = samples[index];
                    point.voiced = pitch.has_value();
                    if (!pitch) continue;
                    point.relativeCents = pitch->first - centreCents;
                    point.withoutVibratoCents = pitch->second - centreCents;
                }
                ++updatedNotes;
            }
        }
    if (usedStatus != nullptr) *usedStatus = current;
    if (progress) progress(1.0);
    if (!failures.isEmpty()) error = failures.joinIntoString("\n");
    if (updatedNotes == 0)
    {
        if (error.isEmpty()) error = "Source-pitch reanalysis produced no aligned contours";
        return false;
    }
    return true;
}

juce::String AnalysisService::backendText(const AnalysisStatus& value)
{
    auto text = value.activeBackend;
    if (value.activeBackend.startsWith("GAME+"))
        text << " (GAME " << (value.performanceMode ? "small" : "large")
             << ", " << value.activeInference << ")";
    return text;
}
}
