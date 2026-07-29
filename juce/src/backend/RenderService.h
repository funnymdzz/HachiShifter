#pragma once

#include "Mld5Renderer.h"
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

namespace hachi::backend
{
struct Mld5FileRenderRequest
{
    juce::File sourceFile;
    double sourceOffsetSeconds = 0.0;
    double sourceDurationSeconds = 0.0;
    double targetDurationSeconds = 0.0;
    double framePeriodMs = 5.0;
    std::vector<float> sourceMidi;
    std::vector<float> targetMidi;
};

struct RenderedAudio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
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
