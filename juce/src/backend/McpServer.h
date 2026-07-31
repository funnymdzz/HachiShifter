#pragma once

#include "../AudioEngine.h"
#include "../ProjectModel.h"
#include "MelodyneImporter.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace hachi::backend
{
class McpServer final
{
public:
    McpServer();
    int run();

private:
    juce::var handle(const juce::var& request, bool& shouldRespond);
    juce::var callTool(const juce::String& name, const juce::var& arguments);
    juce::var projectJson() const;
    juce::int64 currentProjectFingerprint() const;
    void syncAudio();
    bool waitForRender(double timeoutSeconds, juce::String& error);
    juce::var transportStatusJson() const;
    static juce::var toolResult(const juce::String& text, bool isError = false);
    static juce::var errorResponse(const juce::var& id, int code, const juce::String& message);

    ProjectModel project;
    juce::AudioFormatManager formats;
    std::unique_ptr<AudioEngine> audio;
    juce::int64 preparedFingerprint = 0;
    bool audioPrepared = false;
};
}
