#pragma once

#include "I18n.h"
#include "ProjectModel.h"
#include <juce_gui_basics/juce_gui_basics.h>

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

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    ProjectModel& model;
    I18n& strings;
    ProjectData snapshot;
    int rowHeight = 96;
    juce::String volumeDragTrack;
    int volumeDragRow = -1;
};
}
