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

    static bool runtimeAvailable();
    static std::vector<NoteData> analyse(const juce::File& audioFile,
                                         const juce::File& modelDirectory,
                                         const Options& options,
                                         juce::String& error,
                                         NativeAnalyzer::Progress progress = {});
};
}
