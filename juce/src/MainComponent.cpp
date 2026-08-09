#include "MainComponent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <thread>

namespace hachi
{
namespace
{
class ComposeTrackSelector final : public juce::Component
{
public:
    ComposeTrackSelector(const std::vector<TrackData>& tracks, const I18n& strings)
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(9);
        addAndMakeVisible(viewport);
        for (const auto& track : tracks)
        {
            auto toggle = std::make_unique<juce::ToggleButton>();
            toggle->setButtonText(track.name + "  ·  "
                                  + strings.text(track.compose ? "track.compose" : "track.audio"));
            toggle->setToggleState(track.compose, juce::dontSendNotification);
            content.addAndMakeVisible(*toggle);
            toggles.push_back(std::move(toggle));
        }
        setSize(430, juce::jlimit(56, 320, static_cast<int>(toggles.size()) * rowHeight + 4));
    }

    bool isCompose(std::size_t index) const
    {
        return index < toggles.size() && toggles[index]->getToggleState();
    }

    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        const auto contentHeight = std::max(getHeight(), static_cast<int>(toggles.size()) * rowHeight + 4);
        content.setSize(std::max(1, getWidth() - viewport.getScrollBarThickness()), contentHeight);
        for (std::size_t index = 0; index < toggles.size(); ++index)
            toggles[index]->setBounds(6, 2 + static_cast<int>(index) * rowHeight,
                                      content.getWidth() - 12, rowHeight);
    }

private:
    static constexpr int rowHeight = 28;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
};
}

MainComponent::MainComponent()
    : menuBar(this), progressBar(progress), trackList(project, strings), timeline(project), pianoRoll(project)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "HachiShifterNext";
    options.filenameSuffix = "settings";
    options.folderName = "HachiShifterNext";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    preferences = std::make_unique<juce::PropertiesFile>(options);
    restoreRecentProjects();
    savedProjectRevision = project.revisionNumber();
    audio.restoreDeviceState(*preferences);
    applyPreferences();
    menuItemsChanged();
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);
    setWantsKeyboardFocus(true);

    for (auto* button : { &openButton, &saveButton, &audioButton, &melodyneButton,
                          &playButton, &stopButton, &noteEditButton, &wrenchButton,
                          &drawButton, &lineButton, &connectButton, &pitchParamButton,
                          &driftParamButton, &attackParamButton,
                          &breathParamButton, &tensionParamButton, &formantParamButton,
                          &volumeParamButton })
        addAndMakeVisible(*button);
    for (auto* component : { static_cast<juce::Component*>(&menuBar),
                             static_cast<juce::Component*>(&bpmCaption),
                             static_cast<juce::Component*>(&bpmEditor),
                             static_cast<juce::Component*>(&beatsCaption),
                             static_cast<juce::Component*>(&beatsEditor),
                             static_cast<juce::Component*>(&denominatorLabel),
                             static_cast<juce::Component*>(&gridCaption),
                             static_cast<juce::Component*>(&gridSelector),
                             static_cast<juce::Component*>(&scaleCaption),
                             static_cast<juce::Component*>(&scaleSelector),
                             static_cast<juce::Component*>(&pitchAlgorithm),
                             static_cast<juce::Component*>(&stretchAlgorithm),
                             static_cast<juce::Component*>(&pitchLabel),
                             static_cast<juce::Component*>(&stretchLabel),
                             static_cast<juce::Component*>(&statusLabel),
                             static_cast<juce::Component*>(&sourceEditHint),
                             static_cast<juce::Component*>(&sampleRegionSelector),
                             static_cast<juce::Component*>(&sampleAliasEditor),
                             static_cast<juce::Component*>(&sampleStartEditor),
                             static_cast<juce::Component*>(&sampleEndEditor),
                             static_cast<juce::Component*>(&sampleAlignmentEditor),
                             static_cast<juce::Component*>(&sampleFixedEditor),
                             static_cast<juce::Component*>(&sampleAliasLabel),
                             static_cast<juce::Component*>(&sampleStartLabel),
                             static_cast<juce::Component*>(&sampleEndLabel),
                             static_cast<juce::Component*>(&sampleAlignmentLabel),
                             static_cast<juce::Component*>(&sampleFixedLabel),
                             static_cast<juce::Component*>(&sampleSaveButton),
                             static_cast<juce::Component*>(&otoImportButton),
                             static_cast<juce::Component*>(&otoExportButton),
                             static_cast<juce::Component*>(&parameterTitle),
                             static_cast<juce::Component*>(&smoothCaption),
                              static_cast<juce::Component*>(&smoothSlider),
                              static_cast<juce::Component*>(&zoomBar),
                              static_cast<juce::Component*>(&vZoomBar),
                              static_cast<juce::Component*>(&progressBar),
                             static_cast<juce::Component*>(&trackViewport),
                             static_cast<juce::Component*>(&timelineViewport),
                             static_cast<juce::Component*>(&pianoViewport),
                             static_cast<juce::Component*>(&panelSplitter) })
        addAndMakeVisible(*component);

    panelSplitter.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    panelSplitter.addMouseListener(this, false);
    zoomBar.setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    zoomBar.addMouseListener(this, false);
    vZoomBar.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    vZoomBar.addMouseListener(this, false);

    for (auto* editor : { &sampleAliasEditor, &sampleStartEditor, &sampleEndEditor,
                          &sampleAlignmentEditor, &sampleFixedEditor })
        editor->setSelectAllWhenFocused(true);
    for (auto* editor : { &sampleStartEditor, &sampleEndEditor,
                          &sampleAlignmentEditor, &sampleFixedEditor })
        editor->setInputRestrictions(14, "0123456789.-");
    sampleRegionSelector.onChange = [this]
    {
        commitSampleEditors();
        activeSampleSetting = std::max(0, sampleRegionSelector.getSelectedItemIndex());
        refreshSampleEditors();
    };
    const auto commit = [this] { commitSampleEditors(); };
    sampleAliasEditor.onFocusLost = commit;
    sampleStartEditor.onFocusLost = commit;
    sampleEndEditor.onFocusLost = commit;
    sampleAlignmentEditor.onFocusLost = commit;
    sampleFixedEditor.onFocusLost = commit;
    sampleSaveButton.onClick = [this] { saveSampleSettings(); };
    otoImportButton.onClick = [this] { importOto(); };
    otoExportButton.onClick = [this] { exportOto(); };
    pianoRoll.onSampleRegionEdited = [this](int index, const SampleRegionSetting& row, bool)
    {
        if (index < 0 || index >= static_cast<int>(sampleSettingsRows.size())) return;
        activeSampleSetting = index;
        sampleSettingsRows[static_cast<std::size_t>(index)] = row;
        sampleRegionSelector.setSelectedItemIndex(index, juce::dontSendNotification);
        refreshSampleEditors();
    };

    openButton.setComponentID("icon.open");
    saveButton.setComponentID("icon.save");
    audioButton.setComponentID("icon.audio");
    melodyneButton.setComponentID("icon.melodyne");
    playButton.setComponentID("icon.play");
    stopButton.setComponentID("icon.stop");
    noteEditButton.setComponentID("icon.pointer");
    drawButton.setComponentID("icon.draw");
    lineButton.setComponentID("icon.line");
    wrenchButton.setComponentID("icon.wrench");
    connectButton.setComponentID("icon.connect");

    bpmEditor.setEditable(true, false, false);
    beatsEditor.setEditable(true, false, false);
    for (auto* editor : { &bpmEditor, &beatsEditor })
    {
        editor->setJustificationType(juce::Justification::centred);
        editor->setColour(juce::Label::backgroundColourId, Palette::background);
        editor->setColour(juce::Label::outlineColourId, Palette::grid);
    }
    bpmEditor.onTextChange = [this]
    {
        const auto value = bpmEditor.getText().getDoubleValue();
        if (value >= 20.0 && value <= 400.0)
        {
            const auto data = project.snapshot();
            project.setTempo(value, data.numerator, data.denominator);
        }
    };
    beatsEditor.onTextChange = [this]
    {
        const auto value = beatsEditor.getText().getIntValue();
        if (value >= 1 && value <= 32)
        {
            const auto data = project.snapshot();
            project.setTempo(data.bpm, value, data.denominator);
        }
    };

    for (const auto& value : { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
                               "1/4.", "1/8.", "1/16.", "1/4t", "1/8t", "1/16t" })
        gridSelector.addItem(value, gridSelector.getNumItems() + 1);
    for (const auto& value : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" })
        scaleSelector.addItem(value, scaleSelector.getNumItems() + 1);
    gridSelector.onChange = [this] { project.setGridDivision(gridSelector.getText()); };
    scaleSelector.onChange = [this] { project.setBaseScale(scaleSelector.getText()); };

    trackViewport.setViewedComponent(&trackList, false);
    trackViewport.setScrollBarsShown(true, false);
    trackViewport.setScrollBarThickness(10);
    timelineViewport.setViewedComponent(&timeline, false);
    timelineViewport.setScrollBarsShown(true, true);
    timelineViewport.setScrollBarThickness(10);
    pianoViewport.setViewedComponent(&pianoRoll, false);
    pianoViewport.setScrollBarsShown(true, true);
    pianoViewport.setScrollBarThickness(10);
    const auto editorWheel = [this](const juce::MouseEvent& event,
                                    const juce::MouseWheelDetails& wheel)
    {
        if (preferences == nullptr
            || preferences->getIntValue("operation.wheelAction", 1) != 1)
            return false;
        auto delta = wheel.deltaY;
        if (wheel.isReversed) delta = -delta;
        const auto factor = std::exp(static_cast<double>(delta) * 1.35);
        if (event.mods.isCommandDown())
        {
            const auto hZoom = juce::jlimit(40.0, 600.0, zoomSlider.getValue() * factor);
            const auto vZoom = juce::jlimit(0.5, 2.0, vZoomSlider.getValue() * factor);
            zoomSlider.setValue(hZoom);
            vZoomSlider.setValue(vZoom);
            return true;
        }
        if (event.mods.isShiftDown())
        {
            vZoomSlider.setValue(juce::jlimit(0.5, 2.0,
                vZoomSlider.getValue() * factor));
            return true;
        }
        zoomSlider.setValue(juce::jlimit(40.0, 600.0,
            zoomSlider.getValue() * factor));
        return true;
    };
    timelineViewport.onWheel = editorWheel;
    pianoViewport.onWheel = editorWheel;

    pitchAlgorithm.addItem("mld5", 1);
    pitchAlgorithm.addItem("nsf-hifigan", 2);
    pitchAlgorithm.addItem("WORLD", 3);
    pitchAlgorithm.addItem("vslib", 4);
    pitchAlgorithm.addItem("mld3", 5);
    pitchAlgorithm.addItem("llsm2", 6);
    pitchAlgorithm.setSelectedId(1);
    refreshStretchAlgorithmItems(1);
    pitchAlgorithm.onChange = [this]
    {
        const auto id = pitchAlgorithm.getSelectedId();
        const auto previousStretch = stretchAlgorithm.getSelectedId();
        refreshStretchAlgorithmItems(previousStretch);
        const auto algorithm = id == 2 ? PitchAlgorithm::nsfHifigan
            : id == 3 ? PitchAlgorithm::world
            : id == 4 ? PitchAlgorithm::vocalShifter
            : id == 5 ? PitchAlgorithm::mld3
            : id == 6 ? PitchAlgorithm::llsm2 : PitchAlgorithm::mld5;
        const auto data = project.snapshot();
        const auto selectedTrack = std::find_if(data.tracks.begin(), data.tracks.end(),
            [this](const auto& track)
            {
                return track.id == selectedTrackId;
            });
        if (selectedTrack != data.tracks.end())
            project.setTrackPitchAlgorithm(selectedTrack->id, algorithm);
        else
            project.setPitchAlgorithm(algorithm);
        if (previousStretch != stretchAlgorithm.getSelectedId())
        {
            if (selectedTrack != data.tracks.end())
                project.setTrackStretchAlgorithm(selectedTrack->id,
                                                  StretchAlgorithm::melodyneHybrid);
            else
                project.setStretchAlgorithm(StretchAlgorithm::melodyneHybrid);
        }
        refreshSelectedNoteParameter();
        resized();
    };
    stretchAlgorithm.onChange = [this]
    {
        const auto id = stretchAlgorithm.getSelectedId();
        const auto algorithm = id == 2 ? StretchAlgorithm::variableMelHop
            : id == 3 ? StretchAlgorithm::loop
            : id == 4 ? StretchAlgorithm::soundTouch
            : id == 5 ? StretchAlgorithm::nsfShiftThenSplice
            : StretchAlgorithm::melodyneHybrid;
        const auto data = project.snapshot();
        const auto selectedTrack = std::find_if(data.tracks.begin(), data.tracks.end(),
            [this](const auto& track)
            {
                return track.id == selectedTrackId;
            });
        if (selectedTrack != data.tracks.end())
            project.setTrackStretchAlgorithm(selectedTrack->id, algorithm);
        else
            project.setStretchAlgorithm(algorithm);
    };

    smoothSlider.setRange(0.0, 100.0, 1.0);
    smoothSlider.setValue(0.0);
    smoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    smoothSlider.onValueChange = [this]
    {
        if (updatingSmoothSlider || smoothSliderDragging || selectedNoteId.isEmpty()
            || !smoothSlider.isEnabled()) return;
        applySelectedNoteParameter();
    };
    smoothSlider.onDragStart = [this] { smoothSliderDragging = true; };
    smoothSlider.onDragEnd = [this]
    {
        smoothSliderDragging = false;
        if (selectedNoteId.isNotEmpty() && smoothSlider.isEnabled())
            applySelectedNoteParameter();
    };
    robustPitchCurveButton.onClick = [this]
    {
        if (updatingRobustPitchCurve || selectedNoteId.isEmpty()
            || !robustPitchCurveButton.isEnabled()) return;
        project.setNoteRobustPitchCurve(selectedNoteId,
            robustPitchCurveButton.getToggleState());
    };

    zoomSlider.setRange(40.0, 600.0, 1.0);
    zoomSlider.setValue(140.0);
    zoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.onValueChange = [this]
    {
        const auto zoom = static_cast<float>(zoomSlider.getValue());
        timeline.setPixelsPerSecond(zoom);
        pianoRoll.setPixelsPerSecond(zoom);
        repaint();
    };

    vZoomSlider.setRange(0.5, 2.0, 0.05);
    vZoomSlider.setValue(1.0);
    vZoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    vZoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    vZoomSlider.onValueChange = [this]
    {
        const auto factor = static_cast<float>(vZoomSlider.getValue());
        const auto pianoRow = 22.0f * factor;
        pianoRoll.setRowHeight(pianoRow);
        const auto row = 96.0f * factor;
        timeline.setRowHeight(row);
        trackList.setRowHeight(row);
        if (preferences != nullptr)
            preferences->setValue("ui.vZoom", vZoomSlider.getValue());
        repaint();
    };
    timeline.onSeek = [this](double seconds) { audio.setPosition(seconds); };
    timeline.onClipSelected = [this](const juce::String& clipId) { focusClip(clipId); };
    timeline.onClipGainRequested = [this](const juce::String& clipId)
    {
        selectedClipId = clipId;
        showClipGainDialog();
    };
    pianoRoll.onSeek = [this](double seconds) { audio.setPosition(seconds); };
    pianoRoll.onNoteSelected = [this](const juce::String& noteId)
    {
        focusNote(noteId);
    };
    trackList.peakProvider = [this](const juce::String& trackId) { return audio.trackPeak(trackId); };
    trackList.onTrackSelected = [this](const juce::String& trackId)
    {
        selectedTrackId = trackId;
        pianoRoll.setFocusedTrack(trackId);
        refreshProjectControls();
    };

    openButton.onClick = [this] { openProject(); };
    saveButton.onClick = [this] { saveProject(); };
    audioButton.onClick = [this] { importAudio(); };
    melodyneButton.onClick = [this] { importMelodyne(); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this]
    {
        playWhenRenderReady = false;
        audio.stop();
        audio.setPosition(0.0);
    };
    noteEditButton.onClick = [this]
    {
        setSourceEditMode(false);
        pianoRoll.setTool(PianoRollComponent::Tool::note);
        setToolButton(noteEditButton);
    };
    wrenchButton.onClick = [this]
    {
        setSourceEditMode(true);
        pianoRoll.setTool(PianoRollComponent::Tool::note);
        setToolButton(wrenchButton);
    };
    drawButton.onClick = [this]
    {
        setSourceEditMode(false);
        pianoRoll.setTool(PianoRollComponent::Tool::draw);
        setToolButton(drawButton);
    };
    lineButton.onClick = [this]
    {
        setSourceEditMode(false);
        pianoRoll.setTool(PianoRollComponent::Tool::line);
        setToolButton(lineButton);
    };
    connectButton.onClick = [this]
    {
        setSourceEditMode(false);
        pianoRoll.setTool(PianoRollComponent::Tool::connect);
        setToolButton(connectButton);
    };
    for (auto* button : { &pitchParamButton, &driftParamButton, &attackParamButton,
                          &breathParamButton, &tensionParamButton,
                          &formantParamButton, &volumeParamButton })
        button->onClick = [this, button]
        {
            setToolButton(*button);
            parameterMode = button == &driftParamButton ? ParameterMode::pitchDrift
                : button == &attackParamButton ? ParameterMode::attackSpeed
                : button == &breathParamButton ? ParameterMode::breath
                : button == &tensionParamButton ? ParameterMode::tension
                : button == &formantParamButton ? ParameterMode::formant
                : button == &volumeParamButton ? ParameterMode::volume
                : ParameterMode::pitchSmooth;
            refreshSelectedNoteParameter();
        };
    noteEditButton.setClickingTogglesState(false);
    wrenchButton.setClickingTogglesState(false);

    project.addChangeListener(this);
    audio.addChangeListener(this);
    refreshTexts();
    refreshProjectControls();
    setSourceEditMode(false);
    setToolButton(pitchParamButton);
    refreshSelectedNoteParameter();
    setSize(1280, 760);
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (preferences != nullptr) audio.saveDeviceState(*preferences);
    panelSplitter.removeMouseListener(this);
    audio.removeChangeListener(this);
    project.removeChangeListener(this);
    menuBar.setModel(nullptr);
    setLookAndFeel(nullptr);
}

