#pragma once

#include "../ProjectModel.h"
#include <functional>
#include <optional>

namespace hachi::backend
{
struct MelodyneImportResult
{
    ProjectData project;
    juce::StringArray missingFiles;
    juce::StringArray referencedFiles;
};

struct MelodyneImportOptions
{
    bool recursiveMediaSearch = true;
    bool preserveProjectEdits = true;
};

class MelodyneImporter final
{
public:
    using Progress = std::function<void(double, const juce::String&)>;

    [[nodiscard]] static std::optional<MelodyneImportResult>
        importProject(const juce::File& file, juce::String& error, Progress progress = {},
                      MelodyneImportOptions options = {});
};
}
