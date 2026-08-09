#pragma once

#include "OrtExecution.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace hachi::backend
{
// Variable-hop Mel stretch order.  Mirrors the two compositions an editor can
// choose between: splice the per-segment stretched Mel first and pitch-shift
// afterwards (the HachiShifter variable-mel-hop default), or pitch-shift every
// source-time segment before joining them (Melodyne5's order: its frequency
// mask runs on each element before the Catmull-Rom time stretch and splice).
enum class NsfHifiganStretchOrder
{
    fixedHop,
    spliceThenShift,
    shiftThenSplice
};

struct NsfHifiganTimeMapPoint
{
    double targetSeconds = 0.0;
    double sourceSeconds = 0.0;
};

struct NsfHifiganRenderResult
{
    juce::AudioBuffer<float> buffer;
    juce::String error;
    juce::File modelFile;
    juce::String activeInference { "cpu" };
    bool usedModel = false;
};

// Per-edge amplitude guard applied after a neural decode.  Each edge fade is an
// equal-power sine ramp that suppresses the isolated discontinuity at a clip
// boundary whose source phase is unrelated to the next clip.  Connected phrase
// seams (which are decoded inside the same pass) and mixer-crossfaded seams do
// not need it, so the caller can zero the corresponding edge to avoid the
// small 3 ms level dip it would otherwise bake into every splice point.
struct NsfHifiganEdgeGuard
{
    float startSeconds = 0.003f;
    float endSeconds = 0.003f;
};

class NsfHifiganRenderer final
{
public:
    static NsfHifiganRenderResult render(
        const juce::AudioBuffer<float>& source,
        double sampleRate,
        int targetSamples,
        double framePeriodMs,
        const std::vector<float>& targetMidi,
        const std::vector<float>& formantSemitones,
        const std::vector<NsfHifiganTimeMapPoint>& timeMap,
        const juce::File& configuredModelDirectory,
        const OrtExecutionConfig& execution,
        NsfHifiganStretchOrder stretchOrder,
        bool normalizeVolume = false,
        const NsfHifiganEdgeGuard& edgeGuard = NsfHifiganEdgeGuard{});
};
}
