#pragma once

#include "NativeAnalyzer.h"

namespace hachi::backend
{
class GameAnalyzer final
{
public:
    struct Options
    {
        bool performanceMode = false;
        int intraOpThreads = 0;
    };

    struct Result
    {
        std::vector<NoteData> notes;
        bool fcpeUsed = false;
        juce::String warning;
    };

    static bool runtimeAvailable();
    static Result analyse(const juce::File& audioFile,
                          const juce::File& modelDirectory,
                          const juce::File& fcpeModelFile,
                          const Options& options,
                          juce::String& error,
                          NativeAnalyzer::Progress progress = {});
};
}
