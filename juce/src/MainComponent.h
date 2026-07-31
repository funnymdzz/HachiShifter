#pragma once

#include "AudioEngine.h"
#include "I18n.h"
#include "PianoRollComponent.h"
#include "SettingsComponent.h"
#include "SampleSettings.h"
#include "AssetManagerComponent.h"
#include "Theme.h"
#include "TimelineComponent.h"
#include "TrackListComponent.h"
#include "backend/MelodyneImporter.h"
#include "backend/NativeAnalyzer.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace hachi
{
class EditorViewport final : public juce::Viewport
{
public:
    std::function<bool(const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (onWheel && onWheel(event, wheel)) return;
        juce::Viewport::mouseWheelMove(event, wheel);
    }
};

class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer,
                            private juce::MenuBarModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void openExternalFile(const juce::File& file);
    bool keyPressed(const juce::KeyPress& key) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex,
                                    const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;
    void refreshTexts();
    void refreshProjectControls();
    void refreshSelectedNoteParameter();
    void applySelectedNoteParameter();
    void refreshStretchAlgorithmItems(int preferredId = 0);
    void setToolButton(juce::Button& selected);
    void togglePlayback();
    void openProject();
    void saveProject();
    void exportMixdown();
    void importAudio();
    void addAnalysedAudioFile(const juce::File& file, double durationSeconds,
                              double startSeconds = 0.0);
    void scheduleNativeAnalysis(const juce::File& file, const juce::String& clipId);
    void importMidi();
    void importMelodyne();
    void showSettings();
    void applyPreferences();
    void loadSampleSettings();
    void refreshSampleEditors();
    void commitSampleEditors();
    void saveSampleSettings();
    void importOto();
    void exportOto();
    void showAssetManager();
    void showClipGainDialog();
    void confirmDestructive(const juce::String& title, std::function<void()> action);
    void loadMelodyneFile(const juce::File& file);
    void presentMelodyneComposeSelection(backend::MelodyneImportResult imported);
    void focusClip(const juce::String& clipId);
    void setSourceEditMode(bool enabled);
    void showError(const juce::String& message);

    HachiLookAndFeel lookAndFeel;
    I18n strings;
    ProjectModel project;
    AudioEngine audio;

    juce::MenuBarComponent menuBar;
    juce::Label bpmCaption;
    juce::Label bpmEditor;
    juce::Label beatsCaption;
    juce::Label beatsEditor;
    juce::Label denominatorLabel;
    juce::Label gridCaption;
    juce::ComboBox gridSelector;
    juce::Label scaleCaption;
    juce::ComboBox scaleSelector;
    juce::TextButton openButton;
    juce::TextButton saveButton;
    juce::TextButton audioButton;
    juce::TextButton melodyneButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton noteEditButton;
    juce::TextButton wrenchButton;
    juce::TextButton drawButton;
    juce::TextButton lineButton;
    juce::TextButton connectButton;
    juce::TextButton pitchParamButton;
    juce::TextButton driftParamButton;
    juce::TextButton attackParamButton;
    juce::TextButton breathParamButton;
    juce::TextButton tensionParamButton;
    juce::TextButton formantParamButton;
    juce::TextButton volumeParamButton;
    juce::TextButton midiButton;
    juce::ComboBox pitchAlgorithm;
    juce::ComboBox stretchAlgorithm;
    juce::Label pitchLabel;
    juce::Label stretchLabel;
    juce::Label statusLabel;
    juce::Label sourceEditHint;
    juce::ComboBox sampleRegionSelector;
    juce::TextEditor sampleAliasEditor;
    juce::TextEditor sampleStartEditor;
    juce::TextEditor sampleEndEditor;
    juce::TextEditor sampleAlignmentEditor;
    juce::TextEditor sampleFixedEditor;
    juce::Label sampleAliasLabel, sampleStartLabel, sampleEndLabel,
                sampleAlignmentLabel, sampleFixedLabel;
    juce::TextButton sampleSaveButton, otoImportButton, otoExportButton;
    juce::Label parameterTitle;
    juce::Label smoothCaption;
    juce::Slider smoothSlider;
    juce::Slider zoomSlider;
    double progress = 0.0;
    juce::ProgressBar progressBar;
    bool importInProgress = false;
    bool showingRenderProgress = false;
    bool playWhenRenderReady = false;
    int pendingNativeAnalyses = 0;
    double nativeAnalysisProgress = 0.0;
    juce::String nativeAnalysisName;

    TrackListComponent trackList;
    TimelineComponent timeline;
    PianoRollComponent pianoRoll;
    juce::Viewport trackViewport;
    EditorViewport timelineViewport;
    EditorViewport pianoViewport;
    juce::Component panelSplitter;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::PropertiesFile> preferences;
    int lastTimelineX = 0;
    int lastPianoX = 0;
    int lastTimelineY = 0;
    int lastTrackY = 0;
    bool syncingScroll = false;
    bool pianoInitialScrollSet = false;
    bool sourceEditActive = false;
    juce::File sampleSettingsFile;
    std::vector<SampleRegionSetting> sampleSettingsRows;
    int activeSampleSetting = 0;
    juce::String selectedClipId;
    juce::String selectedTrackId;
    juce::String selectedNoteId;
    enum class ParameterMode { pitchSmooth, pitchDrift, attackSpeed, breath, tension, formant, volume };
    ParameterMode parameterMode = ParameterMode::pitchSmooth;
    bool updatingSmoothSlider = false;
    bool smoothSliderDragging = false;
    bool draggingPanelSplitter = false;
    int panelSplitterDragScreenY = 0;
    float panelSplitterDragRatio = 0.60f;
    float panelSplitRatio = 0.60f;
};
}
