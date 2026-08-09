#pragma once

#include "ProjectModel.h"
#include "backend/RenderService.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include <optional>
#include <unordered_map>

namespace hachi
{
class AudioEngine final : public juce::AudioSource, public juce::ChangeBroadcaster
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    void syncProject(const ProjectData& project);
    void setHifiganModelDirectory(const juce::File& directory);
    void setInferenceConfiguration(backend::InferenceBackend inference, int deviceIndex);
    [[nodiscard]] std::optional<double> probeDuration(const juce::File& file);
    bool setAuditionFile(const juce::File& file);
    void clearAuditionFile();

    void play();
    void stop();
    void setPosition(double seconds);
    [[nodiscard]] double position() const;
    [[nodiscard]] float trackPeak(const juce::String& trackId) const;
    [[nodiscard]] std::optional<double> renderProgress() const;
    [[nodiscard]] juce::String activeRenderBackends() const;
    bool exportWav(const juce::File& file, juce::String& error);
    [[nodiscard]] bool isPlaying() const { return playing.load(); }
    [[nodiscard]] juce::AudioDeviceManager& devices() { return deviceManager; }
    bool ensureOutputDevice(juce::String& error);
    void restoreDeviceState(juce::PropertiesFile& properties);
    void saveDeviceState(juce::PropertiesFile& properties) const;

private:
    struct RenderedClip
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
        juce::String backend;
        std::atomic<bool> scheduled { false };
        std::atomic<bool> ready { false };
        std::atomic<bool> finished { false };
        // For a stretch-splice-then-pitch phrase, this entry holds the full
        // merged buffer; each target is a per-element slice that is filled when
        // the merged render completes (or immediately if it is already ready).
        struct SliceTarget
        {
            std::shared_ptr<RenderedClip> clip;
            int startSample = 0;
            int sampleCount = 0;
        };
        std::vector<SliceTarget> pendingSlices;
        juce::CriticalSection sliceLock;
    };

    struct LoadedClip
    {
        ClipData clip;
        std::string trackId;
        bool smoothOverlaps = true;
        float trackGain = 1.0f;
        float trackPan = 0.0f;
        std::shared_ptr<std::atomic<float>> meter;
        std::shared_ptr<RenderedClip> rendered;
        std::shared_ptr<juce::AudioFormatReader> reader;
        juce::AudioBuffer<float> scratch;
    };

    static float fadeEnvelope(const ClipData& clip, double localSeconds);
    void rebuildLoadedClips(const ProjectData& project);

    juce::AudioFormatManager formatManager;
    backend::RenderService renderService;
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    mutable juce::ReadWriteLock renderLock;
    std::vector<std::unique_ptr<LoadedClip>> loadedClips;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<float>>> trackMeters;
    std::unordered_map<std::string, std::shared_ptr<RenderedClip>> renderCache;
    juce::File hifiganModelDirectory;
    backend::OrtExecutionConfig inferenceConfiguration;
    std::shared_ptr<juce::AudioFormatReader> auditionReader;
    juce::AudioBuffer<float> auditionScratch;
    std::atomic<bool> auditionMode { false };
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> timelineSample { 0 };
    std::atomic<double> outputSampleRate { 48'000.0 };
    std::atomic<double> projectDurationSeconds { 0.0 };
    std::atomic<float> masterLimiterGain { 1.0f };
    std::atomic<bool> offlineRendering { false };
};
}
