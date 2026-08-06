#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Mld5Renderer.h"   // for TimeMapPoint

namespace hachi::backend
{
// Melodyne-3 independent renderer request.  M3 exposes independent pitch
// and formant multiplicative ratios (`_setPitchRatio` at this+0x5c,
// `_setFormantRatio` at this+0x60) and uses a period-locked overlap
// synthesis instead of the M5 spectral-envelope reconstruction.

struct Mld3RenderRequest
{
    const juce::AudioBuffer<float>* input = nullptr;
    double sampleRate = 48'000.0;
    double framePeriodMs = 5.0;
    std::vector<float> sourceMidi;
    std::vector<float> targetMidi;
    std::vector<float> formantSemitones;
    std::vector<float> noteGain;
    std::vector<TimeMapPoint> timeMap;
    int targetSamples = 0;
};

// PSOLA / period-transition renderer with a coarser synthesis clock than M5
// (72 ms analysis / 7.5 ms periodic step at the nominal rate, matching the
// configured `mld3` stretch clock in `RenderService` before this rewrite),
// independent multiplicative pitch and formant ratios, and per-note pitch
// transition adaptation.  This path is intentionally separate from
// `Mld5Renderer` so the two algorithms remain distinguishable.
class Mld3Renderer final
{
public:
    [[nodiscard]] juce::AudioBuffer<float> render(const Mld3RenderRequest& request) const;
};
} // namespace hachi::backend