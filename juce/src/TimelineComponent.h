#pragma once

#include "ProjectModel.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <memory>
#include <unordered_map>

namespace hachi
{
class TimelineComponent final : public juce::Component,
                                private juce::ChangeListener,
                                private juce::Timer
{
public:
    explicit TimelineComponent(ProjectModel& modelToUse);
    ~TimelineComponent() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void setPixelsPerSecond(float value);
    void setPlayheadSeconds(double seconds);
    [[nodiscard]] int pixelForSeconds(double seconds) const;
    [[nodiscard]] double secondsForPixel(int pixel) const;
    std::function<void(double)> onSeek;
    std::function<void(const juce::String&)> onClipSelected;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
    [[nodiscard]] float timeToX(double seconds) const;

    struct ClipHit
    {
        juce::String id;
        juce::Rectangle<float> bounds;
        double startSeconds = 0.0;
    };

    ProjectModel& model;
    ProjectData snapshot;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 96 };
    std::unordered_map<std::string, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    std::vector<ClipHit> clipHits;
    float pixelsPerSecond = 140.0f;
    static constexpr int rulerHeight = 24;
    int rowHeight = 96;
    double playheadSeconds = 0.0;
    juce::String selectedClip;
    juce::String draggedClip;
    double draggedClipStart = 0.0;
    float dragAnchorX = 0.0f;
};
}
