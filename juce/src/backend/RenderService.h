#pragma once

#include "Mld5Renderer.h"
#include "OrtExecution.h"
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

namespace hachi::backend
{
enum class PitchRenderBackend
{
    mld5 = 0,
    mld3 = 1,
    nsfHifigan = 2,
    world = 3,
    vslib = 4,
    llsm2 = 5
};

struct Mld5FileRenderRequest
{
    juce::File sourceFile;
    double sourceOffsetSeconds = 0.0;
    double sourceDurationSeconds = 0.0;
    double targetDurationSeconds = 0.0;
    double framePeriodMs = 5.0;
    std::vector<float> sourceMidi;
    std::vector<float> targetMidi;
    std::vector<float> formantSemitones;
    std::vector<float> noteGain;
    std::vector<float> tension;
    std::vector<float> breath;
    // Zero disables Robust Pitch Curve.  A positive value selects it and also
    // identifies the owning note, preventing filtering across note boundaries.
    // External callers may use 1.0 for one continuous robust region.
    std::vector<float> robustPitchCurve;
    std::vector<TimeMapPoint> timeMap;
    juce::File hifiganModelDirectory;
    OrtExecutionConfig inference;
    PitchRenderBackend pitchBackend = PitchRenderBackend::mld5;
    int stretchAlgorithm = 0;
    bool normalizeVolume = false;
    bool matchNsfSourceLevel = false;
    // Per-edge neural guard overrides for the nsf-hifigan backend.  A negative
    // value keeps the renderer default (3 ms both sides).  A connected seam
    // sets the matching edge to 0 so the mixer crossfade owns the hand-off
    // instead of a baked-in 3 ms level dip at every splice point.
    float neuralGuardStartSeconds = -1.0f;
    float neuralGuardEndSeconds = -1.0f;
    // Neighbor F0 at boundary edges for same-source pitch-split transitions.
    // When 0.0f the edge context uses normal reflection; a positive value
    // interpolates from/to the neighbor's actual F0 at that seam.
    float neighborEdgeF0Start = 0.0f;
    float neighborEdgeF0End = 0.0f;
};

struct RenderedAudio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
    juce::String backend;
};

class RenderService final
{
public:
    using Completion = std::function<void(juce::AudioBuffer<float>)>;
    using FileCompletion = std::function<void(RenderedAudio)>;

    RenderService();
    ~RenderService();
    void renderMld5(Mld5RenderRequest request, Completion completion);
    void renderMld5File(Mld5FileRenderRequest request, FileCompletion completion);
    void cancelAll();

private:
    class RenderJob;
    class FileRenderJob;
    juce::ThreadPool pool;
};
}