void MainComponent::applyPreferences()
{
    if (preferences == nullptr) return;
    strings.setLanguage(static_cast<I18n::Language>(juce::jlimit(1, 5,
        preferences->getIntValue("ui.language", static_cast<int>(strings.getLanguage()) + 1)) - 1));
    const auto parseColour = [](juce::String value, juce::Colour fallback)
    {
        value = value.trim().removeCharacters("#");
        if (value.length() != 6 && value.length() != 8) return fallback;
        if (value.length() == 6) value = "ff" + value;
        return juce::Colour::fromString(value);
    };
    Palette::applyTheme(preferences->getValue("ui.theme", "dark"),
        parseColour(preferences->getValue("ui.accent", "7F69CA"), juce::Colour(0xff7f69ca)),
        parseColour(preferences->getValue("ui.accentLight", "CBCBFA"), juce::Colour(0xffcbcbfa)),
        parseColour(preferences->getValue("ui.noteColour", "F4C000"), juce::Colour(0xfff4c000)));
    audio.setHifiganModelDirectory(juce::File(
        preferences->getValue("algorithm.hifiganPath")));
    const auto analysisConfig = backend::AnalysisService::configFromProperties(preferences.get());
    audio.setInferenceConfiguration(analysisConfig.inference, analysisConfig.deviceIndex);
    pianoRoll.setShowNoteLabels(preferences->getBoolValue("ui.showNoteLabels", false));
    showWaveforms = preferences->getBoolValue("ui.showWaveforms", true);
    pianoRoll.setShowWaveforms(showWaveforms);
    const auto vZoom = juce::jlimit(0.5, 2.0, preferences->getDoubleValue("ui.vZoom", 1.0));
    vZoomSlider.setValue(vZoom, juce::dontSendNotification);
    pianoRoll.setRowHeight(22.0f * static_cast<float>(vZoom));
    const auto row = 96.0f * static_cast<float>(vZoom);
    timeline.setRowHeight(row);
    trackList.setRowHeight(row);
    // Applying a new model path must invalidate and immediately reschedule
    // already imported compose clips; waiting for a later edit made the
    // settings change appear ineffective.
    audio.syncProject(project.snapshot());
    lookAndFeel.refreshColours();
    applyUiScale();
}

void MainComponent::applyUiScale()
{
    static const float systemScale = juce::Desktop::getInstance().getGlobalScaleFactor();
    const auto uiScale = static_cast<float>(juce::jlimit(0.6, 2.0,
        preferences != nullptr ? preferences->getDoubleValue("ui.uiScale", 1.0) : 1.0));
    juce::Desktop::getInstance().setGlobalScaleFactor(systemScale * uiScale);
}

void MainComponent::refreshTexts()
{
    openButton.setButtonText({});
    openButton.setTooltip(strings.text("file.open"));
    saveButton.setButtonText({});
    saveButton.setTooltip(strings.text("file.save"));
    audioButton.setButtonText({});
    audioButton.setTooltip(strings.text("file.audio"));
    melodyneButton.setButtonText({});
    melodyneButton.setTooltip(strings.text("file.melodyne"));
    playButton.setButtonText({});
    playButton.setTooltip(strings.text("transport.play"));
    stopButton.setButtonText({});
    stopButton.setTooltip(strings.text("transport.stop"));
    noteEditButton.setButtonText({});
    noteEditButton.setTooltip(strings.text("tool.main"));
    wrenchButton.setButtonText({});
    wrenchButton.setTooltip(strings.text("tool.wrench"));
    drawButton.setButtonText({});
    drawButton.setTooltip(strings.text("tool.draw"));
    lineButton.setButtonText({});
    lineButton.setTooltip(strings.text("tool.line"));
    connectButton.setButtonText({});
    connectButton.setTooltip(strings.text("tool.connect"));
    parameterTitle.setText(strings.text("editor.parameters"), juce::dontSendNotification);
    smoothCaption.setText(strings.text("editor.smooth"), juce::dontSendNotification);
    pitchParamButton.setButtonText(strings.text("param.pitch"));
    driftParamButton.setButtonText(strings.text("param.drift"));
    attackParamButton.setButtonText(strings.text("param.attack"));
    breathParamButton.setButtonText(strings.text("param.breath"));
    tensionParamButton.setButtonText(strings.text("param.tension"));
    formantParamButton.setButtonText(strings.text("param.formant"));
    volumeParamButton.setButtonText(strings.text("param.volume"));
    robustPitchCurveButton.setButtonText(strings.text("param.robustPitchCurveShort"));
    robustPitchCurveButton.setTooltip(strings.text("param.robustPitchCurve"));
    bpmCaption.setText("BPM", juce::dontSendNotification);
    beatsCaption.setText(strings.text("beats.bar"), juce::dontSendNotification);
    denominatorLabel.setText("/ 4", juce::dontSendNotification);
    gridCaption.setText(strings.text("grid"), juce::dontSendNotification);
    scaleCaption.setText(strings.text("base.scale"), juce::dontSendNotification);
    pitchLabel.setText(strings.text("algo.pitch"), juce::dontSendNotification);
    stretchLabel.setText(strings.text("algo.stretch"), juce::dontSendNotification);
    pitchAlgorithm.setTooltip(strings.text("algo.pitch"));
    stretchAlgorithm.setTooltip(strings.text("algo.stretch"));
    refreshStretchAlgorithmItems(stretchAlgorithm.getSelectedId());
    statusLabel.setText(strings.text("status.ready"), juce::dontSendNotification);
    sourceEditHint.setText(strings.text("edit.source"), juce::dontSendNotification);
    sampleAliasLabel.setText(strings.text("sample.alias"), juce::dontSendNotification);
    sampleStartLabel.setText(strings.text("sample.start"), juce::dontSendNotification);
    sampleEndLabel.setText(strings.text("sample.end"), juce::dontSendNotification);
    sampleAlignmentLabel.setText(strings.text("sample.alignment"), juce::dontSendNotification);
    sampleFixedLabel.setText(strings.text("sample.fixed"), juce::dontSendNotification);
    sampleSaveButton.setButtonText(strings.text("sample.save"));
    otoImportButton.setButtonText(strings.text("sample.importOto"));
    otoExportButton.setButtonText(strings.text("sample.exportOto"));
    refreshSelectedNoteParameter();
}

