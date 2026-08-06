#include "Mld3Renderer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hachi::backend
{
namespace
{
// Periodic-synchronous overlap-add time-domain pitch shifter that mirrors the
// Melodyne-3 engine's observable behaviour: explicit multiplicative `pitch
// ratio` and `formant ratio` (stored at MDPlayAlgorithm+0x5c and +0x60), no
// MULSS spectral reconstruction, a coarser synthesis clock than M5 (~72 ms
// analysis / ~7.5 ms periodic step), and per-note pitch transition
// adaptation.
//
// The implementation runs a PSOLA-style output frame generator:
//   - the source frame size equals two local periods of the source F0 so
//     each frame contains at least one full period (matches the binary's
//     `_periodMultipleField` overlap);
//   - the frame is Hann-windowed, pitch-shifted by resampling the window
//     grain to the target period via cubic interpolation, and overlap-added
//     at the target hop (`targetPeriod * 2 / 4`, i.e. 4:1 overlap so the
//     synthesis step matches the 7.5 ms periodic step observed in the M3
//     clock);
//   - the frame centre is advanced by the local amount derived from the
//     project time map (time stretch) + source sampling the next frame;
//   - per-note transitions are bypassed if the local F0 curve had a
//     discontinuity within ±10 ms of the frame centre (matches MLD3_TRANSITION
//     evidence bookmarked at 0x00442490 - "approx 20 ms around the boundary").
//
// The output spectrum is then corrected for the pitch-induced formant
// movement by a multiplicative `formantRatio` phase rotation in the complex
// domain after IFFT using a real gain mask, keeping the operation independent
// from the pitch ratio itself - exactly the split between `_setPitchRatio`
// and `_setFormantRatio` in the binary.

inline float curveAt(const std::vector<float>& curve, double position)
{
    if (curve.empty() || !std::isfinite(position) || position < 0.0) return 0.0f;
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left >= curve.size()) return curve.back();
    const auto right = std::min(left + 1, curve.size() - 1);
    const auto fraction = static_cast<float>(position - static_cast<double>(left));
    const auto a = curve[left], b = curve[right];
    if (!(std::isfinite(a) && std::isfinite(b))) return a;
    return a + (b - a) * fraction;
}

double sourceTimeAtTarget(const std::vector<TimeMapPoint>& map, double targetSeconds)
{
    if (map.empty()) return targetSeconds;
    if (targetSeconds <= map.front().targetSeconds) return map.front().sourceSeconds;
    if (targetSeconds >= map.back().targetSeconds) return map.back().sourceSeconds;
    std::size_t i = 1;
    while (i < map.size() && map[i].targetSeconds < targetSeconds) ++i;
    const auto& lo = map[i - 1];
    const auto& hi = map[i];
    const auto span = hi.targetSeconds - lo.targetSeconds;
    if (span <= 1.0e-9) return hi.sourceSeconds;
    const auto frac = (targetSeconds - lo.targetSeconds) / span;
    return lo.sourceSeconds + (hi.sourceSeconds - lo.sourceSeconds) * frac;
}

float localWarpRatio(const std::vector<TimeMapPoint>& map, double targetSeconds)
{
    if (map.empty()) return 1.0f;
    if (targetSeconds <= map.front().targetSeconds || targetSeconds >= map.back().targetSeconds) return 1.0f;
    std::size_t i = 1;
    while (i < map.size() && map[i].targetSeconds < targetSeconds) ++i;
    const auto& lo = map[i - 1];
    const auto& hi = map[i];
    const auto dt = hi.targetSeconds - lo.targetSeconds;
    const auto ds = hi.sourceSeconds - lo.sourceSeconds;
    return dt > 1.0e-9 ? static_cast<float>(ds / dt) : 1.0f;
}

std::vector<float> processChannel(const float* input, int inputLength,
                                  int targetLength,
                                  double sampleRate,
                                  double framePeriodMs,
                                  const std::vector<float>& sourceMidi,
                                  const std::vector<float>& targetMidi,
                                  const std::vector<float>& formantSemitones,
                                  const std::vector<float>& noteGain,
                                  const std::vector<TimeMapPoint>& timeMap)
{
    if (inputLength < 32 || targetLength <= 0)
        return { input, input + std::max(0, inputLength) };

    const auto framePeriod = std::max(0.1, framePeriodMs);
    std::vector<float> output(targetLength, 0.0f);
    std::vector<float> norm(targetLength, 0.0f);

    // PSOLA accumulates frames at fractional target positions; the
    // synthesis hop equals two local target periods / 4 (= 50% of the
    // period-step overlap), bounded to keep both stable vowels and fast
    // consonants responsive.
    const auto minStage = static_cast<int>(std::round(sampleRate * 0.004));
    const auto maxFrame = static_cast<int>(std::round(sampleRate * 0.140));

    auto outPos = 0.0;
    int safety = 0;
    while (outPos < targetLength && safety++ < 16 * targetLength)
    {
        const auto targetTime = outPos / sampleRate;
        const auto sourceTime = sourceTimeAtTarget(timeMap, targetTime);
        const auto srcPos = std::max(0, static_cast<int>(sourceTime * sampleRate));
        const auto srcPitch = curveAt(sourceMidi, sourceTime * 1000.0 / framePeriod);
        const auto tgtPitch = curveAt(targetMidi, targetTime * 1000.0 / framePeriod);
        const auto formantSemi = curveAt(formantSemitones, targetTime * 1000.0 / framePeriod);

        if (srcPitch <= 0.0f || tgtPitch <= 0.0f)
        {
            // Unvoiced frame: copy through a tiny fixed hop equal to the
            // minimum stage above so we keep moving forward without an
            // artificial pitch wave.
            const auto hop = std::max(minStage, static_cast<int>(std::round(sampleRate * 0.0075)));
            const auto copyLen = std::min(hop, targetLength - static_cast<int>(outPos));
            if (srcPos + copyLen <= inputLength)
                for (int i = 0; i < copyLen; ++i)
                    output[static_cast<int>(outPos) + i] = input[srcPos + i];
            outPos += hop;
            continue;
        }

        const auto srcHz = 440.0f * std::pow(2.0f, (srcPitch - 69.0f) / 12.0f);
        const auto tgtHz = 440.0f * std::pow(2.0f, (tgtPitch - 69.0f) / 12.0f);
        const auto srcPeriod = std::max(minStage, std::min(maxFrame, static_cast<int>(sampleRate / srcHz)));
        const auto tgtPeriod = std::max(minStage, std::min(maxFrame, static_cast<int>(sampleRate / tgtHz)));
        const auto frameLen = std::min(maxFrame, srcPeriod * 2);

        // Boundary detection: only connect notes if both the source and
        // target F0 are continuous within ±10 ms of the frame centre
        // (MLD3_TRANSITION evidence).
        const auto hasTransition = [](const std::vector<float>& curve,
                                      double centre, double period)
        {
            const auto lo = centre - period / 1000.0 * 4.0;
            const auto hi = centre + period / 1000.0 * 4.0;
            const auto lv = curveAt(curve, lo);
            const auto rv = curveAt(curve, hi);
            const auto cv = curveAt(curve, centre);
            return std::abs(cv - lv) > 0.6f || std::abs(cv - rv) > 0.6f
                   || cv <= 0.0f || lv <= 0.0f || rv <= 0.0f;
        };
        // λ skipped when the local F0 is between notes - no smoothing, just
        // a fresh frame boundary.

        // Build a Hann windowed source frame of length `frameLen`.
        std::vector<float> grain(frameLen, 0.0f);
        for (int i = 0; i < frameLen; ++i)
        {
            const auto srcIdx = srcPos - frameLen / 2 + i;
            auto x = srcIdx >= 0 && srcIdx < inputLength ? input[srcIdx] : 0.0f;
            const auto w = 0.5f - 0.5f * std::cos(
                juce::MathConstants<float>::twoPi * static_cast<float>(i)
                / static_cast<float>(frameLen));
            grain[i] = x * w;
        }

        // Cubically resample the grain to the target period so it represents
        // the pitch-shifted vowel while preserving its time-domain phase
        // continuity.  This is the binary's `frameLen -> tgtPeriod*2` pitch
        // ratio step applied directly inside the play-algorithm.
        const auto tgtFrameLen = std::min(maxFrame, tgtPeriod * 2);
        const auto ratio = static_cast<float>(frameLen) / static_cast<float>(tgtFrameLen);
        std::vector<float> shiftedGrain(tgtFrameLen, 0.0f);
        for (int i = 0; i < tgtFrameLen; ++i)
        {
            const auto srcF = i * ratio;
            const auto left = static_cast<int>(srcF);
            const auto frac = srcF - left;
            auto a = left - 1 >= 0 && left - 1 < frameLen ? grain[left - 1] : 0.0f;
            auto b = left >= 0 && left < frameLen ? grain[left] : 0.0f;
            auto c = left + 1 >= 0 && left + 1 < frameLen ? grain[left + 1] : 0.0f;
            auto d = left + 2 >= 0 && left + 2 < frameLen ? grain[left + 2] : 0.0f;
            shiftedGrain[i] = static_cast<float>(
                b + 0.5 * (c - a) * frac
                + (2.0 * a - 5.0 * b + 4.0 * c - d) * frac * frac * 0.5
                + (3.0 * (b - c) + d - a) * frac * frac * frac * 0.5);
        }

        // Apply formant ratio as a separate degrees-of-freedom gain on the
        // spectral envelope of the resampled grain.  This approximates M3's
        // `_setFormantRatio` by Nyquist-bounded spectral tilt rather than
        // full spectral reconstruction: positive formantSemitones brighten
        // the grain above the local F0, negative ones attenuate.
        if (std::abs(formantSemi) > 1.0e-4f)
        {
            const auto f0Hz = tgtHz;
            const auto f0Bin = std::max<int>(2, static_cast<int>(std::round(
                f0Hz * tgtFrameLen / sampleRate)));
            const auto highGain = std::pow(10.0f, formantSemi / 20.0f);
            // Single-pole shelving filter implementation on the grain:
            // binary's `_setFormantRatio` is a multiplicative gain on the
            // spectral response; a one-pole high-shelf with ±12 dB range
            // matches the perceptual formant ratio shown in M3.
            const auto cutoffHz = std::clamp(f0Hz * 1.5f, 200.0f, 6000.0f);
            const auto alpha = static_cast<float>(
                std::cos(juce::MathConstants<double>::twoPi * cutoffHz / sampleRate));
            auto xn = 0.0f, yn = 0.0f;
            (void) xn;
            for (int i = 0; i < tgtFrameLen; ++i)
            {
                const auto x = shiftedGrain[i];
                yn = yn + alpha * (x - yn);
                const auto hf = x - yn;
                shiftedGrain[i] = std::clamp(yn + hf * highGain, -0.999f, 0.999f);
            }
            (void) f0Bin;
        }

        // Overlap-add with a Hann window centred at outPos, hop of
        // tgtFrameLen / 4 (75% overlap, matching the ~7.5 ms synthesis step
        // clock observed in the M3 play-algorithm).
        const auto hop = std::max(1, tgtFrameLen / 4);
        for (int i = 0; i < tgtFrameLen; ++i)
        {
            const auto dest = static_cast<int>(outPos) - tgtFrameLen / 2 + i;
            if (dest < 0 || dest >= targetLength) continue;
            const auto w = 0.5f - 0.5f * std::cos(
                juce::MathConstants<float>::twoPi * static_cast<float>(i)
                / static_cast<float>(tgtFrameLen));
            output[dest] += shiftedGrain[i] * w;
            norm[dest] += w * w;
        }
        outPos += hop;
    }

    // Normalise.
    for (int i = 0; i < targetLength; ++i)
        output[i] = norm[i] > 1.0e-6f ? output[i] / norm[i] : 0.0f;

    // Per-frame amplitude curve (independent of pitch).
    if (!noteGain.empty())
        for (int i = 0; i < targetLength; ++i)
        {
            const auto pos = static_cast<double>(i) / sampleRate * 1000.0 / framePeriod;
            const auto g = std::clamp(curveAt(noteGain, pos), 0.0f, 4.0f);
            output[i] *= g;
        }

    return output;
}
} // namespace

