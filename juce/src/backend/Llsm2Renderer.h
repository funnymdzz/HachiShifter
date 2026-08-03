#pragma once

#include "RenderService.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace hachi::backend
{
class Llsm2Renderer final
{
public:
    // Direct libllsm2 analysis/modification/synthesis path.  The source and
    // target pitch curves use the application's native target-time 5 ms grid;
    // timeMap selects the original analysis frame for each output frame.
    static juce::AudioBuffer<float> render(
        const juce::AudioBuffer<float>& source,
        int targetSamples,
        double sampleRate,
        double framePeriodMs,
        const std::vector<float>& sourceMidi,
        const std::vector<float>& targetMidi,
        const std::vector<float>& formantSemitones,
        const std::vector<float>& tension,
        const std::vector<TimeMapPoint>& timeMap);
};
}