void MainComponent::refreshProjectControls()
{
    const auto data = project.snapshot();
    bpmEditor.setText(juce::String(data.bpm, std::abs(data.bpm - std::floor(data.bpm)) < 1.0e-9 ? 0 : 2),
                      juce::dontSendNotification);
    beatsEditor.setText(juce::String(data.numerator), juce::dontSendNotification);
    gridSelector.setText(data.gridDivision, juce::dontSendNotification);
    scaleSelector.setText(data.baseScale, juce::dontSendNotification);
    auto selected = selectedTrackId.isNotEmpty()
        ? std::find_if(data.tracks.begin(), data.tracks.end(),
            [this](const auto& track) { return track.id == selectedTrackId; })
        : data.tracks.end();
    if (selected == data.tracks.end())
        selected = std::find_if(data.tracks.begin(), data.tracks.end(),
                                [](const auto& track) { return track.compose; });
    if (selected != data.tracks.end())
    {
        const auto pitchId = selected->pitchAlgorithm == PitchAlgorithm::nsfHifigan ? 2
            : selected->pitchAlgorithm == PitchAlgorithm::world ? 3
            : selected->pitchAlgorithm == PitchAlgorithm::vocalShifter ? 4
            : selected->pitchAlgorithm == PitchAlgorithm::mld3 ? 5
            : selected->pitchAlgorithm == PitchAlgorithm::llsm2 ? 6 : 1;
        const auto stretchId = selected->stretchAlgorithm == StretchAlgorithm::variableMelHop ? 2
            : selected->stretchAlgorithm == StretchAlgorithm::loop ? 3
            : selected->stretchAlgorithm == StretchAlgorithm::soundTouch ? 4
            : selected->stretchAlgorithm == StretchAlgorithm::nsfShiftThenSplice ? 5 : 1;
        pitchAlgorithm.setSelectedId(pitchId, juce::dontSendNotification);
        refreshStretchAlgorithmItems(stretchId);
    }
    refreshSelectedNoteParameter();
}

void MainComponent::refreshSelectedNoteParameter()
{
    NoteData selected;
    auto found = false;
    const auto data = project.snapshot();
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            for (const auto& note : clip.notes)
                if (note.id == selectedNoteId)
                {
                    selected = note;
                    found = true;
                }
    updatingSmoothSlider = true;
    double value = 0.0;
    if (parameterMode == ParameterMode::pitchSmooth)
    {
        smoothCaption.setText(strings.text("editor.smooth"), juce::dontSendNotification);
        smoothSlider.setRange(0.0, 100.0, 1.0);
        smoothSlider.setTextValueSuffix("%");
        value = (1.0 - static_cast<double>(selected.modulation)) * 100.0;
    }
    else if (parameterMode == ParameterMode::pitchDrift)
    {
        smoothCaption.setText(strings.text("editor.drift"), juce::dontSendNotification);
        // Melodyne stores the remaining drift factor.  Present the familiar
        // correction amount: 100% removes drift, 0% preserves it, while a
        // negative value retains imported emphasis factors above 1.0.
        smoothSlider.setRange(-100.0, 100.0, 1.0);
        smoothSlider.setTextValueSuffix("%");
        value = (1.0 - static_cast<double>(selected.drift)) * 100.0;
    }
    else if (parameterMode == ParameterMode::attackSpeed)
    {
        smoothCaption.setText(strings.text("editor.attackSpeed"), juce::dontSendNotification);
        smoothSlider.setRange(5.0, 2000.0, 1.0);
        smoothSlider.setTextValueSuffix("%");
        value = static_cast<double>(selected.attackSpeed) * 100.0;
    }
    else if (parameterMode == ParameterMode::breath)
    {
        smoothCaption.setText(strings.text("param.breath"), juce::dontSendNotification);
        smoothSlider.setRange(0.0, 100.0, 1.0);
        smoothSlider.setTextValueSuffix("%");
        value = static_cast<double>(selected.breath) * 100.0;
    }
    else if (parameterMode == ParameterMode::tension)
    {
        smoothCaption.setText(strings.text("param.tension"), juce::dontSendNotification);
        smoothSlider.setRange(-100.0, 100.0, 1.0);
        smoothSlider.setTextValueSuffix("%");
        value = static_cast<double>(selected.tension) * 100.0;
    }
    else if (parameterMode == ParameterMode::formant)
    {
        smoothCaption.setText(strings.text("param.formant"), juce::dontSendNotification);
        smoothSlider.setRange(-12.0, 12.0, 0.1);
        smoothSlider.setTextValueSuffix(" st");
        value = selected.formantSemitones;
    }
    else
    {
        smoothCaption.setText(strings.text("param.volume"), juce::dontSendNotification);
        smoothSlider.setRange(-60.0, 12.0, 0.1);
        smoothSlider.setTextValueSuffix(" dB");
        value = selected.gain > 1.0e-6f ? 20.0 * std::log10(selected.gain) : -60.0;
    }
    smoothSlider.setValue(value, juce::dontSendNotification);
    updatingSmoothSlider = false;
    smoothSlider.setEnabled(found);
    updatingRobustPitchCurve = true;
    robustPitchCurveButton.setToggleState(found && selected.robustPitchCurve,
                                           juce::dontSendNotification);
    updatingRobustPitchCurve = false;
    const auto showRobust = pitchAlgorithm.getSelectedId() == 1;
    const auto visibilityChanged = robustPitchCurveButton.isVisible() != showRobust;
    robustPitchCurveButton.setVisible(showRobust);
    robustPitchCurveButton.setEnabled(found && showRobust);
    if (visibilityChanged) resized();
}

void MainComponent::applySelectedNoteParameter()
{
    const auto value = smoothSlider.getValue();
    if (parameterMode == ParameterMode::pitchSmooth)
        project.setNoteModulation(selectedNoteId,
            1.0f - static_cast<float>(value / 100.0));
    else if (parameterMode == ParameterMode::pitchDrift)
        project.setNoteDrift(selectedNoteId,
            1.0f - static_cast<float>(value / 100.0));
    else if (parameterMode == ParameterMode::attackSpeed)
        project.setNoteAttackSpeed(selectedNoteId,
            static_cast<float>(value / 100.0));
    else if (parameterMode == ParameterMode::breath)
        project.setNoteBreath(selectedNoteId, static_cast<float>(value / 100.0));
    else if (parameterMode == ParameterMode::tension)
        project.setNoteTension(selectedNoteId, static_cast<float>(value / 100.0));
    else if (parameterMode == ParameterMode::formant)
        project.setNoteFormant(selectedNoteId, static_cast<float>(value));
    else
        project.setNoteGain(selectedNoteId, value <= -59.9 ? 0.0f
            : static_cast<float>(std::pow(10.0, value / 20.0)));
}

void MainComponent::refreshStretchAlgorithmItems(int preferredId)
{
    const auto previous = preferredId > 0 ? preferredId : stretchAlgorithm.getSelectedId();
    stretchAlgorithm.clear(juce::dontSendNotification);
    stretchAlgorithm.addItem(strings.text("algo.stretch.melodyneHybrid"), 1);
    if (pitchAlgorithm.getSelectedId() == 2)
    {
        stretchAlgorithm.addItem(strings.text("algo.stretch.nsfVariableMel"), 2);
        stretchAlgorithm.addItem(strings.text("algo.stretch.nsfShiftThenSplice"), 5);
    }
    stretchAlgorithm.addItem(strings.text("algo.stretch.loop"), 3);
    stretchAlgorithm.addItem(strings.text("algo.stretch.soundTouch"), 4);
    const auto canUsePreferred = (previous != 2 && previous != 5)
        || pitchAlgorithm.getSelectedId() == 2;
    stretchAlgorithm.setSelectedId(canUsePreferred && previous > 0 ? previous : 1,
                                   juce::dontSendNotification);
}

void MainComponent::setToolButton(juce::Button& selected)
{
    const std::array<juce::Button*, 5> editTools {
        &noteEditButton, &wrenchButton, &drawButton, &lineButton, &connectButton
    };
    for (auto* button : editTools)
        button->setToggleState(button == &selected, juce::dontSendNotification);
    const std::array<juce::Button*, 7> parameterTools {
        &pitchParamButton, &driftParamButton, &attackParamButton,
        &breathParamButton, &tensionParamButton,
        &formantParamButton, &volumeParamButton
    };
    for (auto* button : parameterTools)
        if (&selected == button || &selected == &pitchParamButton || &selected == &driftParamButton
            || &selected == &attackParamButton
            || &selected == &breathParamButton
            || &selected == &tensionParamButton || &selected == &formantParamButton
            || &selected == &volumeParamButton)
            button->setToggleState(button == &selected, juce::dontSendNotification);
}

