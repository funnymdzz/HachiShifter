#include "MainComponent.h"
#include <array>
#include <cmath>

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
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);
    setWantsKeyboardFocus(true);

    for (auto* button : { &openButton, &saveButton, &audioButton, &melodyneButton,
                          &playButton, &stopButton, &noteEditButton, &wrenchButton,
                          &drawButton, &lineButton, &connectButton, &pitchParamButton,
                          &breathParamButton, &tensionParamButton, &formantParamButton,
                          &volumeParamButton, &midiButton })
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
                             static_cast<juce::Component*>(&languageSelector),
                             static_cast<juce::Component*>(&pitchAlgorithm),
                             static_cast<juce::Component*>(&stretchAlgorithm),
                             static_cast<juce::Component*>(&pitchLabel),
                             static_cast<juce::Component*>(&stretchLabel),
                             static_cast<juce::Component*>(&statusLabel),
                             static_cast<juce::Component*>(&sourceEditHint),
                             static_cast<juce::Component*>(&parameterTitle),
                             static_cast<juce::Component*>(&smoothCaption),
                             static_cast<juce::Component*>(&smoothSlider),
                             static_cast<juce::Component*>(&zoomSlider),
                             static_cast<juce::Component*>(&progressBar),
                             static_cast<juce::Component*>(&trackViewport),
                             static_cast<juce::Component*>(&timelineViewport),
                             static_cast<juce::Component*>(&pianoViewport),
                             static_cast<juce::Component*>(&panelSplitter) })
        addAndMakeVisible(*component);

    panelSplitter.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    panelSplitter.addMouseListener(this, false);

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

    languageSelector.addItem("简体中文", 1);
    languageSelector.addItem("繁體中文", 2);
    languageSelector.addItem("日本語", 3);
    languageSelector.addItem("한국어", 4);
    languageSelector.addItem("English", 5);
    languageSelector.setSelectedId(static_cast<int>(strings.getLanguage()) + 1, juce::dontSendNotification);
    languageSelector.onChange = [this]
    {
        strings.setLanguage(static_cast<I18n::Language>(languageSelector.getSelectedId() - 1));
        refreshTexts();
        menuItemsChanged();
        trackList.repaint();
    };

    trackViewport.setViewedComponent(&trackList, false);
    trackViewport.setScrollBarsShown(true, false);
    trackViewport.setScrollBarThickness(10);
    timelineViewport.setViewedComponent(&timeline, false);
    timelineViewport.setScrollBarsShown(true, true);
    timelineViewport.setScrollBarThickness(10);
    pianoViewport.setViewedComponent(&pianoRoll, false);
    pianoViewport.setScrollBarsShown(true, true);
    pianoViewport.setScrollBarThickness(10);

    pitchAlgorithm.addItem("mld5", 1);
    pitchAlgorithm.addItem("nsf-hifigan", 2);
    pitchAlgorithm.addItem("WORLD", 3);
    pitchAlgorithm.addItem("vslib", 4);
    pitchAlgorithm.setSelectedId(1);
    stretchAlgorithm.addItem("Melodyne Hybrid", 1);
    stretchAlgorithm.addItem("Variable Mel Hop", 2);
    stretchAlgorithm.addItem("Loop", 3);
    stretchAlgorithm.addItem("SoundTouch", 4);
    stretchAlgorithm.setSelectedId(1);
    pitchAlgorithm.onChange = [this]
    {
        const auto id = pitchAlgorithm.getSelectedId();
        project.setPitchAlgorithm(id == 2 ? PitchAlgorithm::nsfHifigan
                                  : id == 3 ? PitchAlgorithm::world
                                  : id == 4 ? PitchAlgorithm::vocalShifter : PitchAlgorithm::mld5);
    };
    stretchAlgorithm.onChange = [this]
    {
        const auto id = stretchAlgorithm.getSelectedId();
        project.setStretchAlgorithm(id == 2 ? StretchAlgorithm::variableMelHop
                                    : id == 3 ? StretchAlgorithm::loop
                                    : id == 4 ? StretchAlgorithm::soundTouch : StretchAlgorithm::melodyneHybrid);
    };

    smoothSlider.setRange(0.0, 100.0, 1.0);
    smoothSlider.setValue(35.0);
    smoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    zoomSlider.setRange(40.0, 600.0, 1.0);
    zoomSlider.setValue(140.0);
    zoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.onValueChange = [this]
    {
        const auto zoom = static_cast<float>(zoomSlider.getValue());
        timeline.setPixelsPerSecond(zoom);
        pianoRoll.setPixelsPerSecond(zoom);
    };
    timeline.onSeek = [this](double seconds) { audio.setPosition(seconds); };
    timeline.onClipSelected = [this](const juce::String& clipId) { focusClip(clipId); };
    pianoRoll.onSeek = [this](double seconds) { audio.setPosition(seconds); };
    trackList.peakProvider = [this](const juce::String& trackId) { return audio.trackPeak(trackId); };

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
    noteEditButton.onClick = [this] { setSourceEditMode(false); };
    wrenchButton.onClick = [this] { setSourceEditMode(true); };
    drawButton.onClick = [this] { setToolButton(drawButton); };
    lineButton.onClick = [this] { setToolButton(lineButton); };
    connectButton.onClick = [this] { setToolButton(connectButton); };
    for (auto* button : { &pitchParamButton, &breathParamButton, &tensionParamButton,
                          &formantParamButton, &volumeParamButton })
        button->onClick = [this, button] { setToolButton(*button); };
    midiButton.onClick = [this] { importMidi(); };
    noteEditButton.setClickingTogglesState(false);
    wrenchButton.setClickingTogglesState(false);

    project.addChangeListener(this);
    audio.addChangeListener(this);
    refreshTexts();
    refreshProjectControls();
    setSourceEditMode(false);
    pitchParamButton.setToggleState(true, juce::dontSendNotification);
    setSize(1280, 760);
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    panelSplitter.removeMouseListener(this);
    audio.removeChangeListener(this);
    project.removeChangeListener(this);
    menuBar.setModel(nullptr);
    setLookAndFeel(nullptr);
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
    breathParamButton.setButtonText(strings.text("param.breath"));
    tensionParamButton.setButtonText(strings.text("param.tension"));
    formantParamButton.setButtonText(strings.text("param.formant"));
    volumeParamButton.setButtonText(strings.text("param.volume"));
    midiButton.setButtonText(strings.text("file.midi"));
    bpmCaption.setText("BPM", juce::dontSendNotification);
    beatsCaption.setText(strings.text("beats.bar"), juce::dontSendNotification);
    denominatorLabel.setText("/ 4", juce::dontSendNotification);
    gridCaption.setText(strings.text("grid"), juce::dontSendNotification);
    scaleCaption.setText(strings.text("base.scale"), juce::dontSendNotification);
    pitchLabel.setText(strings.text("algo.pitch"), juce::dontSendNotification);
    stretchLabel.setText(strings.text("algo.stretch"), juce::dontSendNotification);
    statusLabel.setText(strings.text("status.ready"), juce::dontSendNotification);
    sourceEditHint.setText(strings.text("edit.source"), juce::dontSendNotification);
}