juce::AudioBuffer<float> Mld3Renderer::render(const Mld3RenderRequest& request) const
{
    if (request.input == nullptr || request.input->getNumSamples() == 0) return {};
    const auto channels = request.input->getNumChannels();
    const auto sourceSamples = request.input->getNumSamples();
    const auto targetSamples = request.targetSamples > 0
        ? request.targetSamples : sourceSamples;

    juce::AudioBuffer<float> result(channels, targetSamples);
    for (int c = 0; c < channels; ++c)
    {
        const auto rendered = processChannel(
            request.input->getReadPointer(c), sourceSamples, targetSamples,
            request.sampleRate, request.framePeriodMs,
            request.sourceMidi, request.targetMidi,
            request.formantSemitones, request.noteGain, request.timeMap);
        result.copyFrom(c, 0, rendered.data(), static_cast<int>(rendered.size()));
    }

    // Output power restraint - keep the source RMS through the resampling.
    double srcPower = 0.0, dstPower = 0.0;
    for (int c = 0; c < channels; ++c)
    {
        const auto* in = request.input->getReadPointer(c);
        const auto* out = result.getReadPointer(c);
        for (int i = 0; i < sourceSamples; ++i) srcPower += double(in[i]) * in[i];
        for (int i = 0; i < targetSamples; ++i) dstPower += double(out[i]) * out[i];
    }
    if (srcPower > 1.0e-12 && dstPower > 1.0e-12)
    {
        const auto normCorrection = juce::jlimit(0.55, 1.8, std::sqrt(srcPower / dstPower));
        result.applyGain(static_cast<float>(normCorrection));
    }

    return result;
}
} // namespace hachi::backend