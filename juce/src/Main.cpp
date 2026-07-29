#include "MainComponent.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

namespace hachi
{
class HachiShifterApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "HachiShifter Next"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        auto arguments = juce::StringArray::fromTokens(commandLine, true);
        if (arguments.size() >= 2 && arguments[0] == "--inspect-midi")
        {
            ProjectModel model;
            juce::String error;
            if (!model.addMidiFile(juce::File(arguments[1]), error))
            {
                std::cerr << "error=" << error << std::endl;
                setApplicationReturnValue(2);
            }
            else
            {
                const auto project = model.snapshot();
                std::size_t notes = 0;
                for (const auto& track : project.tracks)
                    for (const auto& clip : track.clips) notes += clip.notes.size();
                std::cout << "project=" << project.name << '\n'
                          << "bpm=" << project.bpm << '\n'
                          << "tracks=" << project.tracks.size() << '\n'
                          << "notes=" << notes << std::endl;
            }
            juce::MessageManager::callAsync([this] { quit(); });
            return;
        }
        if (arguments.size() >= 2 && arguments[0] == "--inspect-mpd")
        {
            juce::String error;
            const auto imported = backend::MelodyneImporter::importProject(juce::File(arguments[1]), error);
            if (!imported)
            {
                std::cerr << "error=" << error << std::endl;
                setApplicationReturnValue(2);
            }
            else
            {
                std::size_t clips = 0;
                std::size_t notes = 0;
                for (const auto& track : imported->project.tracks)
                {
                    clips += track.clips.size();
                    for (const auto& clip : track.clips) notes += clip.notes.size();
                }
                std::cout << "project=" << imported->project.name << '\n'
                          << "bpm=" << imported->project.bpm << '\n'
                          << "beat_origin=" << imported->project.beatOriginSeconds << '\n'
                          << "tracks=" << imported->project.tracks.size() << '\n'
                          << "clips=" << clips << '\n'
                          << "notes=" << notes << '\n'
                          << "missing=" << imported->missingFiles.size() << std::endl;
            }
            juce::MessageManager::callAsync([this] { quit(); });
            return;
        }
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
        if (!arguments.isEmpty())
            mainWindow->openFile(juce::File(arguments[0]));
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, Palette::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, false);
            setResizeLimits(900, 560, 8192, 8192);
            centreWithSize(1280, 760);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        void openFile(const juce::File& file)
        {
            if (auto* component = dynamic_cast<MainComponent*>(getContentComponent()))
                component->openExternalFile(file);
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION(hachi::HachiShifterApplication)
