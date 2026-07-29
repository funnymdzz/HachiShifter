#pragma once

#include "ProjectModel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace hachi
{
class PianoRollComponent final : public juce::Component,
                                 private juce::ChangeListener
{
public:
    explicit PianoRollComponent(ProjectModel& modelToUse);
    ~PianoRollComponent() override;

    void setPixelsPerSecond(float value);
    void setSourceEditMode(bool enabled);
    void setPlayheadSeconds(double seconds);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    struct NoteHit
    {
        juce::String id;
        juce::Rectangle<float> bounds;
        float midi = 60.0f;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuildLayout();
    [[nodiscard]] float timeToX(double seconds) const;
    [[nodiscard]] float midiToY(float midi) const;
    [[nodiscard]] float yToMidi(float y) const;

    ProjectModel& model;
    ProjectData snapshot;
    std::vector<NoteHit> noteHits;
    float pixelsPerSecond = 140.0f;
    float rowHeight = 18.0f;
    int highestMidi = 96;
    int lowestMidi = 24;
    bool sourceEditMode = false;
    double playheadSeconds = 0.0;
    juce::String draggedNote;
    float dragStartMidi = 0.0f;
    float previewMidi = 0.0f;
};
}

