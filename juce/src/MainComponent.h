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
#include "backend/AnalysisService.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace hachi
{
class ZoomScrollBar final : public juce::ScrollBar
{
public:
    explicit ZoomScrollBar(bool isVertical) : juce::ScrollBar(isVertical) {}

    std::function<void(double)> onZoomChanged;

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto geo = getThumbGeometry();
        const auto pos = isVertical() ? event.position.y : event.position.x;
        resizeMode = ResizeMode::none;
        if (onZoomChanged != nullptr && geo.size > 10.0)
        {
            if (std::abs(pos - geo.start) <= 5.0)
                resizeMode = ResizeMode::leading;
            else if (std::abs(pos - (geo.start + geo.size)) <= 5.0)
                resizeMode = ResizeMode::trailing;
        }
        if (resizeMode == ResizeMode::none)
        {
            juce::ScrollBar::mouseDown(event);
            return;
        }
        fixedEdge = resizeMode == ResizeMode::leading ? geo.start + geo.size : geo.start;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (resizeMode == ResizeMode::none)
        {
            juce::ScrollBar::mouseDrag(event);
            return;
        }
        const auto length = isVertical() ? static_cast<double>(getHeight())
                                         : static_cast<double>(getWidth());
        const auto pos = isVertical() ? event.position.y : event.position.x;
        const auto minThumb = static_cast<double>(
            getLookAndFeel().getMinimumScrollbarThumbSize(*this));
        double newSize = resizeMode == ResizeMode::leading ? fixedEdge - pos : pos - fixedEdge;
        newSize = juce::jlimit(minThumb, length, newSize);
        if (onZoomChanged != nullptr && length > 0.0)
            onZoomChanged(newSize / length);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        resizeMode = ResizeMode::none;
        juce::ScrollBar::mouseUp(event);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        if (onZoomChanged == nullptr)
        {
            juce::ScrollBar::mouseMove(event);
            return;
        }
        const auto geo = getThumbGeometry();
        const auto pos = isVertical() ? event.position.y : event.position.x;
        const auto nearEdge = geo.size > 10.0
            && (std::abs(pos - geo.start) <= 5.0
                || std::abs(pos - (geo.start + geo.size)) <= 5.0);
        setMouseCursor(nearEdge ? (isVertical() ? juce::MouseCursor::UpDownResizeCursor
                                                : juce::MouseCursor::LeftRightResizeCursor)
                                : juce::MouseCursor::NormalCursor);
    }

    void paint(juce::Graphics& g) override
    {
        juce::ScrollBar::paint(g);
        if (onZoomChanged == nullptr) return;
        const auto geo = getThumbGeometry();
        if (geo.size < 10.0) return;
        g.setColour(Palette::accentLight.withAlpha(0.85f));
        if (isVertical())
        {
            const auto x = static_cast<float>(getWidth()) * 0.5f;
            g.drawVerticalLine(static_cast<float>(geo.start) + 2.0f, x - 3.0f, x + 3.0f);
            g.drawVerticalLine(static_cast<float>(geo.start + geo.size) - 2.0f, x - 3.0f, x + 3.0f);
        }
        else
        {
            const auto y = static_cast<float>(getHeight()) * 0.5f;
            g.drawHorizontalLine(y, static_cast<float>(geo.start) + 2.0f,
                                 static_cast<float>(geo.start) + 7.0f);
            g.drawHorizontalLine(y, static_cast<float>(geo.start + geo.size) - 7.0f,
                                 static_cast<float>(geo.start + geo.size) - 2.0f);
        }
    }

private:
    struct ThumbGeometry
    {
        double start;
        double size;
    };

    ThumbGeometry getThumbGeometry()
    {
        const auto length = isVertical() ? static_cast<double>(getHeight())
                                         : static_cast<double>(getWidth());
        const auto total = getRangeLimit();
        const auto visible = getCurrentRange();
        const auto minThumb = static_cast<double>(
            getLookAndFeel().getMinimumScrollbarThumbSize(*this));
        double size = length;
        if (total.getLength() > 0.0 && visible.getLength() < total.getLength())
        {
            size = visible.getLength() * length / total.getLength();
            if (size < minThumb) size = juce::jmin(minThumb, length - 1.0);
            if (size > length) size = length;
        }
        double start = 0.0;
        if (total.getLength() > visible.getLength())
            start = (visible.getStart() - total.getStart()) * (length - size)
                    / (total.getLength() - visible.getLength());
        start = juce::jmax(0.0, juce::jmin(length - size, start));
        return { start, size };
    }

    enum class ResizeMode { none, leading, trailing };
    ResizeMode resizeMode = ResizeMode::none;
    double fixedEdge = 0.0;
};

