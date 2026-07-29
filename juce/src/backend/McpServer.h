#pragma once

#include "../ProjectModel.h"
#include "MelodyneImporter.h"
#include <juce_audio_formats/juce_audio_formats.h>

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
    static juce::var toolResult(const juce::String& text, bool isError = false);
    static juce::var errorResponse(const juce::var& id, int code, const juce::String& message);

    ProjectModel project;
    juce::AudioFormatManager formats;
};
}
