#pragma once

#include <juce_core/juce_core.h>

#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
#include <onnxruntime_cxx_api.h>
#endif

namespace hachi::backend
{
enum class InferenceBackend
{
    automatic,
    cpu,
    directML,
    cuda,
    coreML
};

struct OrtExecutionConfig
{
    InferenceBackend requested = InferenceBackend::automatic;
    int deviceIndex = -1;
    int intraOpThreads = 0;
};

[[nodiscard]] juce::String inferenceBackendName(InferenceBackend backend);
[[nodiscard]] bool inferenceBackendAvailable(InferenceBackend backend);
[[nodiscard]] InferenceBackend resolvedInferenceBackend(InferenceBackend requested);

#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
[[nodiscard]] Ort::SessionOptions makeOrtSessionOptions(const OrtExecutionConfig& config,
                                                        juce::String& activeBackend);
#endif
}