void MainComponent::togglePlayback()
{
    if (audio.isPlaying())
    {
        playWhenRenderReady = false;
        audio.stop();
        return;
    }
    juce::String deviceError;
    if (!audio.ensureOutputDevice(deviceError))
    {
        playWhenRenderReady = false;
        showError(strings.text("settings.noAudioDevice") + "\n" + deviceError);
        return;
    }
    if (!sourceEditActive && audio.renderProgress())
    {
        playWhenRenderReady = true;
        return;
    }
    playWhenRenderReady = false;
    audio.play();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);
    g.setColour(Palette::panel);
    g.fillRect(0, 0, getWidth(), 61);
    g.setColour(Palette::border);
    g.drawHorizontalLine(60, 0.0f, static_cast<float>(getWidth()));
    const auto splitterBounds = panelSplitter.getBounds();
    g.setColour(Palette::background);
    g.fillRect(splitterBounds);
    g.setColour(Palette::border);
    g.drawHorizontalLine(splitterBounds.getY(), static_cast<float>(splitterBounds.getX()),
                         static_cast<float>(splitterBounds.getRight()));
    g.drawHorizontalLine(splitterBounds.getBottom() - 1, static_cast<float>(splitterBounds.getX()),
                         static_cast<float>(splitterBounds.getRight()));
    g.setColour(Palette::panel);
    g.fillRect(zoomBar.getBounds());
    g.fillRect(vZoomBar.getBounds());
    g.setColour(Palette::border);
    g.drawHorizontalLine(zoomBar.getBounds().getY(), static_cast<float>(zoomBar.getX()),
                         static_cast<float>(zoomBar.getRight()));
    g.drawVerticalLine(vZoomBar.getBounds().getX(), static_cast<float>(vZoomBar.getY()),
                       static_cast<float>(vZoomBar.getBottom()));
    const auto zoomRatio = (zoomSlider.getValue() - 40.0) / (600.0 - 40.0);
    const auto thumbX = zoomBar.getX() + static_cast<int>(zoomRatio * zoomBar.getWidth());
    g.setColour(Palette::accent);
    g.fillRect(thumbX - 2, zoomBar.getY() + 2, 4, zoomBar.getHeight() - 4);
    const auto vZoomRatio = (vZoomSlider.getValue() - 0.5) / (2.0 - 0.5);
    const auto thumbY = vZoomBar.getY() + static_cast<int>((1.0 - vZoomRatio) * vZoomBar.getHeight());
    g.fillRect(vZoomBar.getX() + 2, thumbY - 2, vZoomBar.getWidth() - 4, 4);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown()) project.redo();
        else project.undo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        project.redo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        pianoRoll.selectAllNotes();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'C')
    {
        if (!pianoRoll.selectedNoteIds().empty()) copySelectedNotes(false);
        else if (selectedClipId.isNotEmpty()) copySelectedClip();
        else return false;
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'X'
        && !pianoRoll.selectedNoteIds().empty())
    {
        copySelectedNotes(true);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'V')
    {
        if (!copiedNotes.empty()) pasteCopiedNotes();
        else if (copiedClipId.isNotEmpty()) pasteCopiedClip();
        else return false;
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D'
        && selectedClipId.isNotEmpty())
    {
        duplicateSelectedClip();
        return true;
    }
    if (key == juce::KeyPress::spaceKey
        && (preferences == nullptr
            || preferences->getBoolValue("operation.spacePlayback", true)))
    {
        togglePlayback();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'N')
    {
        newProject();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O')
    {
        openProject();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
    {
        if (key.getModifiers().isShiftDown()) saveProjectAs();
        else saveProject();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::homeKey)
    {
        audio.stop();
        audio.setPosition(0.0);
        return true;
    }
    return false;
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (juce::File(path).hasFileExtension("wav;flac;aif;aiff;mp3;ogg;hjpx;hspx;mpd;mid;midi"))
            return true;
    return false;
}

void MainComponent::openExternalFile(const juce::File& file)
{
    if (file.hasFileExtension("mpd"))
        loadMelodyneFile(file);
    else if (file.hasFileExtension("hjpx;hspx"))
        performWithUnsavedCheck([this, file] { loadProjectFile(file); });
    else if (file.hasFileExtension("mid;midi"))
    {
        juce::String error;
        if (!project.addMidiFile(file, error))
            showError(strings.text("error.midi") + "\n" + error);
    }
    else if (const auto duration = audio.probeDuration(file))
        addAnalysedAudioFile(file, *duration);
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    auto dropSeconds = audio.position();
    juce::String targetTrackId;
    if (timelineViewport.getBounds().contains(x, y))
    {
        dropSeconds = timeline.secondsForPixel(x - timelineViewport.getX()
                                               + timelineViewport.getViewPositionX());
        targetTrackId = timeline.trackIdForPixel(y - timelineViewport.getY()
                                                 + timelineViewport.getViewPositionY());
    }
    else if (pianoViewport.getBounds().contains(x, y))
    {
        dropSeconds = pianoRoll.secondsForPixel(x - pianoViewport.getX()
                                                + pianoViewport.getViewPositionX());
        targetTrackId = selectedTrackId;
    }
    auto nextDropSeconds = dropSeconds;
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("wav;flac;aif;aiff;mp3;ogg"))
        {
            if (const auto duration = audio.probeDuration(file))
            {
                addAnalysedAudioFile(file, *duration, nextDropSeconds, targetTrackId);
                if (targetTrackId.isNotEmpty()) nextDropSeconds += *duration;
            }
        }
        else
            openExternalFile(file);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto menu = area.removeFromTop(27);
    menuBar.setBounds(menu);
    auto toolbar = area.removeFromTop(34).reduced(5, 3);
    auto take = [&toolbar](juce::Component& component, int width)
    {
        component.setBounds(toolbar.removeFromLeft(width));
        toolbar.removeFromLeft(3);
    };
    take(bpmCaption, 30);
    take(bpmEditor, 50);
    take(beatsCaption, 58);
    take(beatsEditor, 32);
    take(denominatorLabel, 27);
    take(gridCaption, 28);
    take(gridSelector, 67);
    take(scaleCaption, 52);
    take(scaleSelector, 58);
    toolbar.removeFromLeft(5);
    take(stopButton, 28);
    take(playButton, 28);
    toolbar.removeFromLeft(5);
    take(openButton, 28);
    take(saveButton, 28);
    take(audioButton, 32);
    take(melodyneButton, 32);
    progressBar.setBounds(toolbar.removeFromRight(100).reduced(4, 5));

    auto footer = area.removeFromBottom(24);
    statusLabel.setBounds(footer.reduced(8, 0));
    const auto sampleEditorHeight = sourceEditActive ? 36 : 0;
    const auto splitAvailable = std::max(1, area.getHeight() - 8 - 36 - sampleEditorHeight);
    const auto upperHeight = juce::jlimit(150, std::max(150, splitAvailable - 120),
        static_cast<int>(std::round(static_cast<float>(splitAvailable) * panelSplitRatio)));
    auto upper = area.removeFromTop(upperHeight);
    trackViewport.setBounds(upper.removeFromLeft(280));
    timelineViewport.setBounds(upper);

    panelSplitter.setBounds(area.removeFromTop(8));

    auto parameterHeader = area.removeFromTop(36).reduced(4, 3);
    auto takeParameterRight = [&parameterHeader](juce::Component& component, int width)
    {
        component.setBounds(parameterHeader.removeFromRight(width));
        parameterHeader.removeFromRight(3);
    };
    takeParameterRight(stretchAlgorithm, 124);
    takeParameterRight(stretchLabel, 52);
    takeParameterRight(pitchAlgorithm, 106);
    takeParameterRight(pitchLabel, 50);
    auto takeParameter = [&parameterHeader](juce::Component& component, int width)
    {
        component.setBounds(parameterHeader.removeFromLeft(width));
        parameterHeader.removeFromLeft(3);
    };
    takeParameter(parameterTitle, 70);
    takeParameter(noteEditButton, 27);
    takeParameter(drawButton, 27);
    takeParameter(lineButton, 27);
    takeParameter(wrenchButton, 27);
    takeParameter(connectButton, 27);
    takeParameter(smoothCaption, 42);
    takeParameter(smoothSlider, 118);
    takeParameter(pitchParamButton, 50);
    takeParameter(driftParamButton, 50);
    takeParameter(attackParamButton, 50);
    takeParameter(breathParamButton, 50);
    takeParameter(tensionParamButton, 50);
    takeParameter(formantParamButton, 50);
    takeParameter(volumeParamButton, 50);
    if (robustPitchCurveButton.isVisible())
        takeParameter(robustPitchCurveButton, 74);
    sourceEditHint.setBounds(parameterHeader.reduced(3, 0));
    auto sampleBar = area.removeFromTop(sampleEditorHeight).reduced(4, 3);
    auto setSampleVisible = [this](bool visible)
    {
        for (auto* component : { static_cast<juce::Component*>(&sampleRegionSelector),
             static_cast<juce::Component*>(&sampleAliasEditor), static_cast<juce::Component*>(&sampleStartEditor),
             static_cast<juce::Component*>(&sampleEndEditor), static_cast<juce::Component*>(&sampleAlignmentEditor),
             static_cast<juce::Component*>(&sampleFixedEditor), static_cast<juce::Component*>(&sampleAliasLabel),
             static_cast<juce::Component*>(&sampleStartLabel), static_cast<juce::Component*>(&sampleEndLabel),
             static_cast<juce::Component*>(&sampleAlignmentLabel), static_cast<juce::Component*>(&sampleFixedLabel),
             static_cast<juce::Component*>(&sampleSaveButton), static_cast<juce::Component*>(&otoImportButton),
             static_cast<juce::Component*>(&otoExportButton) }) component->setVisible(visible);
    };
    setSampleVisible(sourceEditActive);
    if (sourceEditActive)
    {
        const auto takeSample = [&sampleBar](juce::Component& component, int width)
        {
            component.setBounds(sampleBar.removeFromLeft(width));
            sampleBar.removeFromLeft(3);
        };
        takeSample(sampleRegionSelector, 104);
        takeSample(sampleAliasLabel, 34); takeSample(sampleAliasEditor, 100);
        takeSample(sampleStartLabel, 34); takeSample(sampleStartEditor, 64);
        takeSample(sampleEndLabel, 30); takeSample(sampleEndEditor, 64);
        takeSample(sampleAlignmentLabel, 44); takeSample(sampleAlignmentEditor, 64);
        takeSample(sampleFixedLabel, 44); takeSample(sampleFixedEditor, 64);
        takeSample(sampleSaveButton, 64);
        takeSample(otoImportButton, 82);
        takeSample(otoExportButton, 82);
    }
    constexpr auto zoomStrip = 14;
    zoomBar.setBounds(area.removeFromBottom(zoomStrip));
    vZoomBar.setBounds(area.removeFromRight(zoomStrip));
    pianoViewport.setBounds(area);
    if (!pianoInitialScrollSet && pianoViewport.getHeight() > 0)
    {
        pianoViewport.setViewPosition(0, std::max(0, (pianoRoll.getHeight() - pianoViewport.getHeight()) / 2));
        pianoInitialScrollSet = true;
    }
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.eventComponent == &panelSplitter)
    {
        draggingPanelSplitter = true;
        panelSplitterDragScreenY = event.getScreenY();
        panelSplitterDragRatio = panelSplitRatio;
    }
    else if (event.eventComponent == &zoomBar)
    {
        draggingZoomBar = true;
        updateHorizontalZoom(event.getPosition().x);
    }
    else if (event.eventComponent == &vZoomBar)
    {
        draggingVZoomBar = true;
        updateVerticalZoom(event.getPosition().y);
    }
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingZoomBar && event.eventComponent == &zoomBar)
    {
        updateHorizontalZoom(event.getPosition().x);
        return;
    }
    if (draggingVZoomBar && event.eventComponent == &vZoomBar)
    {
        updateVerticalZoom(event.getPosition().y);
        return;
    }
    if (!draggingPanelSplitter || event.eventComponent != &panelSplitter) return;
    const auto splitAvailable = std::max(1, getHeight() - 27 - 34 - 24 - 8 - 36);
    panelSplitRatio = juce::jlimit(0.15f, 0.85f,
        panelSplitterDragRatio + static_cast<float>(event.getScreenY() - panelSplitterDragScreenY)
            / static_cast<float>(splitAvailable));
    resized();
    repaint();
}

void MainComponent::mouseUp(const juce::MouseEvent&)
{
    draggingPanelSplitter = false;
    draggingZoomBar = false;
    draggingVZoomBar = false;
}

void MainComponent::updateHorizontalZoom(int x)
{
    const auto width = std::max(1, zoomBar.getWidth());
    zoomSlider.setValue(juce::jlimit(40.0, 600.0,
        40.0 + static_cast<double>(x) / width * (600.0 - 40.0)));
}

