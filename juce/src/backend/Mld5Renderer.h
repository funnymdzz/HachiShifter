#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace hachi::backend
{
struct Mld5RenderRequest
{
    const juce::AudioBuffer<float>* input = nullptr;
    double sampleRate = 48'000.0;
    double framePeriodMs = 5.0;
    std::vector<float> sourceMidi;
    std::vector<float> targetMidi;
};

class Mld5Renderer final
{
public:
    [[nodiscard]] juce::AudioBuffer<float> render(const Mld5RenderRequest& request) const;

private:
    static std::vector<float> renderMono(const float* input, int inputLength,
                                         double sampleRate, double framePeriodMs,
                                         const std::vector<float>& sourceMidi,
                                         const std::vector<float>& targetMidi);
};
}

