#include "MainComponent.h"
#include "backend/McpServer.h"
#include "backend/NativeAnalyzer.h"
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
        if (!arguments.isEmpty() && arguments[0] == "--inspect-settings")
        {
            I18n diagnosticStrings;
            juce::AudioDeviceManager diagnosticDevices;
            juce::PropertiesFile::Options options;
            options.applicationName = "HachiShifterSettingsSmoke";
            options.filenameSuffix = "settings";
            options.folderName = "HachiShifterNextSmoke";
            options.storageFormat = juce::PropertiesFile::storeAsXML;
            juce::PropertiesFile diagnosticProperties(options);
            SettingsComponent settings(diagnosticStrings, diagnosticDevices,
                                       diagnosticProperties, [] {});
            settings.setBounds(0, 0, 720, 570);
            settings.resized();
            const auto tabs = settings.diagnosticTabCount();
            const auto pageChildren = settings.diagnosticCurrentPageChildCount();
            std::cout << "tabs=" << tabs << '\n'
                      << "page_children=" << pageChildren << '\n'
                      << "width=" << settings.getWidth() << '\n'
                      << "height=" << settings.getHeight() << std::endl;
            if (tabs != 5 || pageChildren <= 0) setApplicationReturnValue(5);
            juce::MessageManager::callAsync([this] { quit(); });
            return;
        }
        if (arguments.size() >= 3 && arguments[0] == "--smoke-export")
        {
            cliAudioEngine = std::make_unique<AudioEngine>();
            const auto input = juce::File(arguments[1]);
            const auto duration = cliAudioEngine->probeDuration(input);
            if (!duration)
            {
                std::cerr << "error=audio_read" << std::endl;
                setApplicationReturnValue(2);
            }
            else
            {
                ProjectModel model;
                (void) model.addAudioFile(input, *duration);
                auto data = model.snapshot();
                if (!data.tracks.empty()) data.tracks.front().compose = false;
                cliAudioEngine->syncProject(data);
                juce::String error;
                if (cliAudioEngine->exportWav(juce::File(arguments[2]), error))
                    std::cout << "duration=" << *duration << '\n'
                              << "output=" << arguments[2] << std::endl;
                else
                {
                    std::cerr << "error=" << error << std::endl;
                    setApplicationReturnValue(3);
                }
            }
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
            const auto formant = arguments.size() >= 6 ? arguments[5].getFloatValue() : 0.0f;
            const auto gain = arguments.size() >= 7 ? arguments[6].getFloatValue() : 1.0f;
            const auto breath = arguments.size() >= 8 ? arguments[7].getFloatValue() : 0.0f;
            request.formantSemitones.assign(static_cast<std::size_t>(frames), formant);
            request.noteGain.assign(static_cast<std::size_t>(frames), gain);
            request.breath.assign(static_cast<std::size_t>(frames), breath);
            if (arguments.size() >= 9)
            {
                const auto backendName = arguments[8].toLowerCase();
                request.pitchBackend = backendName.contains("nsf")
                    ? backend::PitchRenderBackend::nsfHifigan
                    : backendName == "world" ? backend::PitchRenderBackend::world
                    : backendName.contains("vslib") ? backend::PitchRenderBackend::vslib
                    : backend::PitchRenderBackend::mld5;
            }
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
                              << "backend=" << result.backend << '\n'
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
        if (arguments.size() >= 2 && arguments[0] == "--inspect-audio")
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const auto file = juce::File(arguments[1]);
            auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
            if (reader == nullptr || reader->sampleRate <= 0.0)
            {
                std::cerr << "error=audio_read" << std::endl;
                setApplicationReturnValue(2);
            }
            else
            {
                ProjectModel model;
                const auto clipId = model.addAudioFile(file,
                    static_cast<double>(reader->lengthInSamples) / reader->sampleRate);
                juce::String analysisError;
                auto analysed = backend::NativeAnalyzer::analyse(file, analysisError);
                (void) model.setClipNotesIfEmpty(clipId, std::move(analysed));
                const auto data = model.snapshot();
                std::size_t notes = 0;
                for (const auto& track : data.tracks)
                    for (const auto& clip : track.clips) notes += clip.notes.size();
                std::cout << "tracks=" << data.tracks.size() << '\n'
                          << "notes=" << notes << '\n'
                          << "analysis=" << (analysisError.isEmpty() ? "native" : "skipped")
                          << std::endl;
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
        cliAudioEngine.reset();
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
    std::unique_ptr<AudioEngine> cliAudioEngine;
};
}

START_JUCE_APPLICATION(hachi::HachiShifterApplication)