void MainComponent::updateVerticalZoom(int y)
{
    const auto height = std::max(1, vZoomBar.getHeight());
    vZoomSlider.setValue(juce::jlimit(0.5, 2.0,
        2.0 - static_cast<double>(y) / height * (2.0 - 0.5)));
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &project)
    {
        const auto data = project.snapshot();
        audio.syncProject(data);
        const auto trackExists = std::any_of(data.tracks.begin(), data.tracks.end(),
            [this](const auto& track) { return track.id == selectedTrackId; });
        if (!trackExists) selectedTrackId.clear();
        if (selectedTrackId.isEmpty())
        {
            const auto firstCompose = std::find_if(data.tracks.begin(), data.tracks.end(),
                [](const auto& track) { return track.compose; });
            if (firstCompose != data.tracks.end()) selectedTrackId = firstCompose->id;
        }
        trackList.setSelectedTrack(selectedTrackId);
        pianoRoll.setFocusedTrack(selectedTrackId);
        auto clipExists = false;
        for (const auto& track : data.tracks)
            clipExists = clipExists || std::any_of(track.clips.begin(), track.clips.end(),
                [this](const auto& clip) { return clip.id == selectedClipId; });
        if (!clipExists)
        {
            selectedClipId.clear();
            pianoRoll.setFocusedClip({});
        }
        auto noteExists = false;
        for (const auto& track : data.tracks)
            for (const auto& clip : track.clips)
                noteExists = noteExists || std::any_of(clip.notes.begin(), clip.notes.end(),
                    [this](const auto& note) { return note.id == selectedNoteId; });
        if (!noteExists) selectedNoteId.clear();
        refreshProjectControls();
        refreshSelectedNoteParameter();
        menuItemsChanged();
    }
}

void MainComponent::timerCallback()
{
    const auto expectedPlayIcon = audio.isPlaying() ? juce::String("icon.pause")
                                                     : juce::String("icon.play");
    if (playButton.getComponentID() != expectedPlayIcon)
    {
        playButton.setComponentID(expectedPlayIcon);
        playButton.setTooltip(strings.text(audio.isPlaying() ? "transport.pause" : "transport.play"));
        playButton.repaint();
    }
    pianoRoll.setPlayheadSeconds(audio.position());
    timeline.setPlayheadSeconds(audio.position());
    trackList.repaint();
    if (!importInProgress)
    {
        if (const auto render = audio.renderProgress())
        {
            showingRenderProgress = true;
            progress = *render;
            statusLabel.setText(strings.text("status.rendering") + "  "
                                    + juce::String(static_cast<int>(std::round(*render * 100.0))) + "%",
                                juce::dontSendNotification);
        }
        else
        {
            if (showingRenderProgress)
            {
                showingRenderProgress = false;
                progress = 0.0;
            }
            if (pendingNativeAnalyses > 0)
                statusLabel.setText(strings.text("status.analyzing") + "  "
                    + nativeAnalysisName + "  "
                    + juce::String(static_cast<int>(std::round(nativeAnalysisProgress * 100.0)))
                    + "%", juce::dontSendNotification);
            else
            {
                const auto backend = audio.activeRenderBackends();
                statusLabel.setText((audio.isPlaying() ? strings.text("transport.play")
                                                        : strings.text("status.ready"))
                                        + "  " + juce::String(audio.position(), 2) + " s"
                                        + (backend.isNotEmpty() ? "  ·  " + backend : juce::String()),
                                    juce::dontSendNotification);
            }
            if (playWhenRenderReady)
            {
                playWhenRenderReady = false;
                juce::String deviceError;
                if (audio.ensureOutputDevice(deviceError)) audio.play();
                else showError(strings.text("settings.noAudioDevice") + "\n" + deviceError);
            }
        }
    }

    if (!syncingScroll)
    {
        if (audio.isPlaying() && !sourceEditActive)
        {
            const auto playheadX = timeline.pixelForSeconds(audio.position());
            const auto viewLeft = timelineViewport.getViewPositionX();
            const auto viewWidth = timelineViewport.getViewWidth();
            if (playheadX < viewLeft + 20 || playheadX > viewLeft + viewWidth - 56)
                timelineViewport.setViewPosition(std::max(0, playheadX - viewWidth / 4),
                                                 timelineViewport.getViewPositionY());
        }
        if (audio.isPlaying() && sourceEditActive)
        {
            const auto playheadX = pianoRoll.pixelForSeconds(audio.position());
            const auto viewLeft = pianoViewport.getViewPositionX();
            const auto viewWidth = pianoViewport.getViewWidth();
            if (playheadX < viewLeft + 58 || playheadX > viewLeft + viewWidth - 56)
                pianoViewport.setViewPosition(std::max(0, playheadX - viewWidth / 4),
                                              pianoViewport.getViewPositionY());
        }
        const auto timelineX = timelineViewport.getViewPositionX();
        const auto pianoX = pianoViewport.getViewPositionX();
        syncingScroll = true;
        if (!sourceEditActive && timelineX != lastTimelineX)
            pianoViewport.setViewPosition(timelineX, pianoViewport.getViewPositionY());
        else if (!sourceEditActive && pianoX != lastPianoX)
            timelineViewport.setViewPosition(pianoX, timelineViewport.getViewPositionY());
        lastTimelineX = timelineViewport.getViewPositionX();
        lastPianoX = pianoViewport.getViewPositionX();
        const auto timelineY = timelineViewport.getViewPositionY();
        const auto trackY = trackViewport.getViewPositionY();
        if (timelineY != lastTimelineY)
            trackViewport.setViewPosition(0, timelineY);
        else if (trackY != lastTrackY)
            timelineViewport.setViewPosition(timelineViewport.getViewPositionX(), trackY);
        lastTimelineY = timelineViewport.getViewPositionY();
        lastTrackY = trackViewport.getViewPositionY();
        syncingScroll = false;
    }
}

void MainComponent::setSourceEditMode(bool enabled)
{
    playWhenRenderReady = false;
    sourceEditActive = enabled;
    if (enabled && selectedClipId.isEmpty())
    {
        const auto data = project.snapshot();
        for (const auto& track : data.tracks)
            if (!track.clips.empty())
            {
                selectedClipId = track.clips.front().id;
                break;
            }
    }
    pianoRoll.setSourceEditMode(enabled);
    pianoRoll.setFocusedClip(selectedClipId);
    if (enabled)
    {
        const auto data = project.snapshot();
        for (const auto& track : data.tracks)
            for (const auto& clip : track.clips)
                if (clip.id == selectedClipId)
                {
                    audio.setAuditionFile(clip.sourceFile);
                    pianoViewport.setViewPosition(std::max(0, pianoRoll.pixelForSeconds(clip.sourceOffsetSeconds)
                                                              - pianoViewport.getViewWidth() / 4),
                                                  pianoViewport.getViewPositionY());
                }
        loadSampleSettings();
    }
    else
    {
        audio.clearAuditionFile();
        pianoViewport.setViewPosition(timelineViewport.getViewPositionX(), pianoViewport.getViewPositionY());
    }
    noteEditButton.setToggleState(!enabled, juce::dontSendNotification);
    wrenchButton.setToggleState(enabled, juce::dontSendNotification);
    sourceEditHint.setVisible(enabled);
    resized();
}

void MainComponent::focusClip(const juce::String& clipId)
{
    selectedClipId = clipId;
    pianoRoll.setFocusedClip(clipId);
    const auto data = project.snapshot();
    const ClipData* selectedClip = nullptr;
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            if (clip.id == clipId)
            {
                selectedClip = &clip;
                selectedTrackId = track.id;
                trackList.setSelectedTrack(track.id);
                pianoRoll.setFocusedTrack(track.id);
                refreshProjectControls();
                break;
            }

    if (!sourceEditActive) return;
    if (selectedClip == nullptr)
    {
        audio.clearAuditionFile();
        return;
    }
    audio.setAuditionFile(selectedClip->sourceFile);
    pianoViewport.setViewPosition(std::max(0,
        pianoRoll.pixelForSeconds(selectedClip->sourceOffsetSeconds)
            - pianoViewport.getViewWidth() / 4), pianoViewport.getViewPositionY());
    loadSampleSettings();
}

void MainComponent::focusNote(const juce::String& noteId)
{
    selectedNoteId = noteId;
    if (noteId.isNotEmpty())
    {
        const auto data = project.snapshot();
        for (const auto& track : data.tracks)
            for (const auto& clip : track.clips)
                if (std::any_of(clip.notes.begin(), clip.notes.end(),
                    [&noteId](const auto& note) { return note.id == noteId; }))
                {
                    focusClip(clip.id);
                    refreshSelectedNoteParameter();
                    return;
                }
    }
    refreshSelectedNoteParameter();
}

void MainComponent::loadSampleSettings()
{
    sampleSettingsFile = juce::File{};
    const auto data = project.snapshot();
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            if (clip.id == selectedClipId)
                sampleSettingsFile = clip.sourceFile;
    sampleSettingsRows = sampleSettingsFile.existsAsFile()
        ? SampleSettings::loadOrDerive(sampleSettingsFile, data)
        : std::vector<SampleRegionSetting>{};
    activeSampleSetting = 0;
    sampleRegionSelector.clear(juce::dontSendNotification);
    for (std::size_t index = 0; index < sampleSettingsRows.size(); ++index)
        sampleRegionSelector.addItem(juce::String(index + 1) + " · "
                                     + sampleSettingsRows[index].name,
                                     static_cast<int>(index + 1));
    if (!sampleSettingsRows.empty())
        sampleRegionSelector.setSelectedId(1, juce::dontSendNotification);
    pianoRoll.setSampleRegions(sampleSettingsRows, activeSampleSetting);
    refreshSampleEditors();
}

void MainComponent::refreshSampleEditors()
{
    const auto enabled = activeSampleSetting >= 0
        && activeSampleSetting < static_cast<int>(sampleSettingsRows.size());
    for (auto* editor : { &sampleAliasEditor, &sampleStartEditor, &sampleEndEditor,
                          &sampleAlignmentEditor, &sampleFixedEditor }) editor->setEnabled(enabled);
    if (!enabled)
    {
        for (auto* editor : { &sampleAliasEditor, &sampleStartEditor, &sampleEndEditor,
                              &sampleAlignmentEditor, &sampleFixedEditor }) editor->clear();
        return;
    }
    const auto& row = sampleSettingsRows[static_cast<std::size_t>(activeSampleSetting)];
    sampleAliasEditor.setText(row.name, false);
    sampleStartEditor.setText(juce::String(row.regionStartSeconds, 4), false);
    sampleEndEditor.setText(juce::String(row.regionEndSeconds, 4), false);
    sampleAlignmentEditor.setText(juce::String(row.alignmentSeconds, 4), false);
    sampleFixedEditor.setText(juce::String(row.fixedDurationSeconds, 4), false);
    pianoRoll.setSampleRegions(sampleSettingsRows, activeSampleSetting);
}

void MainComponent::commitSampleEditors()
{
    if (activeSampleSetting < 0
        || activeSampleSetting >= static_cast<int>(sampleSettingsRows.size())) return;
    auto& row = sampleSettingsRows[static_cast<std::size_t>(activeSampleSetting)];
    row.name = sampleAliasEditor.getText().trim();
    row.regionStartSeconds = std::max(0.0, sampleStartEditor.getText().getDoubleValue());
    row.regionEndSeconds = std::max(row.regionStartSeconds + 0.001,
                                    sampleEndEditor.getText().getDoubleValue());
    row.alignmentSeconds = juce::jlimit(row.regionStartSeconds, row.regionEndSeconds,
                                        sampleAlignmentEditor.getText().getDoubleValue());
    row.fixedDurationSeconds = juce::jlimit(0.0, row.regionEndSeconds - row.regionStartSeconds,
                                            sampleFixedEditor.getText().getDoubleValue());
    sampleRegionSelector.changeItemText(activeSampleSetting + 1,
        juce::String(activeSampleSetting + 1) + " · " + row.name);
    pianoRoll.setSampleRegions(sampleSettingsRows, activeSampleSetting);
}

