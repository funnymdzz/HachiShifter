#include "MainComponent.h"
#include "backend/McpServer.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
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
        if (!arguments.isEmpty() && arguments[0] == "--mcp")
        {
            backend::McpServer server;
            setApplicationReturnValue(server.run());
            juce::MessageManager::callAsync([this] { quit(); });
            return;
        }
        if (arguments.size() >= 4 && arguments[0] == "--smoke-mld5")
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            auto reader = std::unique_ptr<juce::AudioFormatReader>(
                formats.createReaderFor(juce::File(arguments[1])));
            if (reader == nullptr)
            {
                std::cerr << "error=audio_read" << std::endl;
                setApplicationReturnValue(2);
                juce::MessageManager::callAsync([this] { quit(); });
                return;
            }
            const auto sourceDuration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            const auto shift = arguments[2].getFloatValue();
            const auto stretch = juce::jlimit(0.25, 4.0, arguments[3].getDoubleValue());
            backend::Mld5FileRenderRequest request;
            request.sourceFile = juce::File(arguments[1]);
            request.sourceDurationSeconds = sourceDuration;
            request.targetDurationSeconds = sourceDuration * stretch;
            const auto frames = std::max(2, static_cast<int>(std::ceil(
                request.targetDurationSeconds / 0.005)) + 1);
            request.sourceMidi.assign(static_cast<std::size_t>(frames), 60.0f);
            request.targetMidi.assign(static_cast<std::size_t>(frames), 60.0f + shift);
            const auto outputFile = arguments.size() >= 5 ? juce::File(arguments[4]) : juce::File();
            cliRenderService = std::make_unique<backend::RenderService>();
            cliRenderService->renderMld5File(std::move(request), [this, outputFile](backend::RenderedAudio result)
            {
                if (result.buffer.getNumSamples() <= 0)
                {
                    std::cerr << "error=render_empty" << std::endl;
                    setApplicationReturnValue(3);
                }
                else
                {
                    double squareSum = 0.0;
                    for (int channel = 0; channel < result.buffer.getNumChannels(); ++channel)
                        for (int sample = 0; sample < result.buffer.getNumSamples(); ++sample)
                        {
                            const auto value = result.buffer.getSample(channel, sample);
                            squareSum += static_cast<double>(value) * value;
                        }
                    const auto count = std::max(1, result.buffer.getNumChannels()
                                                  * result.buffer.getNumSamples());
                    std::cout << "sample_rate=" << result.sampleRate << '\n'
                              << "channels=" << result.buffer.getNumChannels() << '\n'
                              << "samples=" << result.buffer.getNumSamples() << '\n'
                              << "rms=" << std::sqrt(squareSum / static_cast<double>(count)) << std::endl;
                    if (outputFile != juce::File())
                    {
                        outputFile.deleteFile();
                        auto stream = outputFile.createOutputStream();
                        juce::WavAudioFormat wav;
                        auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(
                            stream.release(), result.sampleRate,
                            static_cast<unsigned int>(result.buffer.getNumChannels()), 24, {}, 0));
                        if (writer == nullptr
                            || !writer->writeFromAudioSampleBuffer(result.buffer, 0,
                                                                   result.buffer.getNumSamples()))
                        {
                            std::cerr << "error=audio_write" << std::endl;
                            setApplicationReturnValue(4);
                        }
                        else std::cout << "output=" << outputFile.getFullPathName() << std::endl;
                    }
                }
                quit();
            });
            return;
        }
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
        cliRenderService.reset();
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
    std::unique_ptr<backend::RenderService> cliRenderService;
};
}

START_JUCE_APPLICATION(hachi::HachiShifterApplication)
