#pragma once

#include "NativeAnalyzer.h"
#include "OrtExecution.h"

namespace hachi::backend
{
struct FcpeFrame
{
    double timeSeconds = 0.0;
    float midi = 0.0f;
    float confidence = 0.0f;
    bool voiced = false;
};

class FcpeAnalyzer final
{
public:
    static std::vector<FcpeFrame> analyse(const juce::File& audioFile,
                                          const juce::File& modelFile,
                                          const OrtExecutionConfig& execution,
                                          juce::String& error,
                                          NativeAnalyzer::Progress progress = {},
                                          juce::String* activeInference = nullptr);
};
}
