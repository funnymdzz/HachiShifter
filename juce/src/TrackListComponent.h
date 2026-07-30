#pragma once

#include "I18n.h"
#include "ProjectModel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace hachi
{
class TrackListComponent final : public juce::Component,
                                 private juce::ChangeListener
{
public:
    TrackListComponent(ProjectModel& modelToUse, I18n& stringsToUse);
    ~TrackListComponent() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    std::function<float(const juce::String&)> peakProvider;
    std::function<void(const juce::String&)> onTrackSelected;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    ProjectModel& model;
    I18n& strings;
    ProjectData snapshot;
    static constexpr int rulerHeight = 24;
    int rowHeight = 96;
    juce::String selectedTrack;
    juce::String volumeDragTrack;
    float volumeDragPreview = 1.0f;
    juce::String panDragTrack;
    float panDragPreview = 0.0f;
    int volumeDragRow = -1;
};
}