class EditorViewport : public juce::Viewport
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

class ZoomViewport final : public EditorViewport
{
public:
    juce::ScrollBar* createScrollBarComponent(bool isVertical) override
    {
        return new ZoomScrollBar(isVertical);
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
    void requestClose(std::function<void()> approved);
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
    void refreshRenderOrderItems(int preferredId = 0);
    void setToolButton(juce::Button& selected);
    void togglePlayback();
    void newProject();
    void openProject();
    void loadProjectFile(const juce::File& file);
    void saveProject(std::function<void(bool)> completion = {});
    void saveProjectAs(std::function<void(bool)> completion = {});
    bool saveProjectTo(const juce::File& file);
    void performWithUnsavedCheck(std::function<void()> action);
    void addRecentProject(const juce::File& file);
    void restoreRecentProjects();
    void exportMixdown();
    void importAudio();
    void addAnalysedAudioFile(const juce::File& file, double durationSeconds,
                              double startSeconds = 0.0,
                              const juce::String& targetTrackId = {});
    void scheduleAnalysis(const juce::File& file, const juce::String& clipId);
    void importMidi();
    void importMelodyne();
    void showSettings();
    void applyPreferences();
    void applyUiScale();
    void loadSampleSettings();
    void refreshSampleEditors();
    void commitSampleEditors();
    void saveSampleSettings();
    void importOto();
    void exportOto();
    void showAssetManager();
    void showClipGainDialog();
    void showRenameTrackDialog();
    void showTransposeNotesDialog();
    void showSetNotesPitchDialog();
    void copySelectedNotes(bool cut);
    void pasteCopiedNotes();
    void copySelectedClip();
    void pasteCopiedClip();
    void duplicateSelectedClip();
    void confirmDestructive(const juce::String& title, std::function<void()> action);
    void loadMelodyneFile(const juce::File& file);
    void presentMelodyneComposeSelection(backend::MelodyneImportResult imported);
    void focusClip(const juce::String& clipId);
    void focusNote(const juce::String& noteId);
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
    juce::TextButton splitButton;
    juce::TextButton pitchParamButton;
    juce::TextButton driftParamButton;
    juce::TextButton attackParamButton;
    juce::TextButton breathParamButton;
    juce::TextButton tensionParamButton;
    juce::TextButton formantParamButton;
    juce::TextButton volumeParamButton;
    juce::ToggleButton robustPitchCurveButton;
    juce::ComboBox pitchAlgorithm;
    juce::ComboBox stretchAlgorithm;
    juce::ComboBox renderOrder;
    juce::Label pitchLabel;
    juce::Label stretchLabel;
    juce::Label renderOrderLabel;
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
    juce::Slider vZoomSlider;
    bool showWaveforms = true;
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
    ZoomViewport pianoViewport;
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
    juce::String copiedClipId;
    std::vector<NoteData> copiedNotes;
    juce::File currentProjectFile;
    juce::StringArray recentProjectPaths;
    std::uint64_t savedProjectRevision = 0;
    enum class ParameterMode { pitchSmooth, pitchDrift, attackSpeed, breath, tension, formant, volume };
    ParameterMode parameterMode = ParameterMode::pitchSmooth;
    bool updatingSmoothSlider = false;
    bool updatingRobustPitchCurve = false;
    bool smoothSliderDragging = false;
    bool draggingPanelSplitter = false;
    int panelSplitterDragScreenY = 0;
    float panelSplitterDragRatio = 0.60f;
    float panelSplitRatio = 0.60f;
};
}
