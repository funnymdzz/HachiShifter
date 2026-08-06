#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace hachi::backend
{
// Project-level source/target time anchor.  Defined here (the lowest-level
// renderer header) so every renderer that needs a custom time warp shares the
// same value type instead of declaring its own private alias.

struct TimeMapPoint
{
    double targetSeconds = 0.0;
    double sourceSeconds = 0.0;
};

// Independent renderer request for the Melodyne-5 MULSS component engine.
// The fields here are the ones the M5 path consumes on its own: pitch-formant
// curves, formant offset, per-frame amplitude, time warp and target length.
// Tension and breath remain RenderService-level expression controls applied
// after the algorithm output (kept identical across all backends).

struct Mld5RenderRequest
{
    const juce::AudioBuffer<float>* input = nullptr;
    double sampleRate = 48'000.0;
    double framePeriodMs = 5.0;
    std::vector<float> sourceMidi;
    std::vector<float> targetMidi;
    std::vector<float> formantSemitones;
    std::vector<float> noteGain;
    std::vector<TimeMapPoint> timeMap;        // empty -> no time warp
    int targetSamples = 0;                     // 0 -> output length = input length
};

class Mld5Renderer final
{
public:
    // Melodyne-5 MULSS-style renderer: phase-preserving bandlimited-harmonic
    // spectral remap with formant-envelope compensation and granular hop time
    // warp.  Independent of every other pitch backend.
    [[nodiscard]] juce::AudioBuffer<float> render(const Mld5RenderRequest& request) const;
};
}

