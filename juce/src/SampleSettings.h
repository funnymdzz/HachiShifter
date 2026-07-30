#pragma once

#include "ProjectModel.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace hachi
{
struct SampleRegionSetting
{
    juce::String name;
    double regionStartSeconds = 0.0;
    double regionEndSeconds = 0.5;
    double alignmentSeconds = 0.0;
    double fixedDurationSeconds = 0.0;
    double relativePitchCents = 0.0;
    bool melodyneData = false;
    double melodynePitchCenterCents = 0.0;
    double melodyneOriginalPitchCenterCents = 0.0;
    double melodynePitchDrift = 1.0;
    double melodynePitchModulation = 1.0;
    double melodyneTransitionSeconds = 0.0;
    double melodyneFormantCents = 0.0;
    double melodyneAmplitude = 1.0;
    double melodyneSibilantBalance = 0.0;
    double melodyneAttackSeconds = 0.0;
    double melodyneDecayElongation = 0.0;
};

class SampleSettings final
{
public:
    static juce::File sidecarFor(const juce::File& audio);
    static std::vector<SampleRegionSetting> loadOrDerive(const juce::File& audio,
                                                         const ProjectData& project);
    static bool save(const juce::File& audio, const std::vector<SampleRegionSetting>& rows,
                     juce::String& error);
    static bool importOto(const juce::File& oto, const juce::File& audio,
                          double audioDuration, std::vector<SampleRegionSetting>& rows,
                          juce::String& error);
    static bool exportOto(const juce::File& oto, const juce::File& audio,
                          const std::vector<SampleRegionSetting>& rows, double audioDuration,
                          juce::String& error);
};
}
