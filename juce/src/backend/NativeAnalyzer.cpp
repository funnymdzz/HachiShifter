#include "NativeAnalyzer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <numeric>
#include <optional>

namespace hachi::backend
{
namespace
{
constexpr double analysisRate = 8'000.0;
constexpr int hopSamples = 80;       // 10 ms
constexpr int windowSamples = 320;   // 40 ms
constexpr int fftOrder = 10;
constexpr int fftSize = 1 << fftOrder;

struct Frame
{
    float rms = 0.0f;
    float highRatio = 0.0f;
    float pitchMidi = 0.0f;
    float confidence = 0.0f;
    bool voiced = false;
};

std::vector<float> readMono8k(const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "Audio analysis could not read " + file.getFullPathName();
        return {};
    }
    const auto sourceRate = reader->sampleRate;
    const auto sourceLength = reader->lengthInSamples;
    const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    auto source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    juce::ResamplingAudioSource resampler(source.get(), false, channels);
    resampler.setResamplingRatio(sourceRate / analysisRate);
    resampler.prepareToPlay(4096, analysisRate);
    const auto duration = static_cast<double>(sourceLength) / sourceRate;
    const auto total = std::max(1, static_cast<int>(std::ceil(duration * analysisRate)));
    std::vector<float> mono(static_cast<std::size_t>(total));
    juce::AudioBuffer<float> block(channels, 4096);
    for (int offset = 0; offset < total; offset += block.getNumSamples())
    {
        const auto count = std::min(block.getNumSamples(), total - offset);
        block.clear();
        juce::AudioSourceChannelInfo info(&block, 0, count);
        resampler.getNextAudioBlock(info);
        for (int sample = 0; sample < count; ++sample)
        {
            auto value = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                value += block.getSample(channel, sample);
            mono[static_cast<std::size_t>(offset + sample)] = value / static_cast<float>(channels);
        }
    }
    resampler.releaseResources();
    return mono;
}

float percentile(std::vector<float> values, double phase)
{
    if (values.empty()) return 0.0f;
    const auto index = std::min(values.size() - 1,
        static_cast<std::size_t>(std::floor(juce::jlimit(0.0, 1.0, phase)
                                            * static_cast<double>(values.size() - 1))));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

std::pair<float, float> estimatePitch(const std::vector<float>& mono, int start,
                                      juce::dsp::FFT& fft,
                                      std::vector<std::complex<float>>& input,
                                      std::vector<std::complex<float>>& spectrum)
{
    std::fill(input.begin(), input.end(), std::complex<float>{});
    auto mean = 0.0f;
    for (int index = 0; index < windowSamples; ++index)
        mean += mono[static_cast<std::size_t>(start + index)];
    mean /= static_cast<float>(windowSamples);
    std::vector<double> energy(static_cast<std::size_t>(windowSamples + 1));
    for (int index = 0; index < windowSamples; ++index)
    {
        const auto window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
            * static_cast<float>(index) / static_cast<float>(windowSamples - 1));
        const auto value = (mono[static_cast<std::size_t>(start + index)] - mean) * window;
        input[static_cast<std::size_t>(index)] = { value, 0.0f };
        energy[static_cast<std::size_t>(index + 1)] = energy[static_cast<std::size_t>(index)]
            + static_cast<double>(value) * value;
    }
    if (energy.back() < 1.0e-8) return {};
    fft.perform(input.data(), spectrum.data(), false);
    for (auto& value : spectrum) value = { std::norm(value), 0.0f };
    fft.perform(spectrum.data(), input.data(), true);
    const auto minLag = static_cast<int>(analysisRate / 800.0);
    const auto maxLag = std::min(windowSamples / 2,
        static_cast<int>(std::ceil(analysisRate / 55.0)));
    auto best = -1.0f;
    auto bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const auto leftEnergy = energy[static_cast<std::size_t>(windowSamples - lag)];
        const auto rightEnergy = energy.back() - energy[static_cast<std::size_t>(lag)];
        const auto denominator = std::sqrt(std::max(1.0e-18, leftEnergy * rightEnergy));
        const auto correlation = static_cast<float>(input[static_cast<std::size_t>(lag)].real()
                                                     / denominator);
        if (correlation > best)
        {
            best = correlation;
            bestLag = lag;
        }
    }
    if (bestLag <= 0 || best < 0.40f) return { 0.0f, std::max(0.0f, best) };
    auto refined = static_cast<double>(bestLag);
    if (bestLag > minLag && bestLag < maxLag)
    {
        const auto previous = input[static_cast<std::size_t>(bestLag - 1)].real();
        const auto centre = input[static_cast<std::size_t>(bestLag)].real();
        const auto next = input[static_cast<std::size_t>(bestLag + 1)].real();
        const auto denominator = previous - 2.0 * centre + next;
        if (std::abs(denominator) > 1.0e-12)
            refined += juce::jlimit(-0.5, 0.5, 0.5 * (previous - next) / denominator);
    }
    const auto hz = analysisRate / std::max(1.0, refined);
    return { static_cast<float>(69.0 + 12.0 * std::log2(hz / 440.0)), best };
}

