#pragma once

#include "I18n.h"
#include "Theme.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include <vector>

namespace hachi
{
class SettingsComponent final : public juce::Component
{
public:
    using Applied = std::function<void()>;

    SettingsComponent(I18n& stringsToUse, juce::AudioDeviceManager& devicesToUse,
                      juce::PropertiesFile& propertiesToUse, Applied appliedToUse);
    ~SettingsComponent() override;
    void resized() override;

private:
    class FormPage final : public juce::Component
    {
    public:
        void addRow(juce::Label& label, juce::Component& editor);
        void addWide(juce::Component& component, int height = 28);
        void resized() override;
    private:
        struct Row { juce::Label* label = nullptr; juce::Component* editor = nullptr; int height = 28; };
        std::vector<Row> rows;
    };

    static juce::Colour colourFromText(const juce::String& text, juce::Colour fallback);
    void loadValues();
    void saveValues();
    void setTexts();
    void refreshAudioValues();
    void applyAudioValues();
    void openAdvancedAudioPanel();

    I18n& strings;
    juce::AudioDeviceManager& devices;
    juce::PropertiesFile& properties;
    Applied onApplied;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    FormPage interfacePage;
    FormPage algorithmPage;
    FormPage operationPage;
    FormPage importPage;
    FormPage audioPage;

    juce::Label languageLabel, themeLabel, accentLabel, accentLightLabel, noteColourLabel;
    juce::ComboBox language, theme;
    juce::TextEditor accent, accentLight, noteColour;

    juce::Label audioDeviceLabel, sampleRateLabel, bufferSizeLabel, currentAudioDevice;
    juce::ComboBox sampleRate, bufferSize;
    juce::TextButton advancedAudio;

    juce::Label gamePathLabel, gameModelLabel, hifiganPathLabel, inferenceLabel,
                inferenceDeviceLabel, utauResamplerLabel;
    juce::TextEditor gamePath, hifiganPath, utauResamplerPath;
    juce::ComboBox gameModel, inference, inferenceDevice;

    juce::Label shortcutLabel, wheelLabel;
    juce::ComboBox shortcutPreset, wheelAction;
    juce::ToggleButton spacePlayback, confirmDestructive;

    juce::Label melodyneComposeLabel, melodynePitchLabel, importedAlgorithmLabel;
    juce::ComboBox melodyneCompose, melodynePitchSource, importedAlgorithm;
    juce::ToggleButton preserveProjectEdits, locateMediaRecursively;

    juce::TextButton applyButton;
};
}