void MainComponent::saveSampleSettings()
{
    commitSampleEditors();
    if (!sampleSettingsFile.existsAsFile() || sampleSettingsRows.empty()) return;
    juce::String error;
    if (!SampleSettings::save(sampleSettingsFile, sampleSettingsRows, error))
    {
        showError(error);
        return;
    }
    project.applySourceSettings(sampleSettingsFile, sampleSettingsRows);
    statusLabel.setText(strings.text("sample.saved") + "  "
                        + SampleSettings::sidecarFor(sampleSettingsFile).getFullPathName(),
                        juce::dontSendNotification);
}

void MainComponent::importOto()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("sample.importOto"),
                                                   sampleSettingsFile.getParentDirectory(),
                                                   "oto.ini;*.ini");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& selected)
        {
            const auto file = selected.getResult();
            if (file == juce::File{}) return;
            juce::String error;
            const auto duration = audio.probeDuration(sampleSettingsFile).value_or(0.0);
            if (!SampleSettings::importOto(file, sampleSettingsFile, duration,
                                           sampleSettingsRows, error))
            {
                showError(error);
                return;
            }
            activeSampleSetting = 0;
            sampleRegionSelector.clear(juce::dontSendNotification);
            for (std::size_t index = 0; index < sampleSettingsRows.size(); ++index)
                sampleRegionSelector.addItem(juce::String(index + 1) + " · "
                    + sampleSettingsRows[index].name, static_cast<int>(index + 1));
            sampleRegionSelector.setSelectedId(1, juce::dontSendNotification);
            pianoRoll.setSampleRegions(sampleSettingsRows, activeSampleSetting);
            refreshSampleEditors();
        });
}

void MainComponent::exportOto()
{
    commitSampleEditors();
    if (!sampleSettingsFile.existsAsFile() || sampleSettingsRows.empty()) return;
    chooser = std::make_unique<juce::FileChooser>(strings.text("sample.exportOto"),
        sampleSettingsFile.getParentDirectory().getChildFile("oto.ini"), "*.ini");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& selected)
        {
            auto file = selected.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension("ini")) file = file.withFileExtension("ini");
            const auto duration = audio.probeDuration(sampleSettingsFile).value_or(0.0);
            juce::String error;
            if (!SampleSettings::exportOto(file, sampleSettingsFile, sampleSettingsRows,
                                           duration, error)) showError(error);
        });
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { strings.text("menu.file"), strings.text("menu.edit"), strings.text("menu.track"),
             strings.text("menu.view"), strings.text("menu.help") };
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    if (index == 0)
    {
        menu.addItem(1, strings.text("file.new"));
        menu.addItem(2, strings.text("file.open"));
        menu.addItem(3, strings.text("file.save"));
        menu.addItem(11, strings.text("file.saveAs"));
        juce::PopupMenu recent;
        for (int recentIndex = 0; recentIndex < recentProjectPaths.size(); ++recentIndex)
        {
            const juce::File file(recentProjectPaths[recentIndex]);
            recent.addItem(1'000 + recentIndex,
                file.getFileNameWithoutExtension() + "  —  "
                    + file.getParentDirectory().getFullPathName(),
                file.existsAsFile());
        }
        if (recentProjectPaths.isEmpty())
            recent.addItem(999, strings.text("file.recentEmpty"), false);
        menu.addSubMenu(strings.text("file.recent"), recent);
        menu.addItem(8, strings.text("file.export"));
        menu.addSeparator();
        menu.addItem(4, strings.text("file.audio"));
        menu.addItem(5, strings.text("file.melodyne"));
        menu.addItem(6, strings.text("file.midi"));
        menu.addSeparator();
        menu.addItem(9, strings.text("file.settings"));
        menu.addItem(10, strings.text("file.assets"));
        menu.addSeparator();
        menu.addItem(7, strings.text("file.exit"));
    }
    else if (index == 1)
    {
        menu.addItem(20, strings.text("edit.undo"), project.canUndo());
        menu.addItem(21, strings.text("edit.redo"), project.canRedo());
        menu.addSeparator();
        menu.addItem(22, strings.text("edit.selectAll"));
        menu.addItem(26, strings.text("edit.deselect"));
        menu.addSeparator();
        const auto hasNotes = !pianoRoll.selectedNoteIds().empty();
        menu.addItem(16, strings.text("edit.copyNotes"), hasNotes);
        menu.addItem(17, strings.text("edit.cutNotes"), hasNotes);
        menu.addItem(18, strings.text("edit.pasteNotes"), !copiedNotes.empty()
                     && selectedClipId.isNotEmpty());
        menu.addSeparator();
        menu.addItem(27, strings.text("edit.transposeCents"), hasNotes);
        menu.addItem(28, strings.text("edit.setPitch"), hasNotes);
        menu.addItem(29, strings.text("edit.averagePitch"), hasNotes);
        menu.addItem(19, strings.text("edit.quantizePitch"), hasNotes);
        menu.addSeparator();
        menu.addItem(23, strings.text("edit.copyClip"), selectedClipId.isNotEmpty());
        menu.addItem(24, strings.text("edit.pasteClip"), copiedClipId.isNotEmpty());
        menu.addItem(25, strings.text("edit.duplicateClip"), selectedClipId.isNotEmpty());
    }
    else if (index == 2)
    {
        menu.addItem(36, strings.text("track.addCompose"));
        menu.addItem(37, strings.text("track.addAudio"));
        menu.addItem(30, strings.text("file.audio"));
        menu.addSeparator();
        menu.addItem(38, strings.text("track.rename"), selectedTrackId.isNotEmpty());
        menu.addItem(31, strings.text("track.toggleCompose"), selectedTrackId.isNotEmpty());
        menu.addSeparator();
        auto selectedClipMuted = false;
        if (selectedClipId.isNotEmpty())
        {
            const auto data = project.snapshot();
            for (const auto& track : data.tracks)
                for (const auto& clip : track.clips)
                    if (clip.id == selectedClipId) selectedClipMuted = clip.muted;
        }
        menu.addItem(34, strings.text(selectedClipMuted ? "clip.unmute" : "clip.mute"),
                     selectedClipId.isNotEmpty());
        menu.addItem(35, strings.text("clip.gain"), selectedClipId.isNotEmpty());
        menu.addSeparator();
        menu.addItem(32, strings.text("track.delete"), selectedTrackId.isNotEmpty());
        menu.addItem(33, strings.text("clip.delete"), selectedClipId.isNotEmpty());
    }
    else if (index == 3)
    {
        menu.addItem(40, strings.text("view.zoomIn"));
        menu.addItem(41, strings.text("view.zoomOut"));
        menu.addItem(42, strings.text("view.zoomFit"));
        menu.addItem(44, strings.text("view.vZoomIn"));
        menu.addItem(45, strings.text("view.vZoomOut"));
        menu.addSeparator();
        menu.addItem(43, strings.text("view.showWaveforms"), true, showWaveforms);
    }
    else
        menu.addItem(50, strings.text("help.about"));
    return menu;
}