float median(std::vector<float> values)
{
    if (values.empty()) return 60.0f;
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

std::optional<std::pair<float, float>> absolutePitchAt(
    const std::vector<NoteData>& notes, double sourceSeconds)
{
    for (const auto& note : notes)
    {
        const auto local = sourceSeconds - note.startSeconds;
        if (local < -1.0e-6 || local > note.durationSeconds + 1.0e-6
            || note.contour.empty())
            continue;
        const auto right = std::lower_bound(note.contour.begin(), note.contour.end(), local,
            [](const PitchPoint& point, double time) { return point.timeSeconds < time; });
        const auto rightIndex = right == note.contour.end()
            ? note.contour.size() - 1
            : static_cast<std::size_t>(std::distance(note.contour.begin(), right));
        const auto leftIndex = rightIndex > 0 && note.contour[rightIndex].timeSeconds > local
            ? rightIndex - 1 : rightIndex;
        const auto& left = note.contour[leftIndex];
        const auto& next = note.contour[rightIndex];
        if (!left.voiced || !next.voiced) return std::nullopt;
        const auto amount = next.timeSeconds > left.timeSeconds
            ? static_cast<float>(juce::jlimit(0.0, 1.0,
                (local - left.timeSeconds) / (next.timeSeconds - left.timeSeconds)))
            : 0.0f;
        const auto interpolate = [amount](float first, float second)
        {
            return first + (second - first) * amount;
        };
        const auto centre = note.sourceMidiCenter * 100.0f;
        return std::pair {
            centre + interpolate(left.relativeCents, next.relativeCents),
            centre + interpolate(left.withoutVibratoCents, next.withoutVibratoCents)
        };
    }
    return std::nullopt;
}
}

