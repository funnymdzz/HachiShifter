#pragma once

#include "AudioEngine.h"
#include "I18n.h"
#include "PianoRollComponent.h"
#include "Theme.h"
#include "TimelineComponent.h"
#include "TrackListComponent.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace hachi
{
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void refreshTexts();
    void openProject();
    void saveProject();
    void importAudio();
    void importMelodyne();
    void setSourceEditMode(bool enabled);
    void showError(const juce::String& message);

    HachiLookAndFeel lookAndFeel;
    I18n strings;
    ProjectModel project;
    AudioEngine audio;

    juce::TextButton openButton;
    juce::TextButton saveButton;
    juce::TextButton audioButton;
    juce::TextButton melodyneButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton noteEditButton;
    juce::TextButton wrenchButton;
    juce::ComboBox pitchAlgorithm;
    juce::ComboBox stretchAlgorithm;
    juce::Label pitchLabel;
    juce::Label stretchLabel;
    juce::Label statusLabel;
    juce::Label sourceEditHint;
    juce::Slider zoomSlider;
    juce::ProgressBar progressBar;
    double progress = 0.0;

    TrackListComponent trackList;
    TimelineComponent timeline;
    PianoRollComponent pianoRoll;
    juce::Viewport timelineViewport;
    juce::Viewport pianoViewport;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
