#include "AnalysisService.h"
#include "GameAnalyzer.h"
#include <algorithm>
#include <array>

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
    result.gameModelDirectory = resolveGameDirectory(config);
    result.fcpeModelPath = resolveFcpePath(config, result.gameModelDirectory);
    result.gameModelReady = result.gameModelDirectory != juce::File{};
    result.fcpeModelReady = result.fcpeModelPath != juce::File{};
    result.onnxRuntimeReady = GameAnalyzer::runtimeAvailable();
    if (result.gameModelReady && result.onnxRuntimeReady)
    {
        result.activeBackend = "GAME+native-hq";
        result.message = "GAME " + juce::String(config.performanceMode ? "small" : "large")
            + (result.fcpeModelReady ? " ready; FCPE model ready"
                                     : " ready; FCPE model missing");
    }
    else
    {
        juce::StringArray missing;
        if (!result.gameModelReady) missing.add("GAME " + juce::String(
            config.performanceMode ? "small" : "large") + " model pack");
        if (!result.fcpeModelReady) missing.add("FCPE model");
        if (!result.onnxRuntimeReady) missing.add("ONNX analysis runtime");
        result.message = "native-hq fallback; missing: " + missing.joinIntoString(", ");
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
        juce::String gameError;
        result.notes = GameAnalyzer::analyse(file, result.status.gameModelDirectory,
                                             options, gameError, progress);
        if (!result.notes.empty())
        {
            result.status.activeBackend = "GAME+native-hq";
            if (!result.status.fcpeModelReady)
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
    auto current = status(config);
    current.activeBackend = "native-hq";
    if (usedStatus != nullptr) *usedStatus = current;
    return NativeAnalyzer::reanalyseProjectSourcePitch(project, error, std::move(progress));
}

juce::String AnalysisService::backendText(const AnalysisStatus& value)
{
    auto text = value.activeBackend;
    if (value.activeBackend.startsWith("GAME+"))
        text << " (GAME " << (value.performanceMode ? "small" : "large") << ")";
    return text;
}
}
