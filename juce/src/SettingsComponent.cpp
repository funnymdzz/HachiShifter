#include "SettingsComponent.h"
#include <cmath>

namespace hachi
{
void SettingsComponent::FormPage::addRow(juce::Label& label, juce::Component& editor)
{
    addAndMakeVisible(label);
    addAndMakeVisible(editor);
    rows.push_back({ &label, &editor, 30 });
}

void SettingsComponent::FormPage::addWide(juce::Component& component, int height)
{
    addAndMakeVisible(component);
    rows.push_back({ nullptr, &component, height });
}

void SettingsComponent::FormPage::resized()
{
    auto area = getLocalBounds().reduced(18, 14);
    for (const auto& row : rows)
    {
        auto line = area.removeFromTop(row.height);
        area.removeFromTop(7);
        if (row.label != nullptr)
        {
            row.label->setBounds(line.removeFromLeft(210));
            line.removeFromLeft(10);
        }
        row.editor->setBounds(line);
    }
}

SettingsComponent::SettingsComponent(I18n& stringsToUse,
                                     juce::AudioDeviceManager& devicesToUse,
                                     juce::PropertiesFile& propertiesToUse,
                                     Applied appliedToUse)
    : strings(stringsToUse), devices(devicesToUse), properties(propertiesToUse),
      onApplied(std::move(appliedToUse))
{
    addAndMakeVisible(tabs);
    addAndMakeVisible(applyButton);

    language.addItem("zh-CN", 1);
    language.addItem("zh-TW", 2);
    language.addItem("ja-JP", 3);
    language.addItem("ko-KR", 4);
    language.addItem("en-US", 5);
    theme.addItem("Dark", 1);
    theme.addItem("Light", 2);
    interfacePage.addRow(languageLabel, language);
    interfacePage.addRow(themeLabel, theme);
    interfacePage.addRow(accentLabel, accent);
    interfacePage.addRow(accentLightLabel, accentLight);
    interfacePage.addRow(noteColourLabel, noteColour);

    currentAudioDevice.setColour(juce::Label::backgroundColourId, Palette::background);
    currentAudioDevice.setColour(juce::Label::outlineColourId, Palette::border);
    currentAudioDevice.setJustificationType(juce::Justification::centredLeft);
    sampleRate.setEditableText(true);
    bufferSize.setEditableText(true);
    for (const auto value : { 32000, 44100, 48000, 88200, 96000, 192000 })
        sampleRate.addItem(juce::String(value) + " Hz", sampleRate.getNumItems() + 1);
    for (const auto value : { 32, 64, 128, 256, 512, 1024, 2048, 4096 })
        bufferSize.addItem(juce::String(value), bufferSize.getNumItems() + 1);
    audioPage.addRow(audioDeviceLabel, currentAudioDevice);
    audioPage.addRow(sampleRateLabel, sampleRate);
    audioPage.addRow(bufferSizeLabel, bufferSize);
    audioPage.addWide(advancedAudio);
    advancedAudio.onClick = [this] { openAdvancedAudioPanel(); };

    gameModel.addItem("large", 1);
    gameModel.addItem("small", 2);
    inference.addItem("Auto", 1);
    inference.addItem("CPU", 2);
    inference.addItem("DirectML", 3);
    inference.addItem("CUDA", 4);
    inference.addItem("CoreML", 5);
    inferenceDevice.addItem("Auto", 1);
    inferenceDevice.addItem("GPU 0", 2);
    inferenceDevice.addItem("GPU 1", 3);
    algorithmPage.addRow(gamePathLabel, gamePath);
    algorithmPage.addRow(gameModelLabel, gameModel);
    algorithmPage.addRow(hifiganPathLabel, hifiganPath);
    algorithmPage.addRow(inferenceLabel, inference);
    algorithmPage.addRow(inferenceDeviceLabel, inferenceDevice);
    algorithmPage.addRow(utauResamplerLabel, utauResamplerPath);

    shortcutPreset.addItem("HachiShifter", 1);
    shortcutPreset.addItem("Melodyne", 2);
    shortcutPreset.addItem("UTAU", 3);
    wheelAction.addItem("Zoom", 1);
    wheelAction.addItem("Scroll", 2);
    operationPage.addRow(shortcutLabel, shortcutPreset);
    operationPage.addRow(wheelLabel, wheelAction);
    operationPage.addWide(spacePlayback);
    operationPage.addWide(confirmDestructive);

    melodyneCompose.addItem("Ask", 1);
    melodyneCompose.addItem("Melodic tracks", 2);
    melodyneCompose.addItem("All tracks", 3);
    melodyneCompose.addItem("Audio only", 4);
    melodynePitchSource.addItem("Project data", 1);
    melodynePitchSource.addItem("GAME + FCPE", 2);
    importedAlgorithm.addItem("mld5", 1);
    importedAlgorithm.addItem("nsf-hifigan", 2);
    importedAlgorithm.addItem("WORLD", 3);
    importedAlgorithm.addItem("vslib", 4);
    importPage.addRow(melodyneComposeLabel, melodyneCompose);
    importPage.addRow(melodynePitchLabel, melodynePitchSource);
    importPage.addRow(importedAlgorithmLabel, importedAlgorithm);
    importPage.addWide(preserveProjectEdits);
    importPage.addWide(locateMediaRecursively);

    tabs.addTab({}, Palette::panel, &interfacePage, false);
    tabs.addTab({}, Palette::panel, &audioPage, false);
    tabs.addTab({}, Palette::panel, &algorithmPage, false);
    tabs.addTab({}, Palette::panel, &operationPage, false);
    tabs.addTab({}, Palette::panel, &importPage, false);
    // Select a concrete page before the dialog is attached to a peer.  On
    // headless/device-delayed startup JUCE may otherwise keep index -1 until a
    // tab click, making the settings dialog appear completely empty.
    tabs.setCurrentTabIndex(0, false);
    loadValues();
    setTexts();
    applyButton.onClick = [this]
    {
        saveValues();
        setTexts();
        if (onApplied) onApplied();
    };
    setSize(720, 570);
}

SettingsComponent::~SettingsComponent()
{
    saveValues();
}

juce::Colour SettingsComponent::colourFromText(const juce::String& value,
                                               juce::Colour fallback)
{
    auto text = value.trim().removeCharacters("#");
    if (text.length() != 6 && text.length() != 8) return fallback;
    if (text.length() == 6) text = "ff" + text;
    return juce::Colour::fromString(text);
}

void SettingsComponent::loadValues()
{
    language.setSelectedId(properties.getIntValue("ui.language",
        static_cast<int>(strings.getLanguage()) + 1), juce::dontSendNotification);
    theme.setSelectedId(properties.getValue("ui.theme", "dark") == "light" ? 2 : 1,
                        juce::dontSendNotification);
    accent.setText(properties.getValue("ui.accent", "7F69CA"), false);
    accentLight.setText(properties.getValue("ui.accentLight", "CBCBFA"), false);
    noteColour.setText(properties.getValue("ui.noteColour", "F4C000"), false);
    gamePath.setText(properties.getValue("algorithm.gamePath"), false);
    gameModel.setSelectedId(properties.getValue("algorithm.gameModel", "large") == "small" ? 2 : 1,
                            juce::dontSendNotification);
    hifiganPath.setText(properties.getValue("algorithm.hifiganPath"), false);
    inference.setSelectedId(properties.getIntValue("algorithm.inference", 1), juce::dontSendNotification);
    inferenceDevice.setSelectedId(properties.getIntValue("algorithm.device", 1), juce::dontSendNotification);
    utauResamplerPath.setText(properties.getValue("algorithm.utauResampler"), false);
    shortcutPreset.setSelectedId(properties.getIntValue("operation.shortcutPreset", 1), juce::dontSendNotification);
    wheelAction.setSelectedId(properties.getIntValue("operation.wheelAction", 1), juce::dontSendNotification);
    spacePlayback.setToggleState(properties.getBoolValue("operation.spacePlayback", true), juce::dontSendNotification);
    confirmDestructive.setToggleState(properties.getBoolValue("operation.confirmDestructive", true), juce::dontSendNotification);
    melodyneCompose.setSelectedId(properties.getIntValue("import.melodyneCompose", 1), juce::dontSendNotification);
    melodynePitchSource.setSelectedId(properties.getIntValue("import.melodynePitchSource", 1), juce::dontSendNotification);
    importedAlgorithm.setSelectedId(properties.getIntValue("import.algorithm", 1), juce::dontSendNotification);
    preserveProjectEdits.setToggleState(properties.getBoolValue("import.preserveEdits", true), juce::dontSendNotification);
    locateMediaRecursively.setToggleState(properties.getBoolValue("import.recursiveMedia", true), juce::dontSendNotification);
    refreshAudioValues();
}

void SettingsComponent::saveValues()
{
    applyAudioValues();
    properties.setValue("ui.language", language.getSelectedId());
    properties.setValue("ui.theme", theme.getSelectedId() == 2 ? "light" : "dark");
    properties.setValue("ui.accent", accent.getText().trim());
    properties.setValue("ui.accentLight", accentLight.getText().trim());
    properties.setValue("ui.noteColour", noteColour.getText().trim());
    properties.setValue("algorithm.gamePath", gamePath.getText());
    properties.setValue("algorithm.gameModel", gameModel.getSelectedId() == 2 ? "small" : "large");
    properties.setValue("algorithm.hifiganPath", hifiganPath.getText());
    properties.setValue("algorithm.inference", inference.getSelectedId());
    properties.setValue("algorithm.device", inferenceDevice.getSelectedId());
    properties.setValue("algorithm.utauResampler", utauResamplerPath.getText());
    properties.setValue("operation.shortcutPreset", shortcutPreset.getSelectedId());
    properties.setValue("operation.wheelAction", wheelAction.getSelectedId());
    properties.setValue("operation.spacePlayback", spacePlayback.getToggleState());
    properties.setValue("operation.confirmDestructive", confirmDestructive.getToggleState());
    properties.setValue("import.melodyneCompose", melodyneCompose.getSelectedId());
    properties.setValue("import.melodynePitchSource", melodynePitchSource.getSelectedId());
    properties.setValue("import.algorithm", importedAlgorithm.getSelectedId());
    properties.setValue("import.preserveEdits", preserveProjectEdits.getToggleState());
    properties.setValue("import.recursiveMedia", locateMediaRecursively.getToggleState());
    properties.saveIfNeeded();
    strings.setLanguage(static_cast<I18n::Language>(juce::jlimit(1, 5, language.getSelectedId()) - 1));
    Palette::applyTheme(theme.getSelectedId() == 2 ? "light" : "dark",
        colourFromText(accent.getText(), juce::Colour(0xff7f69ca)),
        colourFromText(accentLight.getText(), juce::Colour(0xffcbcbfa)),
        colourFromText(noteColour.getText(), juce::Colour(0xfff4c000)));
}

void SettingsComponent::setTexts()
{
    tabs.setTabName(0, strings.text("settings.interface"));
    tabs.setTabName(1, strings.text("settings.audio"));
    tabs.setTabName(2, strings.text("settings.algorithm"));
    tabs.setTabName(3, strings.text("settings.operation"));
    tabs.setTabName(4, strings.text("settings.import"));
    languageLabel.setText(strings.text("settings.language"), juce::dontSendNotification);
    themeLabel.setText(strings.text("settings.theme"), juce::dontSendNotification);
    accentLabel.setText(strings.text("settings.accent"), juce::dontSendNotification);
    accentLightLabel.setText(strings.text("settings.accentLight"), juce::dontSendNotification);
    noteColourLabel.setText(strings.text("settings.noteColour"), juce::dontSendNotification);
    audioDeviceLabel.setText(strings.text("settings.audioDevice"), juce::dontSendNotification);
    sampleRateLabel.setText(strings.text("settings.sampleRate"), juce::dontSendNotification);
    bufferSizeLabel.setText(strings.text("settings.bufferSize"), juce::dontSendNotification);
    advancedAudio.setButtonText(strings.text("settings.advancedAudio"));
    gamePathLabel.setText(strings.text("settings.gamePath"), juce::dontSendNotification);
    gameModelLabel.setText(strings.text("settings.gameModel"), juce::dontSendNotification);
    hifiganPathLabel.setText(strings.text("settings.hifiganPath"), juce::dontSendNotification);
    inferenceLabel.setText(strings.text("settings.inference"), juce::dontSendNotification);
    inferenceDeviceLabel.setText(strings.text("settings.device"), juce::dontSendNotification);
    utauResamplerLabel.setText(strings.text("settings.utauResampler"), juce::dontSendNotification);
    shortcutLabel.setText(strings.text("settings.shortcuts"), juce::dontSendNotification);
    wheelLabel.setText(strings.text("settings.wheel"), juce::dontSendNotification);
    spacePlayback.setButtonText(strings.text("settings.spacePlayback"));
    confirmDestructive.setButtonText(strings.text("settings.confirmDestructive"));
    melodyneComposeLabel.setText(strings.text("settings.melodyneCompose"), juce::dontSendNotification);
    melodynePitchLabel.setText(strings.text("settings.melodynePitch"), juce::dontSendNotification);
    importedAlgorithmLabel.setText(strings.text("settings.importAlgorithm"), juce::dontSendNotification);
    preserveProjectEdits.setButtonText(strings.text("settings.preserveEdits"));
    locateMediaRecursively.setButtonText(strings.text("settings.recursiveMedia"));
    applyButton.setButtonText(strings.text("dialog.apply"));
}

void SettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto footer = area.removeFromBottom(40);
    applyButton.setBounds(footer.removeFromRight(120).reduced(4));
    tabs.setBounds(area);
}

