#pragma once

#include "NativeAnalyzer.h"
#include "OrtExecution.h"
#include <juce_data_structures/juce_data_structures.h>

namespace hachi::backend
{
struct AnalysisConfig
{
    juce::File gameModelDirectory;
    juce::File fcpeModelPath;
    bool performanceMode = false;
    InferenceBackend inference = InferenceBackend::automatic;
    int deviceIndex = -1;
};

struct AnalysisStatus
{
    juce::String requestedBackend { "GAME+FCPE" };
    juce::String activeBackend { "native-hq" };
    juce::File gameModelDirectory;
    juce::File fcpeModelPath;
    bool gameModelReady = false;
    bool fcpeModelReady = false;
    bool onnxRuntimeReady = false;
    bool performanceMode = false;
    juce::String requestedInference { "auto" };
    juce::String activeInference { "cpu" };
    juce::String message;
};

struct AnalysisResult
{
    std::vector<NoteData> notes;
    AnalysisStatus status;
    juce::String warning;
};

// Single entry point for ordinary audio analysis, Melodyne source-F0
// reanalysis, CLI diagnostics and MCP.  Model-free packages deliberately keep
// NativeAnalyzer as a fallback; this service prevents UI settings from being
// silently ignored and provides one place for the ONNX GAME/FCPE engine.
class AnalysisService final
{
public:
    using Progress = NativeAnalyzer::Progress;

    static AnalysisConfig configFromProperties(const juce::PropertiesFile* properties);
    static AnalysisConfig configFromEnvironment();
    static AnalysisStatus status(const AnalysisConfig& config);
    static AnalysisResult analyse(const juce::File& file, const AnalysisConfig& config,
                                  juce::String& error, Progress progress = {});
    static bool reanalyseProjectSourcePitch(ProjectData& project,
                                            const AnalysisConfig& config,
                                            juce::String& error,
                                            Progress progress = {},
                                            AnalysisStatus* usedStatus = nullptr);
    static juce::String backendText(const AnalysisStatus& status);

private:
    static juce::File resolveGameDirectory(const AnalysisConfig& config);
    static juce::File resolveFcpePath(const AnalysisConfig& config,
                                      const juce::File& gameDirectory);
};
}
