#include "AudioFileReader.h"

namespace hachi::backend
{
namespace
{
juce::StringArray ffmpegExecutables()
{
    juce::StringArray result;
    const auto configured = juce::SystemStats::getEnvironmentVariable(
        "HACHISHIFTER_FFMPEG", {});
    if (configured.isNotEmpty()) result.add(configured);
    const auto executableDirectory = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();
#if JUCE_WINDOWS
    const auto executableName = juce::String("ffmpeg.exe");
    constexpr auto pathSeparator = ";";
#else
    const auto executableName = juce::String("ffmpeg");
    constexpr auto pathSeparator = ":";
#endif
    const auto bundled = executableDirectory.getChildFile(executableName);
    if (bundled.existsAsFile()) result.add(bundled.getFullPathName());
    juce::StringArray paths;
    paths.addTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}), pathSeparator, {});
    for (const auto& path : paths)
    {
        const auto candidate = juce::File(path.unquoted()).getChildFile(executableName);
        if (candidate.existsAsFile()) result.add(candidate.getFullPathName());
    }
    result.removeDuplicates(false);
    return result;
}

juce::File transcodedAudioFile(const juce::File& source)
{
    const auto identity = source.getFullPathName() + "#" + juce::String(source.getSize())
        + "#" + juce::String(source.getLastModificationTime().toMilliseconds());
    const auto cache = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("HachiShifterNext-media-cache");
    if (cache.createDirectory().failed()) return {};
    const auto output = cache.getChildFile(
        "v2-" + juce::String::toHexString(identity.hashCode64()) + ".wav");
    if (output.existsAsFile() && output.getSize() > 44) return output;

    const auto temporary = output.getSiblingFile(output.getFileNameWithoutExtension()
        + "." + juce::Uuid().toString() + ".part.wav");
    for (const auto& executable : ffmpegExecutables())
    {
        juce::StringArray arguments {
            executable, "-v", "error", "-nostdin", "-y", "-i",
            source.getFullPathName(), "-map", "0:a:0", "-vn", "-c:a",
            "pcm_s16le", temporary.getFullPathName()
        };
        juce::ChildProcess process;
        if (!process.start(arguments, 0)) continue;
        if (!process.waitForProcessToFinish(120'000))
        {
            process.kill();
            temporary.deleteFile();
            continue;
        }
        if (process.getExitCode() == 0 && temporary.existsAsFile() && temporary.getSize() > 44)
        {
            if (output.existsAsFile() || temporary.moveFileTo(output)) return output;
        }
        temporary.deleteFile();
    }
    return {};
}
}

std::unique_ptr<juce::AudioFormatReader> createAudioReader(
    juce::AudioFormatManager& formats, const juce::File& file)
{
    if (!file.existsAsFile()) return {};
    if (auto* reader = formats.createReaderFor(file))
        return std::unique_ptr<juce::AudioFormatReader>(reader);
    if (auto stream = file.createInputStream())
        if (auto* reader = formats.createReaderFor(std::move(stream)))
            return std::unique_ptr<juce::AudioFormatReader>(reader);
    auto transcoded = transcodedAudioFile(file);
    if (!transcoded.existsAsFile()) return {};
    if (auto* reader = formats.createReaderFor(transcoded))
        return std::unique_ptr<juce::AudioFormatReader>(reader);
    if (!transcoded.deleteFile()) return {};
    transcoded = transcodedAudioFile(file);
    return std::unique_ptr<juce::AudioFormatReader>(
        transcoded.existsAsFile() ? formats.createReaderFor(transcoded) : nullptr);
}
}