void SettingsComponent::refreshAudioValues()
{
    if (auto* device = devices.getCurrentAudioDevice())
    {
        currentAudioDevice.setText(device->getName(), juce::dontSendNotification);
        sampleRate.setText(juce::String(static_cast<int>(std::llround(device->getCurrentSampleRate())))
                           + " Hz", juce::dontSendNotification);
        bufferSize.setText(juce::String(device->getCurrentBufferSizeSamples()),
                           juce::dontSendNotification);
    }
    else
    {
        currentAudioDevice.setText(strings.text("settings.noAudioDevice"),
                                   juce::dontSendNotification);
        sampleRate.setText("48000 Hz", juce::dontSendNotification);
        bufferSize.setText("512", juce::dontSendNotification);
    }
}

void SettingsComponent::applyAudioValues()
{
    if (devices.getCurrentAudioDevice() == nullptr) return;
    auto setup = devices.getAudioDeviceSetup();
    const auto requestedRate = sampleRate.getText().retainCharacters("0123456789.")
        .getDoubleValue();
    const auto requestedBuffer = bufferSize.getText().retainCharacters("0123456789")
        .getIntValue();
    auto changed = false;
    if (requestedRate > 0.0 && std::abs(setup.sampleRate - requestedRate) > 0.5)
    {
        setup.sampleRate = requestedRate;
        changed = true;
    }
    if (requestedBuffer > 0 && setup.bufferSize != requestedBuffer)
    {
        setup.bufferSize = requestedBuffer;
        changed = true;
    }
    if (changed)
    {
        const auto error = devices.setAudioDeviceSetup(setup, true);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                strings.text("settings.audio"), error);
    }
    refreshAudioValues();
}

void SettingsComponent::openAdvancedAudioPanel()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        devices, 0, 2, 1, 2, true, true, true, false);
    selector->setSize(680, 500);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = strings.text("settings.advancedAudio");
    options.dialogBackgroundColour = Palette::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.content.setOwned(selector);
    options.launchAsync();
}
}
