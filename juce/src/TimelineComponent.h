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
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void setPixelsPerSecond(float value);
    void setRowHeight(float value);
    void setPlayheadSeconds(double seconds);
    [[nodiscard]] int pixelForSeconds(double seconds) const;
    [[nodiscard]] double secondsForPixel(int pixel) const;
    [[nodiscard]] juce::String trackIdForPixel(int pixel) const;
    std::function<void(double)> onSeek;
    std::function<void(const juce::String&)> onClipSelected;
    std::function<void(const juce::String&)> onClipGainRequested;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
    [[nodiscard]] float timeToX(double seconds) const;
    [[nodiscard]] double gridSeconds() const;

    struct ClipHit
    {
        juce::String id;
        juce::Rectangle<float> bounds;
        double startSeconds = 0.0;
        double durationSeconds = 0.0;
        double fadeInSeconds = 0.0;
        double fadeOutSeconds = 0.0;
        bool muted = false;
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
    double draggedClipDuration = 0.0;
    double draggedClipPreviewStart = 0.0;
    double draggedClipPreviewDuration = 0.0;
    double draggedClipFadeIn = 0.0;
    double draggedClipFadeOut = 0.0;
    double draggedClipPreviewFadeIn = 0.0;
    double draggedClipPreviewFadeOut = 0.0;
    float dragAnchorX = 0.0f;
    enum class DragMode { none, move, resizeLeft, resizeRight, fadeIn, fadeOut }
        dragMode = DragMode::none;
};
}
