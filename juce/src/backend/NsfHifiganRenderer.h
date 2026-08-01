#pragma once

#include "OrtExecution.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace hachi::backend
{
struct NsfHifiganRenderResult
{
    juce::AudioBuffer<float> buffer;
    juce::String error;
    juce::File modelFile;
    juce::String activeInference { "cpu" };
    bool usedModel = false;
};

class NsfHifiganRenderer final
{
public:
    static NsfHifiganRenderResult render(
        const juce::AudioBuffer<float>& source,
        double sampleRate,
        double framePeriodMs,
        const std::vector<float>& targetMidi,
        const juce::File& configuredModelDirectory,
        const OrtExecutionConfig& execution);
};
}