void MainComponent::refreshProjectControls()
{
    const auto data = project.snapshot();
    bpmEditor.setText(juce::String(data.bpm, std::abs(data.bpm - std::floor(data.bpm)) < 1.0e-9 ? 0 : 2),
                      juce::dontSendNotification);
    beatsEditor.setText(juce::String(data.numerator), juce::dontSendNotification);
    gridSelector.setText(data.gridDivision, juce::dontSendNotification);
    scaleSelector.setText(data.baseScale, juce::dontSendNotification);
}

void MainComponent::setToolButton(juce::Button& selected)
{
    const std::array<juce::Button*, 5> editTools {
        &noteEditButton, &wrenchButton, &drawButton, &lineButton, &connectButton
    };
    for (auto* button : editTools)
        button->setToggleState(button == &selected, juce::dontSendNotification);
    const std::array<juce::Button*, 5> parameterTools {
        &pitchParamButton, &breathParamButton, &tensionParamButton, &formantParamButton, &volumeParamButton
    };
    for (auto* button : parameterTools)
        if (&selected == button || &selected == &pitchParamButton || &selected == &breathParamButton
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
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayback();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O')
    {
        openProject();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
    {
        saveProject();
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
        if (juce::File(path).hasFileExtension("wav;flac;aif;aiff;mp3;ogg;hspx;mpd;mid;midi"))
            return true;
    return false;
}

void MainComponent::openExternalFile(const juce::File& file)
{
    if (file.hasFileExtension("mpd"))
        loadMelodyneFile(file);
    else if (file.hasFileExtension("hspx"))
    {
        juce::String error;
        if (!project.load(file, error)) showError(error);
    }
    else if (file.hasFileExtension("mid;midi"))
    {
        juce::String error;
        if (!project.addMidiFile(file, error))
            showError(strings.text("error.midi") + "\n" + error);
    }
    else if (const auto duration = audio.probeDuration(file))
        project.addAudioFile(file, *duration);
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    auto dropSeconds = audio.position();
    if (timelineViewport.getBounds().contains(x, y))
        dropSeconds = timeline.secondsForPixel(x - timelineViewport.getX()
                                               + timelineViewport.getViewPositionX());
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("wav;flac;aif;aiff;mp3;ogg"))
        {
            if (const auto duration = audio.probeDuration(file))
                project.addAudioFile(file, *duration, dropSeconds);
        }
        else
            openExternalFile(file);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto menu = area.removeFromTop(27);
    languageSelector.setBounds(menu.removeFromRight(112).reduced(2));
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
    zoomSlider.setBounds(toolbar.removeFromRight(100));
    progressBar.setBounds(toolbar.removeFromRight(100).reduced(4, 5));

    auto footer = area.removeFromBottom(24);
    statusLabel.setBounds(footer.reduced(8, 0));
    const auto splitAvailable = std::max(1, area.getHeight() - 8 - 36);
    const auto upperHeight = juce::jlimit(150, std::max(150, splitAvailable - 120),
        static_cast<int>(std::round(static_cast<float>(splitAvailable) * panelSplitRatio)));
    auto upper = area.removeFromTop(upperHeight);
    trackViewport.setBounds(upper.removeFromLeft(226));
    timelineViewport.setBounds(upper);

    panelSplitter.setBounds(area.removeFromTop(8));

    auto parameterHeader = area.removeFromTop(36).reduced(4, 3);
    auto takeParameter = [&parameterHeader](juce::Component& component, int width)
    {
        component.setBounds(parameterHeader.removeFromLeft(width));
        parameterHeader.removeFromLeft(3);
    };
    takeParameter(parameterTitle, 82);
    takeParameter(noteEditButton, 27);
    takeParameter(drawButton, 27);
    takeParameter(lineButton, 27);
    takeParameter(wrenchButton, 27);
    takeParameter(connectButton, 27);
    takeParameter(smoothCaption, 42);
    takeParameter(smoothSlider, 82);
    takeParameter(pitchParamButton, 58);
    takeParameter(breathParamButton, 58);
    takeParameter(tensionParamButton, 58);
    takeParameter(formantParamButton, 58);
    takeParameter(volumeParamButton, 58);
    takeParameter(pitchAlgorithm, 108);
    takeParameter(stretchAlgorithm, 128);
    takeParameter(midiButton, 76);
    sourceEditHint.setBounds(parameterHeader.reduced(3, 0));
    pitchLabel.setBounds({});
    stretchLabel.setBounds({});
    pianoViewport.setBounds(area);
    if (!pianoInitialScrollSet && pianoViewport.getHeight() > 0)
    {
        pianoViewport.setViewPosition(0, std::max(0, (pianoRoll.getHeight() - pianoViewport.getHeight()) / 2));
        pianoInitialScrollSet = true;
    }
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.eventComponent != &panelSplitter) return;
    draggingPanelSplitter = true;
    panelSplitterDragScreenY = event.getScreenY();
    panelSplitterDragRatio = panelSplitRatio;
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
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
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &project)
    {
        audio.syncProject(project.snapshot());
        refreshProjectControls();
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
            statusLabel.setText((audio.isPlaying() ? strings.text("transport.play")
                                                    : strings.text("status.ready"))
                                    + "  " + juce::String(audio.position(), 2) + " s",
                                juce::dontSendNotification);
            if (playWhenRenderReady)
            {
                playWhenRenderReady = false;
                audio.play();
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
    }
    else
    {
        audio.clearAuditionFile();
        pianoViewport.setViewPosition(timelineViewport.getViewPositionX(), pianoViewport.getViewPositionY());
    }
    noteEditButton.setToggleState(!enabled, juce::dontSendNotification);
    wrenchButton.setToggleState(enabled, juce::dontSendNotification);
    sourceEditHint.setVisible(enabled);
}

void MainComponent::focusClip(const juce::String& clipId)
{
    selectedClipId = clipId;
    pianoRoll.setFocusedClip(clipId);
    if (!sourceEditActive) return;
    if (clipId.isEmpty())
    {
        audio.clearAuditionFile();
        return;
    }
    const auto data = project.snapshot();
    for (const auto& track : data.tracks)
        for (const auto& clip : track.clips)
            if (clip.id == clipId)
            {
                audio.setAuditionFile(clip.sourceFile);
                pianoViewport.setViewPosition(std::max(0, pianoRoll.pixelForSeconds(clip.sourceOffsetSeconds)
                                                          - pianoViewport.getViewWidth() / 4),
                                              pianoViewport.getViewPositionY());
                return;
            }
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
        menu.addSeparator();
        menu.addItem(4, strings.text("file.audio"));
        menu.addItem(5, strings.text("file.melodyne"));
        menu.addItem(6, strings.text("file.midi"));
        menu.addSeparator();
        menu.addItem(7, strings.text("file.exit"));
    }
    else if (index == 1)
    {
        menu.addItem(20, strings.text("edit.undo"), false);
        menu.addItem(21, strings.text("edit.redo"), false);
        menu.addSeparator();
        menu.addItem(22, strings.text("edit.selectAll"));
    }
    else if (index == 2)
    {
        menu.addItem(30, strings.text("file.audio"));
        menu.addItem(31, strings.text("track.compose"));
    }
    else if (index == 3)
    {
        menu.addItem(40, strings.text("view.zoomIn"));
        menu.addItem(41, strings.text("view.zoomOut"));
        menu.addItem(42, strings.text("view.zoomFit"));
    }
    else
        menu.addItem(50, strings.text("help.about"));
    return menu;
}