std::vector<NoteData> NativeAnalyzer::analyse(const juce::File& file, juce::String& error,
                                               Progress progress)
{
    auto mono = readMono8k(file, error);
    if (mono.size() < static_cast<std::size_t>(windowSamples)) return {};
    const auto frameCount = 1 + static_cast<int>((mono.size() - windowSamples) / hopSamples);
    std::vector<Frame> frames(static_cast<std::size_t>(frameCount));
    std::vector<float> rmsValues(static_cast<std::size_t>(frameCount));
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto start = frame * hopSamples;
        double square = 0.0;
        double difference = 0.0;
        for (int index = 0; index < windowSamples; ++index)
        {
            const auto value = mono[static_cast<std::size_t>(start + index)];
            square += static_cast<double>(value) * value;
            if (index > 0)
            {
                const auto previous = mono[static_cast<std::size_t>(start + index - 1)];
                const auto delta = value - previous;
                difference += static_cast<double>(delta) * delta;
            }
        }
        auto& feature = frames[static_cast<std::size_t>(frame)];
        feature.rms = static_cast<float>(std::sqrt(square / windowSamples));
        feature.highRatio = static_cast<float>(difference / std::max(1.0e-12, square * 4.0));
        rmsValues[static_cast<std::size_t>(frame)] = feature.rms;
    }
    const auto peak = *std::max_element(rmsValues.begin(), rmsValues.end());
    const auto floor = percentile(rmsValues, 0.20);
    const auto silence = std::max({ 0.0007f, floor * 2.5f, peak * 0.012f });
    juce::dsp::FFT fft(fftOrder);
    std::vector<std::complex<float>> input(static_cast<std::size_t>(fftSize));
    std::vector<std::complex<float>> spectrum(static_cast<std::size_t>(fftSize));
    for (int frame = 0; frame < frameCount; ++frame)
    {
        auto& feature = frames[static_cast<std::size_t>(frame)];
        if (feature.rms >= silence * 1.15f)
        {
            const auto [midi, confidence] = estimatePitch(mono, frame * hopSamples,
                                                          fft, input, spectrum);
            feature.pitchMidi = midi;
            feature.confidence = confidence;
            feature.voiced = midi > 0.0f && confidence >= 0.48f;
        }
        if (progress && frame % 64 == 0)
            progress(static_cast<double>(frame) / std::max(1, frameCount));
    }

    // Build model-free fallback note regions.  Short unvoiced gaps stay with
    // the same syllable; a sustained silence or stable pitch jump starts a new
    // object.  GAME remains the preferred semantic segmenter when its model is
    // present, while this path keeps a model-free installation editable.
    std::vector<std::pair<int, int>> regions;
    auto begin = -1;
    auto lastVoiced = -1;
    auto referenceMidi = 0.0f;
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto& feature = frames[static_cast<std::size_t>(frame)];
        if (feature.voiced)
        {
            const auto gap = lastVoiced >= 0 ? frame - lastVoiced : 0;
            const auto jump = referenceMidi > 0.0f
                ? std::abs(feature.pitchMidi - referenceMidi) : 0.0f;
            if (begin >= 0 && (gap > 8 || (jump > 2.5f && gap <= 2)))
            {
                regions.push_back({ begin, std::max(begin + 1, lastVoiced + 1) });
                begin = -1;
            }
            if (begin < 0)
            {
                begin = frame;
                referenceMidi = feature.pitchMidi;
            }
            else referenceMidi = referenceMidi * 0.90f + feature.pitchMidi * 0.10f;
            lastVoiced = frame;
        }
        else if (begin >= 0 && lastVoiced >= 0 && frame - lastVoiced > 8)
        {
            regions.push_back({ begin, lastVoiced + 1 });
            begin = -1;
            lastVoiced = -1;
            referenceMidi = 0.0f;
        }
    }
    if (begin >= 0 && lastVoiced >= begin) regions.push_back({ begin, lastVoiced + 1 });

    std::vector<NoteData> notes;
    for (std::size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex)
    {
        auto [voicedBegin, voicedEnd] = regions[regionIndex];
        auto startFrame = voicedBegin;
        const auto lower = regionIndex > 0 ? regions[regionIndex - 1].second : 0;
        while (startFrame > lower && voicedBegin - startFrame < 20
               && frames[static_cast<std::size_t>(startFrame - 1)].rms >= silence)
            --startFrame;
        auto endFrame = voicedEnd;
        while (endFrame < frameCount && endFrame - voicedEnd < 8
               && frames[static_cast<std::size_t>(endFrame)].rms >= silence)
            ++endFrame;
        if (endFrame - startFrame < 2) continue;
        std::vector<float> pitches;
        for (int frame = voicedBegin; frame < voicedEnd; ++frame)
            if (frames[static_cast<std::size_t>(frame)].voiced)
                pitches.push_back(frames[static_cast<std::size_t>(frame)].pitchMidi);
        if (pitches.empty()) continue;
        NoteData note;
        note.id = "note_native_" + juce::Uuid().toString().removeCharacters("-");
        note.startSeconds = static_cast<double>(startFrame * hopSamples) / analysisRate;
        note.durationSeconds = static_cast<double>((endFrame - startFrame) * hopSamples)
            / analysisRate;
        note.consonantSeconds = static_cast<double>((voicedBegin - startFrame) * hopSamples)
            / analysisRate;
        note.sourceMidiCenter = median(pitches);
        note.midiNote = note.sourceMidiCenter;
        for (int frame = startFrame; frame < endFrame; ++frame)
        {
            const auto& current = frames[static_cast<std::size_t>(frame)];
            PitchPoint point;
            point.timeSeconds = static_cast<double>((frame - startFrame) * hopSamples)
                / analysisRate;
            point.voiced = current.voiced;
            if (current.voiced)
            {
                point.relativeCents = (current.pitchMidi - note.sourceMidiCenter) * 100.0f;
                auto slowSum = 0.0f;
                auto slowCount = 0;
                for (int neighbour = std::max(voicedBegin, frame - 5);
                     neighbour <= std::min(voicedEnd - 1, frame + 5); ++neighbour)
                    if (frames[static_cast<std::size_t>(neighbour)].voiced)
                    {
                        slowSum += frames[static_cast<std::size_t>(neighbour)].pitchMidi;
                        ++slowCount;
                    }
                point.withoutVibratoCents = slowCount > 0
                    ? (slowSum / slowCount - note.sourceMidiCenter) * 100.0f
                    : point.relativeCents;
            }
            if (!current.voiced && current.highRatio > 0.12f && current.rms > silence)
                note.sibilantMarkers.push_back(point.timeSeconds);
            note.contour.push_back(point);
        }
        notes.push_back(std::move(note));
    }
    if (progress) progress(1.0);
    return notes;
}

