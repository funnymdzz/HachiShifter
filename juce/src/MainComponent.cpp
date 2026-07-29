#include "MainComponent.h"

namespace hachi
{
MainComponent::MainComponent()
    : progressBar(progress), trackList(project, strings), timeline(project), pianoRoll(project)
{
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);

    for (auto* button : { &openButton, &saveButton, &audioButton, &melodyneButton,
                          &playButton, &stopButton, &noteEditButton, &wrenchButton })
        addAndMakeVisible(*button);
    for (auto* component : { static_cast<juce::Component*>(&pitchAlgorithm),
                             static_cast<juce::Component*>(&stretchAlgorithm),
                             static_cast<juce::Component*>(&pitchLabel),
                             static_cast<juce::Component*>(&stretchLabel),
                             static_cast<juce::Component*>(&statusLabel),
                             static_cast<juce::Component*>(&sourceEditHint),
                             static_cast<juce::Component*>(&zoomSlider),
                             static_cast<juce::Component*>(&progressBar),
                             static_cast<juce::Component*>(&trackList),
                             static_cast<juce::Component*>(&timelineViewport),
                             static_cast<juce::Component*>(&pianoViewport) })
        addAndMakeVisible(*component);

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

    openButton.onClick = [this] { openProject(); };
    saveButton.onClick = [this] { saveProject(); };
    audioButton.onClick = [this] { importAudio(); };
    melodyneButton.onClick = [this] { importMelodyne(); };
    playButton.onClick = [this] { audio.play(); };
    stopButton.onClick = [this]
    {
        audio.stop();
        audio.setPosition(0.0);
    };
    noteEditButton.onClick = [this] { setSourceEditMode(false); };
    wrenchButton.onClick = [this] { setSourceEditMode(true); };
    noteEditButton.setClickingTogglesState(false);
    wrenchButton.setClickingTogglesState(false);

    project.addChangeListener(this);
    audio.addChangeListener(this);
    refreshTexts();
    setSourceEditMode(false);
    setSize(1280, 760);
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    audio.removeChangeListener(this);
    project.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void MainComponent::refreshTexts()
{
    openButton.setButtonText(strings.text("file.open"));
    saveButton.setButtonText(strings.text("file.save"));
    audioButton.setButtonText(strings.text("file.audio"));
    melodyneButton.setButtonText(strings.text("file.melodyne"));
    playButton.setButtonText(strings.text("transport.play"));
    stopButton.setButtonText(strings.text("transport.stop"));
    noteEditButton.setButtonText(strings.text("tool.main"));
    wrenchButton.setButtonText(strings.text("tool.wrench"));
    pitchLabel.setText(strings.text("algo.pitch"), juce::dontSendNotification);
    stretchLabel.setText(strings.text("algo.stretch"), juce::dontSendNotification);
    statusLabel.setText(strings.text("status.ready"), juce::dontSendNotification);
    sourceEditHint.setText(strings.text("edit.source"), juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);
    g.setColour(Palette::panel);
    g.fillRect(0, 0, getWidth(), 80);
    g.setColour(Palette::grid);
    g.drawHorizontalLine(25, 0.0f, static_cast<float>(getWidth()));
    g.drawHorizontalLine(79, 0.0f, static_cast<float>(getWidth()));
    g.setColour(Palette::text);
    g.setFont(12.0f);
    g.drawText("文件     编辑     轨道     视图     帮助", 10, 0, 310, 25,
               juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(26);
    auto toolbar = area.removeFromTop(54).reduced(8, 7);
    auto take = [&toolbar](juce::Component& component, int width)
    {
        component.setBounds(toolbar.removeFromLeft(width));
        toolbar.removeFromLeft(5);
    };
    take(openButton, 100);
    take(saveButton, 100);
    take(audioButton, 100);
    take(melodyneButton, 120);
    toolbar.removeFromLeft(12);
    take(playButton, 70);
    take(stopButton, 70);
    toolbar.removeFromLeft(12);
    take(noteEditButton, 90);
    take(wrenchButton, 90);
    zoomSlider.setBounds(toolbar.removeFromRight(130));
    toolbar.removeFromRight(8);
    progressBar.setBounds(toolbar.removeFromRight(150));

    auto footer = area.removeFromBottom(24);
    statusLabel.setBounds(footer.reduced(8, 0));
    auto upper = area.removeFromTop(juce::jmax(190, area.getHeight() / 2));
    trackList.setBounds(upper.removeFromLeft(226));
    timelineViewport.setBounds(upper);

    auto parameterHeader = area.removeFromTop(34);
    pitchLabel.setBounds(parameterHeader.removeFromLeft(86).reduced(5, 3));
    pitchAlgorithm.setBounds(parameterHeader.removeFromLeft(138).reduced(3, 3));
    stretchLabel.setBounds(parameterHeader.removeFromLeft(86).reduced(5, 3));
    stretchAlgorithm.setBounds(parameterHeader.removeFromLeft(160).reduced(3, 3));
    sourceEditHint.setBounds(parameterHeader.removeFromLeft(370).reduced(6, 3));
    pianoViewport.setBounds(area);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &project)
        audio.syncProject(project.snapshot());
}

void MainComponent::timerCallback()
{
    pianoRoll.setPlayheadSeconds(audio.position());
    timeline.setPlayheadSeconds(audio.position());
    statusLabel.setText((audio.isPlaying() ? strings.text("transport.play") : strings.text("status.ready"))
                            + "  " + juce::String(audio.position(), 2) + " s",
                        juce::dontSendNotification);
}

void MainComponent::setSourceEditMode(bool enabled)
{
    pianoRoll.setSourceEditMode(enabled);
    noteEditButton.setToggleState(!enabled, juce::dontSendNotification);
    wrenchButton.setToggleState(enabled, juce::dontSendNotification);
    sourceEditHint.setVisible(enabled);
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
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& selected)
        {
            const auto file = selected.getResult();
            if (file == juce::File{}) return;
            if (const auto duration = audio.probeDuration(file))
                project.addAudioFile(file, *duration);
            else
                showError(strings.text("error.audio"));
        });
}

void MainComponent::importMelodyne()
{
    chooser = std::make_unique<juce::FileChooser>(strings.text("file.melodyne"), juce::File{}, "*.mpd");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& selected)
        {
            if (selected.getResult() != juce::File{})
                showError(strings.text("error.mpd"));
        });
}

void MainComponent::showError(const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                            strings.text("app.title"), message);
}
}
