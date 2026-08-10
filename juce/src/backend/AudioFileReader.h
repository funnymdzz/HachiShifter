#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace hachi::backend
{
[[nodiscard]] std::unique_ptr<juce::AudioFormatReader> createAudioReader(
    juce::AudioFormatManager& formats, const juce::File& file);
}