bool NativeAnalyzer::reanalyseProjectSourcePitch(ProjectData& project, juce::String& error,
                                                  Progress progress)
{
    std::map<juce::String, juce::File> files;
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
            if (clip.sourceFile.existsAsFile())
                files.try_emplace(clip.sourceFile.getFullPathName(), clip.sourceFile);
    if (files.empty())
    {
        error = "Source-pitch reanalysis has no readable media";
        return false;
    }

    std::map<juce::String, std::vector<NoteData>> analyses;
    juce::StringArray failures;
    auto fileIndex = std::size_t(0);
    for (const auto& [path, file] : files)
    {
        juce::String localError;
        auto notes = analyse(file, localError, [&](double value)
        {
            if (progress)
                progress((static_cast<double>(fileIndex) + value)
                         / static_cast<double>(files.size()));
        });
        if (!notes.empty()) analyses.emplace(path, std::move(notes));
        else failures.add(file.getFileName() + ": " + localError);
        ++fileIndex;
    }

    auto updatedNotes = std::size_t(0);
    for (auto& track : project.tracks)
        for (auto& clip : track.clips)
        {
            const auto found = analyses.find(clip.sourceFile.getFullPathName());
            if (found == analyses.end()) continue;
            const auto sourceDuration = clip.sourceDurationSeconds > 1.0e-9
                ? clip.sourceDurationSeconds : clip.durationSeconds;
            for (auto& note : clip.notes)
            {
                if (note.contour.empty())
                {
                    for (double local = 0.0; local < note.durationSeconds; local += 0.01)
                    {
                        PitchPoint point;
                        point.timeSeconds = local;
                        note.contour.push_back(point);
                    }
                    PitchPoint end;
                    end.timeSeconds = note.durationSeconds;
                    note.contour.push_back(end);
                }
                std::vector<std::optional<std::pair<float, float>>> samples;
                samples.reserve(note.contour.size());
                std::vector<float> voicedPitch;
                for (const auto& point : note.contour)
                {
                    const auto clipLocal = note.startSeconds + point.timeSeconds;
                    const auto sourceSeconds = clip.sourceOffsetSeconds
                        + juce::jlimit(0.0, 1.0, clipLocal / std::max(0.001, clip.durationSeconds))
                            * sourceDuration;
                    auto pitch = absolutePitchAt(found->second, sourceSeconds);
                    if (pitch) voicedPitch.push_back(pitch->first);
                    samples.push_back(pitch);
                }
                if (voicedPitch.size() < 2) continue;
                const auto centreCents = median(std::move(voicedPitch));
                note.sourceMidiCenter = juce::jlimit(0.0f, 127.0f, centreCents / 100.0f);
                for (std::size_t index = 0; index < note.contour.size(); ++index)
                {
                    auto& point = note.contour[index];
                    const auto& pitch = samples[index];
                    point.voiced = pitch.has_value();
                    if (!pitch) continue;
                    point.relativeCents = pitch->first - centreCents;
                    point.withoutVibratoCents = pitch->second - centreCents;
                }
                ++updatedNotes;
            }
        }
    if (progress) progress(1.0);
    if (!failures.isEmpty()) error = failures.joinIntoString("\n");
    if (updatedNotes == 0)
    {
        if (error.isEmpty()) error = "Source-pitch reanalysis produced no aligned contours";
        return false;
    }
    return true;
}
}
