#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace hachi::backend
{
struct WorldTimeMapPoint
{
    double targetSeconds = 0.0;
    double sourceSeconds = 0.0;
};

class WorldRenderer final
{
public:
    static juce::AudioBuffer<float> render(
        const juce::AudioBuffer<float>& source,
        int targetSamples,
        double sampleRate,
        double framePeriodMs,
        const std::vector<float>& sourceMidi,
        const std::vector<float>& targetMidi,
        const std::vector<float>& formantSemitones,
        const std::vector<WorldTimeMapPoint>& timeMap);
};
}
