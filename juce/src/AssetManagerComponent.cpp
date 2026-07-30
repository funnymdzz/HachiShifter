#include "AssetManagerComponent.h"
#include "Theme.h"
#include <algorithm>

namespace hachi
{
AssetManagerComponent::AssetManagerComponent(I18n& stringsToUse,
                                             juce::PropertiesFile& propertiesToUse)
    : strings(stringsToUse), properties(propertiesToUse)
{
    addAndMakeVisible(registerButton);
    addAndMakeVisible(utauButton);
    addAndMakeVisible(removeButton);
    registerButton.setButtonText(strings.text("asset.register"));
    utauButton.setButtonText(strings.text("asset.utau"));
    removeButton.setButtonText(strings.text("asset.remove"));
    registerButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>(strings.text("asset.register"),
            juce::File{}, "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& selectedFiles)
            {
                juce::StringArray paths;
                for (const auto& file : selectedFiles.getResults()) paths.add(file.getFullPathName());
                addFiles(paths);
            });
    };
    utauButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>(strings.text("asset.utau"), juce::File{});
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& selectedDirectory)
            {
                const auto directory = selectedDirectory.getResult();
                if (!directory.isDirectory()) return;
                juce::StringArray imported, warnings;
                int sidecars = 0;
                int regions = 0;
                if (!SampleSettings::importVoicebank(directory, imported, sidecars,
                                                      regions, warnings))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        strings.text("asset.utau"), warnings.joinIntoString("\n"));
                    return;
                }
                addFiles(imported);
                auto message = strings.text("asset.utauDone")
                    .replace("{files}", juce::String(imported.size()))
                    .replace("{sidecars}", juce::String(sidecars))
                    .replace("{regions}", juce::String(regions));
                if (!warnings.isEmpty()) message += "\n\n" + warnings.joinIntoString("\n");
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    strings.text("asset.utau"), message);
            });
    };
    removeButton.onClick = [this]
    {
        if (selected < 0 || selected >= assets.size()) return;
        assets.remove(selected);
        selected = std::min(selected, assets.size() - 1);
        save();
        repaint();
    };
    load();
    setSize(480, 520);
}

void AssetManagerComponent::load()
{
    assets = juce::StringArray::fromLines(properties.getValue("assets.files"));
    assets.removeEmptyStrings();
    assets.removeDuplicates(false);
}

void AssetManagerComponent::save()
{
    properties.setValue("assets.files", assets.joinIntoString("\n"));
    properties.saveIfNeeded();
}

void AssetManagerComponent::addFiles(const juce::StringArray& paths)
{
    for (const auto& path : paths)
    {
        const juce::File file(path);
        if (file.existsAsFile() && file.hasFileExtension("wav;flac;aif;aiff;mp3;ogg"))
            assets.addIfNotAlreadyThere(file.getFullPathName(), false);
    }
    save();
    repaint();
}

bool AssetManagerComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const auto& path)
    {
        return juce::File(path).hasFileExtension("wav;flac;aif;aiff;mp3;ogg");
    });
}

void AssetManagerComponent::filesDropped(const juce::StringArray& files, int, int)
{
    addFiles(files);
}

int AssetManagerComponent::rowAt(int y) const
{
    const auto row = (y - toolbarHeight) / rowHeight;
    return y >= toolbarHeight && row >= 0 && row < assets.size() ? row : -1;
}

void AssetManagerComponent::mouseDown(const juce::MouseEvent& event)
{
    selected = rowAt(event.y);
    dragStartRow = selected;
    repaint();
}

void AssetManagerComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragStartRow < 0 || dragStartRow >= assets.size()
        || event.getDistanceFromDragStart() < 6) return;
    const juce::StringArray files { assets[dragStartRow] };
    dragStartRow = -1;
    juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
}

void AssetManagerComponent::paint(juce::Graphics& g)
{
    g.fillAll(Palette::panel);
    g.setColour(Palette::border);
    g.drawHorizontalLine(toolbarHeight - 1, 0.0f, static_cast<float>(getWidth()));
    if (assets.isEmpty())
    {
        g.setColour(Palette::textMuted);
        g.drawFittedText(strings.text("asset.empty"),
            getLocalBounds().withTrimmedTop(toolbarHeight).reduced(30),
            juce::Justification::centred, 3);
        return;
    }
    for (int index = 0; index < assets.size(); ++index)
    {
        auto row = juce::Rectangle<int>(0, toolbarHeight + index * rowHeight,
                                        getWidth(), rowHeight);
        g.setColour(index == selected ? Palette::accent.withAlpha(0.2f)
                                      : index % 2 == 0 ? Palette::panel : Palette::panelRaised);
        g.fillRect(row);
        const juce::File file(assets[index]);
        g.setColour(Palette::noteFill);
        g.fillRoundedRectangle(row.removeFromLeft(9).reduced(3).toFloat(), 2.0f);
        g.setColour(Palette::text);
        g.drawText(file.getFileName(), row.reduced(9, 2).removeFromTop(20),
                   juce::Justification::centredLeft, true);
        g.setColour(Palette::textMuted);
        g.setFont(10.0f);
        g.drawText(file.getParentDirectory().getFullPathName(), row.reduced(9, 2),
                   juce::Justification::centredLeft, true);
    }
}

void AssetManagerComponent::resized()
{
    auto toolbar = getLocalBounds().removeFromTop(toolbarHeight).reduced(6, 5);
    registerButton.setBounds(toolbar.removeFromLeft(150));
    toolbar.removeFromLeft(6);
    utauButton.setBounds(toolbar.removeFromLeft(150));
    toolbar.removeFromLeft(6);
    removeButton.setBounds(toolbar.removeFromLeft(110));
}
}