void MainComponent::menuItemSelected(int id, int)
{
    if (id == 1) newProject();
    else if (id == 2) openProject();
    else if (id == 3) saveProject();
    else if (id == 11) saveProjectAs();
    else if (id == 8) exportMixdown();
    else if (id == 4 || id == 30) importAudio();
    else if (id == 5) importMelodyne();
    else if (id == 6) importMidi();
    else if (id == 9) showSettings();
    else if (id == 10) showAssetManager();
    else if (id == 7) requestClose([] { juce::JUCEApplication::getInstance()->quit(); });
    else if (id >= 1'000 && id < 1'000 + recentProjectPaths.size())
    {
        const juce::File file(recentProjectPaths[id - 1'000]);
        performWithUnsavedCheck([this, file] { loadProjectFile(file); });
    }
    else if (id == 20) project.undo();
    else if (id == 21) project.redo();
    else if (id == 22) pianoRoll.selectAllNotes();
    else if (id == 26) pianoRoll.clearNoteSelection();
    else if (id == 16) copySelectedNotes(false);
    else if (id == 17) copySelectedNotes(true);
    else if (id == 18) pasteCopiedNotes();
    else if (id == 27) showTransposeNotesDialog();
    else if (id == 28) showSetNotesPitchDialog();
    else if (id == 29) project.averageNotesMidi(pianoRoll.selectedNoteIds());
    else if (id == 19) project.quantizeNotesMidi(pianoRoll.selectedNoteIds());
    else if (id == 23) copySelectedClip();
    else if (id == 24) pasteCopiedClip();
    else if (id == 25) duplicateSelectedClip();
    else if (id == 36 || id == 37)
    {
        const auto compose = id == 36;
        selectedTrackId = project.addTrack(
            strings.text(compose ? "track.compose" : "track.audio"), compose);
        trackList.setSelectedTrack(selectedTrackId);
        refreshProjectControls();
        menuItemsChanged();
    }
    else if (id == 38) showRenameTrackDialog();
    else if (id == 31)
    {
        const auto data = project.snapshot();
        const auto found = std::find_if(data.tracks.begin(), data.tracks.end(),
            [this](const auto& track) { return track.id == selectedTrackId; });
        if (found != data.tracks.end()) project.setTrackCompose(found->id, !found->compose);
    }
    else if (id == 32)
    {
        const auto trackId = selectedTrackId;
        confirmDestructive(strings.text("track.delete"),
            [this, trackId] { project.removeTrack(trackId); });
    }
    else if (id == 33)
    {
        const auto clipId = selectedClipId;
        confirmDestructive(strings.text("clip.delete"),
            [this, clipId] { project.removeClip(clipId); });
    }
    else if (id == 34)
    {
        const auto data = project.snapshot();
        for (const auto& track : data.tracks)
            for (const auto& clip : track.clips)
                if (clip.id == selectedClipId)
                {
                    project.setClipMuted(clip.id, !clip.muted);
                    return;
                }
    }
    else if (id == 35) showClipGainDialog();
    else if (id == 40) zoomSlider.setValue(zoomSlider.getValue() * 1.25);
    else if (id == 41) zoomSlider.setValue(zoomSlider.getValue() / 1.25);
    else if (id == 42)
    {
        const auto available = std::max(200, timelineViewport.getWidth());
        zoomSlider.setValue(static_cast<double>(available) / std::max(1.0, project.snapshot().durationSeconds()));
    }
    else if (id == 43)
    {
        showWaveforms = !showWaveforms;
        pianoRoll.setShowWaveforms(showWaveforms);
        if (preferences != nullptr)
            preferences->setValue("ui.showWaveforms", showWaveforms);
        menuItemsChanged();
    }
    else if (id == 44) vZoomSlider.setValue(vZoomSlider.getValue() * 1.2);
    else if (id == 45) vZoomSlider.setValue(vZoomSlider.getValue() / 1.2);
    else if (id == 50)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            strings.text("app.title"), strings.text("help.aboutText"));
}

void MainComponent::showSettings()
{
    if (preferences == nullptr) return;
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = strings.text("settings.title");
    options.dialogBackgroundColour = Palette::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    juce::Component::SafePointer<MainComponent> safe(this);
    options.content.setOwned(new SettingsComponent(strings, audio.devices(), *preferences,
        [safe]
        {
            if (safe == nullptr) return;
            if (safe->preferences != nullptr)
                safe->audio.saveDeviceState(*safe->preferences);
            safe->applyPreferences();
            safe->refreshTexts();
            safe->menuItemsChanged();
            safe->repaint();
            safe->trackList.repaint();
            safe->timeline.repaint();
            safe->pianoRoll.repaint();
        }));
    options.launchAsync();
}

void MainComponent::showAssetManager()
{
    if (preferences == nullptr) return;
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = strings.text("asset.title");
    options.dialogBackgroundColour = Palette::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.content.setOwned(new AssetManagerComponent(strings, *preferences));
    options.launchAsync();
}

void MainComponent::showClipGainDialog()
{
    if (selectedClipId.isEmpty()) return;
    auto gain = 1.0f;
    auto found = false;
    const auto data = project.snapshot();
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            if (clip.id == selectedClipId)
            {
                gain = clip.gain;
                found = true;
            }
    if (!found) return;

    const auto gainDb = gain > 1.0e-6f ? 20.0 * std::log10(gain) : -60.0;
    auto* dialog = new juce::AlertWindow(strings.text("clip.gain"), juce::String{},
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("gain", juce::String(gainDb, 1), strings.text("clip.gainDb"));
    dialog->addButton(strings.text("dialog.apply"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(strings.text("dialog.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    const auto clipId = selectedClipId;
    juce::Component::SafePointer<MainComponent> safe(this);
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([safe, dialog, clipId](int result)
        {
            if (safe != nullptr && result == 1)
            {
                const auto db = juce::jlimit(-60.0, 12.0,
                    dialog->getTextEditorContents("gain").getDoubleValue());
                safe->project.setClipGain(clipId, db <= -59.9 ? 0.0f
                    : static_cast<float>(std::pow(10.0, db / 20.0)));
            }
            delete dialog;
        }), false);
}

void MainComponent::showRenameTrackDialog()
{
    if (selectedTrackId.isEmpty()) return;
    auto currentName = juce::String{};
    for (const auto& track : project.snapshot().tracks)
        if (track.id == selectedTrackId)
        {
            currentName = track.name;
            break;
        }
    if (currentName.isEmpty()) return;
    auto* dialog = new juce::AlertWindow(strings.text("track.rename"), juce::String{},
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", currentName, strings.text("track.name"));
    dialog->addButton(strings.text("dialog.apply"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(strings.text("dialog.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    const auto trackId = selectedTrackId;
    juce::Component::SafePointer<MainComponent> safe(this);
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([safe, dialog, trackId](int result)
        {
            if (safe != nullptr && result == 1)
                safe->project.setTrackName(trackId,
                    dialog->getTextEditorContents("name"));
            delete dialog;
        }), false);
}

void MainComponent::showTransposeNotesDialog()
{
    const auto noteIds = pianoRoll.selectedNoteIds();
    if (noteIds.empty()) return;
    auto* dialog = new juce::AlertWindow(strings.text("edit.transposeCents"), juce::String{},
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("cents", "0", strings.text("edit.cents"));
    dialog->addButton(strings.text("dialog.apply"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(strings.text("dialog.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe(this);
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([safe, dialog, noteIds](int result)
        {
            if (safe != nullptr && result == 1)
            {
                const auto cents = juce::jlimit(-4'800.0, 4'800.0,
                    dialog->getTextEditorContents("cents").getDoubleValue());
                safe->project.transposeNotes(noteIds, static_cast<float>(cents / 100.0));
            }
            delete dialog;
        }), false);
}

void MainComponent::showSetNotesPitchDialog()
{
    const auto noteIds = pianoRoll.selectedNoteIds();
    if (noteIds.empty()) return;
    auto initial = 60.0f;
    auto found = false;
    for (const auto& track : project.snapshot().tracks)
        for (const auto& clip : track.clips)
            for (const auto& note : clip.notes)
                if (std::find(noteIds.begin(), noteIds.end(), note.id) != noteIds.end())
                {
                    initial = note.midiNote;
                    found = true;
                    break;
                }
    if (!found) return;
    auto* dialog = new juce::AlertWindow(strings.text("edit.setPitch"), juce::String{},
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("midi", juce::String(initial, 2), strings.text("edit.midiNote"));
    dialog->addButton(strings.text("dialog.apply"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(strings.text("dialog.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe(this);
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([safe, dialog, noteIds](int result)
        {
            if (safe != nullptr && result == 1)
                safe->project.setNotesMidi(noteIds, static_cast<float>(
                    dialog->getTextEditorContents("midi").getDoubleValue()));
            delete dialog;
        }), false);
}

void MainComponent::copySelectedNotes(bool cut)
{
    const auto ids = pianoRoll.selectedNoteIds();
    if (ids.empty()) return;
    std::vector<std::pair<double, NoteData>> notes;
    for (const auto& track : project.snapshot().tracks)
        for (const auto& clip : track.clips)
            for (const auto& note : clip.notes)
                if (std::find(ids.begin(), ids.end(), note.id) != ids.end())
                    notes.emplace_back(clip.startSeconds + note.startSeconds, note);
    if (notes.empty()) return;
    std::stable_sort(notes.begin(), notes.end(), [](const auto& left, const auto& right)
    {
        return left.first < right.first;
    });
    const auto origin = notes.front().first;
    copiedClipId.clear();
    copiedNotes.clear();
    copiedNotes.reserve(notes.size());
    for (auto& [absolute, note] : notes)
    {
        note.startSeconds = absolute - origin;
        copiedNotes.push_back(std::move(note));
    }
    copiedNotes.front().connectedToPrevious = false;
    copiedNotes.back().connectedToNext = false;
    if (cut)
    {
        project.removeNotes(ids);
        pianoRoll.clearNoteSelection();
    }
    statusLabel.setText(strings.text(cut ? "status.notesCut" : "status.notesCopied"),
                        juce::dontSendNotification);
    menuItemsChanged();
}

void MainComponent::pasteCopiedNotes()
{
    if (copiedNotes.empty() || selectedClipId.isEmpty()) return;
    const auto inserted = project.insertNotes(selectedClipId, copiedNotes,
                                               std::max(0.0, audio.position()));
    if (inserted.empty()) return;
    pianoRoll.setSelectedNoteIds(inserted);
    focusNote(inserted.front());
    statusLabel.setText(strings.text("status.notesPasted"),
                        juce::dontSendNotification);
    menuItemsChanged();
}

void MainComponent::copySelectedClip()
{
    if (selectedClipId.isEmpty()) return;
    const auto data = project.snapshot();
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            if (clip.id == selectedClipId)
            {
                copiedNotes.clear();
                copiedClipId = clip.id;
                selectedTrackId = track.id;
                statusLabel.setText(strings.text("status.clipCopied"),
                                    juce::dontSendNotification);
                menuItemsChanged();
                return;
            }
}

void MainComponent::pasteCopiedClip()
{
    if (copiedClipId.isEmpty()) return;
    auto pasteSeconds = std::max(0.0, audio.position());
    const auto data = project.snapshot();
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            if (clip.id == copiedClipId && pasteSeconds <= clip.startSeconds + 1.0e-6)
                pasteSeconds = clip.startSeconds + clip.durationSeconds;
    const auto inserted = project.duplicateClip(copiedClipId, pasteSeconds,
                                                selectedTrackId);
    if (inserted.isNotEmpty())
    {
        focusClip(inserted);
        statusLabel.setText(strings.text("status.clipPasted"),
                            juce::dontSendNotification);
    }
    else
    {
        copiedClipId.clear();
        menuItemsChanged();
    }
}

void MainComponent::duplicateSelectedClip()
{
    if (selectedClipId.isEmpty()) return;
    const auto inserted = project.duplicateClip(selectedClipId, -1.0,
                                                selectedTrackId);
    if (inserted.isNotEmpty())
    {
        copiedClipId = selectedClipId;
        focusClip(inserted);
        statusLabel.setText(strings.text("status.clipPasted"),
                            juce::dontSendNotification);
        menuItemsChanged();
    }
}

void MainComponent::confirmDestructive(const juce::String& title,
                                       std::function<void()> action)
{
    if (preferences == nullptr
        || !preferences->getBoolValue("operation.confirmDestructive", true))
    {
        if (action) action();
        return;
    }
    auto* dialog = new juce::AlertWindow(title, strings.text("dialog.destructiveMessage"),
                                          juce::MessageBoxIconType::WarningIcon);
    dialog->addButton(strings.text("dialog.delete"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(strings.text("dialog.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe(this);
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [safe, dialog, action = std::move(action)](int result) mutable
            {
                if (safe != nullptr && result == 1 && action) action();
                delete dialog;
            }), false);
}

void MainComponent::newProject()
{
    performWithUnsavedCheck([this]
    {
        audio.stop();
        audio.setPosition(0.0);
        project.clear();
        currentProjectFile = juce::File{};
        savedProjectRevision = project.revisionNumber();
        selectedTrackId.clear();
        selectedClipId.clear();
        selectedNoteId.clear();
        copiedClipId.clear();
        copiedNotes.clear();
        pianoRoll.clearNoteSelection();
        pianoRoll.setFocusedClip({});
        statusLabel.setText(strings.text("status.ready"), juce::dontSendNotification);
    });
}

void MainComponent::openProject()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.open"), juce::File{}, "*.hjpx;*.hspx");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& selected)
        {
            const auto file = selected.getResult();
            if (file == juce::File{}) return;
            performWithUnsavedCheck([this, file] { loadProjectFile(file); });
        });
}

void MainComponent::loadProjectFile(const juce::File& file)
{
    juce::String error;
    if (!project.load(file, error))
    {
        showError(error);
        return;
    }
    currentProjectFile = file;
    savedProjectRevision = project.revisionNumber();
    addRecentProject(file);
    statusLabel.setText(strings.text("status.projectOpened") + "  " + file.getFileName(),
                        juce::dontSendNotification);
    if (error.isNotEmpty())
        showError(strings.text("warning.missingMedia") + "\n" + error);
}

void MainComponent::saveProject(std::function<void(bool)> completion)
{
    if (currentProjectFile != juce::File{})
    {
        const auto saved = saveProjectTo(currentProjectFile);
        if (completion) completion(saved);
        return;
    }
    saveProjectAs(std::move(completion));
}

void MainComponent::saveProjectAs(std::function<void(bool)> completion)
{
    const auto initial = currentProjectFile != juce::File{} ? currentProjectFile
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(project.snapshot().name + ".hjpx");
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.saveAs"),
                                                   initial,
                                                   "*.hjpx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, completion = std::move(completion)](const juce::FileChooser& selected) mutable
        {
            auto file = selected.getResult();
            if (file == juce::File{})
            {
                if (completion) completion(false);
                return;
            }
            if (!file.hasFileExtension("hjpx")) file = file.withFileExtension("hjpx");
            const auto saved = saveProjectTo(file);
            if (completion) completion(saved);
        });
}

bool MainComponent::saveProjectTo(const juce::File& file)
{
    juce::String error;
    if (!project.save(file, error))
    {
        showError(error);
        return false;
    }
    currentProjectFile = file;
    savedProjectRevision = project.revisionNumber();
    addRecentProject(file);
    statusLabel.setText(strings.text("status.projectSaved") + "  " + file.getFileName(),
                        juce::dontSendNotification);
    return true;
}

void MainComponent::performWithUnsavedCheck(std::function<void()> action)
{
    if (project.revisionNumber() == savedProjectRevision)
    {
        if (action) action();
        return;
    }
    auto* dialog = new juce::AlertWindow(strings.text("dialog.unsavedTitle"),
        strings.text("dialog.unsavedMessage"), juce::MessageBoxIconType::WarningIcon);
    dialog->addButton(strings.text("dialog.save"), 1);
    dialog->addButton(strings.text("dialog.discard"), 2);
    dialog->addButton(strings.text("dialog.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe(this);
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [safe, dialog, action = std::move(action)](int result) mutable
            {
                if (safe != nullptr && result == 1)
                    safe->saveProject([safe, action = std::move(action)](bool saved) mutable
                    {
                        if (safe != nullptr && saved && action) action();
                    });
                else if (safe != nullptr && result == 2 && action)
                    action();
                delete dialog;
            }), false);
}

void MainComponent::requestClose(std::function<void()> approved)
{
    performWithUnsavedCheck(std::move(approved));
}

void MainComponent::restoreRecentProjects()
{
    recentProjectPaths.clear();
    if (preferences == nullptr) return;
    juce::StringArray stored;
    stored.addLines(preferences->getValue("files.recentProjects"));
    for (const auto& path : stored)
    {
        const juce::File file(path);
        if (file.existsAsFile() && !recentProjectPaths.contains(file.getFullPathName()))
            recentProjectPaths.add(file.getFullPathName());
        if (recentProjectPaths.size() >= 8) break;
    }
}

void MainComponent::addRecentProject(const juce::File& file)
{
    if (file == juce::File{}) return;
    const auto path = file.getFullPathName();
    recentProjectPaths.removeString(path, true);
    recentProjectPaths.insert(0, path);
    while (recentProjectPaths.size() > 8) recentProjectPaths.remove(8);
    if (preferences != nullptr)
    {
        preferences->setValue("files.recentProjects", recentProjectPaths.joinIntoString("\n"));
        preferences->saveIfNeeded();
    }
    menuItemsChanged();
}

void MainComponent::exportMixdown()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.export"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(project.snapshot().name + ".wav"), "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& selected)
        {
            auto file = selected.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension("wav")) file = file.withFileExtension("wav");
            statusLabel.setText(strings.text("status.exporting"), juce::dontSendNotification);
            repaint();
            juce::String error;
            if (!audio.exportWav(file, error))
                showError(strings.text("error.export") + "\n" + error);
            statusLabel.setText(strings.text("status.ready"), juce::dontSendNotification);
        });
}

void MainComponent::importAudio()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.audio"), juce::File{},
                                                   "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& selected)
        {
            const auto files = selected.getResults();
            const auto startSeconds = audio.position();
            for (const auto& file : files)
                if (const auto duration = audio.probeDuration(file))
                    addAnalysedAudioFile(file, *duration, startSeconds);
                else
                    showError(strings.text("error.audio") + "\n" + file.getFullPathName());
        });
}

void MainComponent::addAnalysedAudioFile(const juce::File& file, double durationSeconds,
                                         double startSeconds,
                                         const juce::String& targetTrackId)
{
    const auto clipId = project.addAudioFile(file, durationSeconds, startSeconds, targetTrackId);
    const auto nativeSidecar = SampleSettings::sidecarFor(file);
    const juce::File legacySidecar(file.getFullPathName() + ".hachi.csv");
    // HJM/OTO data is authoritative.  Acoustic analysis must not overwrite
    // explicitly authored sample regions.
    if (!nativeSidecar.existsAsFile() && !legacySidecar.existsAsFile())
        scheduleAnalysis(file, clipId);
}

void MainComponent::scheduleAnalysis(const juce::File& file,
                                     const juce::String& clipId)
{
    juce::Component::SafePointer<MainComponent> safe(this);
    const auto analysisConfig = backend::AnalysisService::configFromProperties(preferences.get());
    ++pendingNativeAnalyses;
    nativeAnalysisProgress = 0.0;
    nativeAnalysisName = file.getFileName();
    statusLabel.setText(strings.text("status.analyzing") + "  " + file.getFileName(),
                        juce::dontSendNotification);
    std::thread([safe, file, clipId, analysisConfig]
    {
        juce::String error;
        auto result = backend::AnalysisService::analyse(file, analysisConfig, error,
            [safe, name = file.getFileName()](double value)
            {
                juce::MessageManager::callAsync([safe, name, value]
                {
                    if (safe == nullptr || safe->importInProgress) return;
                    safe->nativeAnalysisName = name;
                    safe->nativeAnalysisProgress = value;
                });
            });
        juce::MessageManager::callAsync(
            [safe, clipId, result = std::move(result), error]() mutable
            {
                if (safe == nullptr) return;
                const auto backendName = backend::AnalysisService::backendText(result.status);
                const auto inserted = safe->project.setClipNotesIfEmpty(
                    clipId, std::move(result.notes));
                safe->pendingNativeAnalyses = std::max(0, safe->pendingNativeAnalyses - 1);
                safe->nativeAnalysisProgress = inserted ? 1.0 : 0.0;
                if (inserted)
                    safe->statusLabel.setText(safe->strings.text("status.analysisComplete")
                                                + " · " + backendName,
                                              juce::dontSendNotification);
                else if (error.isNotEmpty())
                    safe->statusLabel.setText(safe->strings.text("status.analysisSkipped"),
                                              juce::dontSendNotification);
            });
    }).detach();
}

void MainComponent::importMelodyne()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.melodyne"), juce::File{}, "*.mpd");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& selected)
        {
            const auto file = selected.getResult();
            if (file == juce::File{}) return;
            loadMelodyneFile(file);
        });
}

void MainComponent::importMidi()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.midi"), juce::File{}, "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& selected)
        {
            const auto file = selected.getResult();
            if (file == juce::File{}) return;
            juce::String error;
            if (!project.addMidiFile(file, error))
                showError(strings.text("error.midi") + "\n" + error);
        });
}

void MainComponent::loadMelodyneFile(const juce::File& file)
{
    importInProgress = true;
    progress = 0.01;
    statusLabel.setText(strings.text("status.loading"), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe(this);
    const auto recursiveMediaSearch = preferences == nullptr
        || preferences->getBoolValue("import.recursiveMedia", true);
    const auto preserveProjectEdits = preferences == nullptr
        || preferences->getBoolValue("import.preserveEdits", true);
    const auto reanalyseSourcePitch = preferences != nullptr
        && preferences->getIntValue("import.melodynePitchSource", 1) == 2;
    const auto analysisConfig = backend::AnalysisService::configFromProperties(preferences.get());
    juce::Thread::launch([safe, file, recursiveMediaSearch, preserveProjectEdits,
                          reanalyseSourcePitch, analysisConfig]
    {
        juce::String error;
        backend::MelodyneImportOptions options;
        options.recursiveMediaSearch = recursiveMediaSearch;
        options.preserveProjectEdits = preserveProjectEdits;
        auto imported = backend::MelodyneImporter::importProject(file, error,
            [safe, reanalyseSourcePitch](double value, const juce::String& stage)
            {
                const auto scaled = reanalyseSourcePitch ? value * 0.75 : value;
                juce::MessageManager::callAsync([safe, scaled, stage]
                {
                    if (safe == nullptr) return;
                    safe->progress = scaled;
                    safe->statusLabel.setText(safe->strings.text("status.loading") + "  "
                                                + safe->strings.text(juce::String("mpd.stage.") + stage),
                                              juce::dontSendNotification);
                });
            }, options);
        auto pitchReanalysed = false;
        backend::AnalysisStatus analysisStatus;
        if (imported && reanalyseSourcePitch)
        {
            juce::String pitchError;
            pitchReanalysed = backend::AnalysisService::reanalyseProjectSourcePitch(
                imported->project, analysisConfig, pitchError, [safe](double value)
                {
                    juce::MessageManager::callAsync([safe, value]
                    {
                        if (safe == nullptr) return;
                        safe->progress = 0.75 + value * 0.25;
                        safe->statusLabel.setText(
                            safe->strings.text("status.analyzing") + "  "
                                + safe->strings.text("mpd.stage.reanalyse_pitch"),
                            juce::dontSendNotification);
                    });
                }, &analysisStatus);
        }
        juce::MessageManager::callAsync([safe, imported = std::move(imported), error,
                                         reanalyseSourcePitch, pitchReanalysed,
                                         analysisStatus]() mutable
        {
            if (safe == nullptr) return;
            safe->importInProgress = false;
            safe->progress = 0.0;
            if (!imported)
            {
                safe->showError(safe->strings.text("error.mpd") + "\n" + error);
                return;
            }
            if (reanalyseSourcePitch)
            {
                safe->statusLabel.setText(safe->strings.text(
                    pitchReanalysed ? "status.analysisComplete" : "status.analysisSkipped"),
                    juce::dontSendNotification);
                safe->statusLabel.setText(safe->statusLabel.getText() + " · "
                    + backend::AnalysisService::backendText(analysisStatus),
                    juce::dontSendNotification);
            }
            safe->presentMelodyneComposeSelection(std::move(*imported));
        });
    });
}

void MainComponent::presentMelodyneComposeSelection(backend::MelodyneImportResult imported)
{
    const auto algorithmId = preferences != nullptr
        ? preferences->getIntValue("import.algorithm", 1) : 1;
    const auto importedPitch = algorithmId == 2 ? PitchAlgorithm::nsfHifigan
        : algorithmId == 3 ? PitchAlgorithm::world
        : algorithmId == 4 ? PitchAlgorithm::vocalShifter
        : algorithmId == 5 ? PitchAlgorithm::mld3
        : algorithmId == 6 ? PitchAlgorithm::llsm2 : PitchAlgorithm::mld5;
    const auto stretchAlgorithmId = preferences != nullptr
        ? preferences->getIntValue("import.stretchAlgorithm", 1) : 1;
    auto importedStretch = stretchAlgorithmId == 2 ? StretchAlgorithm::variableMelHop
        : stretchAlgorithmId == 3 ? StretchAlgorithm::loop
        : stretchAlgorithmId == 4 ? StretchAlgorithm::soundTouch
        : stretchAlgorithmId == 5 ? StretchAlgorithm::nsfShiftThenSplice
        : StretchAlgorithm::melodyneHybrid;
    // The two NSF variable-mel-hop orders are the NSF-HiFiGAN-specific
    // duration paths.  Keep an imported project immediately renderable when
    // another pitch backend is selected in Settings, matching the toolbar's
    // available choices.
    if (importedPitch != PitchAlgorithm::nsfHifigan
        && (importedStretch == StretchAlgorithm::variableMelHop
            || importedStretch == StretchAlgorithm::nsfShiftThenSplice))
        importedStretch = StretchAlgorithm::melodyneHybrid;
    for (auto& track : imported.project.tracks)
    {
        track.pitchAlgorithm = importedPitch;
        track.stretchAlgorithm = importedStretch;
    }

    const auto composeMode = preferences != nullptr
        ? preferences->getIntValue("import.melodyneCompose", 1) : 1;
    if (composeMode != 1)
    {
        for (auto& track : imported.project.tracks)
        {
            if (composeMode == 3) track.compose = true;
            else if (composeMode == 4) track.compose = false;
            // Mode 2 retains the melodic classification stored by Melodyne.
        }
        project.replace(std::move(imported.project));
        if (!imported.missingFiles.isEmpty())
            showError(strings.text("warning.missingMedia") + "\n"
                      + imported.missingFiles.joinIntoString("\n"));
        return;
    }
    auto state = std::make_shared<backend::MelodyneImportResult>(std::move(imported));
    auto* selector = new ComposeTrackSelector(state->project.tracks, strings);
    auto* dialog = new juce::AlertWindow(strings.text("mpd.compose.title"),
                                          strings.text("mpd.compose.description"),
                                          juce::MessageBoxIconType::QuestionIcon);
    dialog->addCustomComponent(selector);
    dialog->addButton(strings.text("dialog.import"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(strings.text("dialog.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe(this);
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([safe, dialog, selector, state](int result)
        {
            if (safe != nullptr && result == 1)
            {
                for (std::size_t index = 0; index < state->project.tracks.size(); ++index)
                    state->project.tracks[index].compose = selector->isCompose(index);
                safe->project.replace(std::move(state->project));
                if (!state->missingFiles.isEmpty())
                    safe->showError(safe->strings.text("warning.missingMedia") + "\n"
                                    + state->missingFiles.joinIntoString("\n"));
            }
            dialog->removeCustomComponent(0);
            delete selector;
            delete dialog;
        }), false);
}

void MainComponent::showError(const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                            strings.text("app.title"), message);
}
}
