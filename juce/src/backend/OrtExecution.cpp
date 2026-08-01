#include "OrtExecution.h"

#include <algorithm>

#if defined(HACHI_HAS_ORT_DML) && HACHI_HAS_ORT_DML
#include <dml_provider_factory.h>
#endif

namespace hachi::backend
{
juce::String inferenceBackendName(InferenceBackend backend)
{
    switch (backend)
    {
        case InferenceBackend::directML: return "directml";
        case InferenceBackend::cuda: return "cuda";
        case InferenceBackend::coreML: return "coreml";
        case InferenceBackend::cpu: return "cpu";
        case InferenceBackend::automatic: return "auto";
    }
    return "cpu";
}

bool inferenceBackendAvailable(InferenceBackend backend)
{
    if (backend == InferenceBackend::automatic || backend == InferenceBackend::cpu) return true;
#if defined(HACHI_HAS_ORT_DML) && HACHI_HAS_ORT_DML
    if (backend == InferenceBackend::directML) return true;
#endif
    return false;
}

InferenceBackend resolvedInferenceBackend(InferenceBackend requested)
{
    // Auto remains CPU for predictable startup on machines whose display
    // adapter is not suitable for long-running inference.  DirectML is used
    // when selected explicitly and is present in the Windows package.
    if (requested == InferenceBackend::automatic) return InferenceBackend::cpu;
    return inferenceBackendAvailable(requested) ? requested : InferenceBackend::cpu;
}

#if defined(HACHI_HAS_ONNX_ANALYSIS) && HACHI_HAS_ONNX_ANALYSIS
Ort::SessionOptions makeOrtSessionOptions(const OrtExecutionConfig& config,
                                          juce::String& activeBackend)
{
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    const auto resolved = resolvedInferenceBackend(config.requested);
    activeBackend = inferenceBackendName(resolved);
    if (resolved == InferenceBackend::directML)
    {
#if defined(HACHI_HAS_ORT_DML) && HACHI_HAS_ORT_DML
        // DirectML requires sequential execution and no memory-pattern arena.
        // Violating either constraint can terminate session creation on some
        // adapters, which was the cause of the former GPU import crash.
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        options.DisableMemPattern();
        const auto device = std::max(0, config.deviceIndex);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, device));
        activeBackend << ":gpu" << device;
#endif
    }
    else if (config.intraOpThreads > 0)
        options.SetIntraOpNumThreads(config.intraOpThreads);
    return options;
}
#endif
}
