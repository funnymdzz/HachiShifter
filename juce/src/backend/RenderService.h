#pragma once

#include "Mld5Renderer.h"
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

namespace hachi::backend
{
class RenderService final
{
public:
    using Completion = std::function<void(juce::AudioBuffer<float>)>;

    RenderService();
    ~RenderService();
    void renderMld5(Mld5RenderRequest request, Completion completion);
    void cancelAll();

private:
    class RenderJob;
    juce::ThreadPool pool;
};
}

