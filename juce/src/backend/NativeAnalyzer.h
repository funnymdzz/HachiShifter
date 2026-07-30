#pragma once

#include "../ProjectModel.h"
#include <functional>

namespace hachi::backend
{
class NativeAnalyzer final
{
public:
    using Progress = std::function<void(double)>;
    static std::vector<NoteData> analyse(const juce::File& file, juce::String& error,
                                         Progress progress = {});
};
}