void MainComponent::menuItemSelected(int id, int)
{
    if (id == 1) project.clear();
    else if (id == 2) openProject();
    else if (id == 3) saveProject();
    else if (id == 4 || id == 30) importAudio();
    else if (id == 5) importMelodyne();
    else if (id == 6) importMidi();
    else if (id == 7) juce::JUCEApplication::getInstance()->systemRequestedQuit();
    else if (id == 40) zoomSlider.setValue(zoomSlider.getValue() * 1.25);
    else if (id == 41) zoomSlider.setValue(zoomSlider.getValue() / 1.25);
    else if (id == 42)
    {
        const auto available = std::max(200, timelineViewport.getWidth());
        zoomSlider.setValue(static_cast<double>(available) / std::max(1.0, project.snapshot().durationSeconds()));
    }
    else if (id == 50)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            strings.text("app.title"), strings.text("help.aboutText"));
}

void MainComponent::openProject()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.open"), juce::File{}, "*.hspx");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& selected)
        {
            const auto file = selected.getResult();
            if (file == juce::File{}) return;
            juce::String error;
            if (!project.load(file, error)) showError(error);
        });
}

void MainComponent::saveProject()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.save"),
                                                   juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                       .getChildFile(project.snapshot().name + ".hspx"),
                                                   "*.hspx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& selected)
        {
            auto file = selected.getResult();
            if (file == juce::File{}) return;
            if (!file.hasFileExtension("hspx")) file = file.withFileExtension("hspx");
            juce::String error;
            if (!project.save(file, error)) showError(error);
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
                    project.addAudioFile(file, *duration, startSeconds);
                else
                    showError(strings.text("error.audio") + "\n" + file.getFullPathName());
        });
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
    juce::Thread::launch([safe, file]
    {
        juce::String error;
        auto imported = backend::MelodyneImporter::importProject(file, error,
            [safe](double value, const juce::String& stage)
            {
                juce::MessageManager::callAsync([safe, value, stage]
                {
                    if (safe == nullptr) return;
                    safe->progress = value;
                    safe->statusLabel.setText(safe->strings.text("status.loading") + "  "
                                                + safe->strings.text(juce::String("mpd.stage.") + stage),
                                              juce::dontSendNotification);
                });
            });
        juce::MessageManager::callAsync([safe, imported = std::move(imported), error]() mutable
        {
            if (safe == nullptr) return;
            safe->importInProgress = false;
            safe->progress = 0.0;
            if (!imported)
            {
                safe->showError(safe->strings.text("error.mpd") + "\n" + error);
                return;
            }
            safe->presentMelodyneComposeSelection(std::move(*imported));
        });
    });
}

void MainComponent::presentMelodyneComposeSelection(backend::MelodyneImportResult imported)
{
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
