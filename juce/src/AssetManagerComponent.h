#pragma once

#include "I18n.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

namespace hachi
{
class AssetManagerComponent final : public juce::Component,
                                    public juce::FileDragAndDropTarget
{
public:
    AssetManagerComponent(I18n& stringsToUse, juce::PropertiesFile& propertiesToUse);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void load();
    void save();
    void addFiles(const juce::StringArray& paths);
    int rowAt(int y) const;

    I18n& strings;
    juce::PropertiesFile& properties;
    juce::TextButton registerButton, removeButton;
    juce::StringArray assets;
    int selected = -1;
    int dragStartRow = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    static constexpr int toolbarHeight = 38;
    static constexpr int rowHeight = 42;
};
}
