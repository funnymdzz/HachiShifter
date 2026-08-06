#include "Mld5Renderer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace hachi::backend
{
namespace
{
// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

// Half-cosine smoothstep over [0,1]; endpoint x=0 -> 0, x=1 -> 1.  This is
// the shape of the runtime bandlimited extraction kernel `m5_kernelWindow01`
// (0x18298fda0) used by `MULSS_reconstructSpectralComponents`.
inline float kernel01(float x) noexcept
{
    x = std::clamp(x, 0.0f, 1.0f);
    return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * x);
}

inline float curveAt(const std::vector<float>& curve, double position) noexcept
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

// Monotone seeded time-map lookup.
double sourceTimeAtTarget(const std::vector<TimeMapPoint>& map, double targetSeconds) noexcept
{
    if (map.empty()) return targetSeconds;
    if (targetSeconds <= map.front().targetSeconds) return map.front().sourceSeconds;
    if (targetSeconds >= map.back().targetSeconds) return map.back().sourceSeconds;
    std::size_t i = 1;
    while (i < map.size() && map[i].targetSeconds < targetSeconds) ++i;
    const auto& lo = map[i - 1], hi = map[i];
    const auto span = hi.targetSeconds - lo.targetSeconds;
    if (span <= 1.0e-9) return hi.sourceSeconds;
    const auto frac = (targetSeconds - lo.targetSeconds) / span;
    return lo.sourceSeconds + (hi.sourceSeconds - lo.sourceSeconds) * frac;
}

float localWarpRatio(const std::vector<TimeMapPoint>& map, double targetSeconds) noexcept
{
    if (map.empty()) return 1.0f;
    if (targetSeconds <= map.front().targetSeconds || targetSeconds >= map.back().targetSeconds)
        return 1.0f;
    std::size_t i = 1;
    while (i < map.size() && map[i].targetSeconds < targetSeconds) ++i;
    const auto& lo = map[i - 1], hi = map[i];
    const auto dt = hi.targetSeconds - lo.targetSeconds;
    const auto ds = hi.sourceSeconds - lo.sourceSeconds;
    return dt > 1.0e-9 ? static_cast<float>(ds / dt) : 1.0f;
}

// Clamp a possibly-fractional bin index into [0, half].
inline int clampBin(float bin, int half) noexcept
{
    return std::max(0, std::min(half, static_cast<int>(std::round(bin))));
}

// ---------------------------------------------------------------------------
//  MULSS-style single-channel shifter.
//
//  Faithful reconstruction of the Melodyne 5 component renderer:
//    1. windowed STFT (Hann, 4096-bin frame matching the 0x1000 cap);
//    2. compute a wide log-domain spectral envelope (~520 Hz radius) — the
//       vocal-tract response;
//    3. flatten: divide source magnitude by the envelope to leave only the
//       harmonic fine structure;
//    4. shift the fine structure to the target bins via the bandlimited
//       smoothstep kernel (`m5_kernelWindow01`), sampling one source bin
//       per target harmonic moved by `pitchRatio = targetHz/sourceHz`;
//    5. re-apply the envelope sampled at the *source* frequency (formant
//       preservation) and optionally scaled by the user formant factor;
//    6. build a per-bin real gain mask `reconstructed/source` and multiply
//       the *source complex spectrum* by it — this preserves transients /
//       natural phase instead of synthesising a phase accumulator (the
//       core reason the MULSS path does not phase-vocoder-double the voice);
//    7. IFFT and overlap-add at a target hop that follows the local time
//       warp so the output takes the requested duration.
//
//  Every bin index access is bounds-checked, which is what the previous
//  implementation got wrong (a high-harmonic envelope lookup ran past
//  `half` and crashed the GUI import path).
// ---------------------------------------------------------------------------
std::vector<float> mulssProcessChannel(const float* input, int inputLength,
                                       int targetLength, double sampleRate,
                                       double framePeriodMs,
                                       const std::vector<float>& sourceMidi,
                                       const std::vector<float>& targetMidi,
                                       const std::vector<float>& formantSemitones,
                                       const std::vector<TimeMapPoint>& timeMap)
{
    if (inputLength < 32 || targetLength <= 0)
        return { input, input + std::max(0, inputLength) };

    constexpr auto fftOrder = 12;          // 4096
    constexpr auto fftSize = 1 << fftOrder;
    constexpr auto half = fftSize / 2;
    const auto analysisHop = fftSize / 4;  // 75% overlap

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize));
    // Normalisation for 75% OLA so the result is unity-gain.
    const auto windowGain = 1.5f;

    const auto pad = fftSize / 2;
    std::vector<std::complex<float>> timeFrame(fftSize);
    std::vector<std::complex<float>> spectrum(fftSize);
    std::vector<std::complex<float>> shiftedFrame(fftSize);
    std::vector<std::complex<float>> inverse(fftSize);
    std::vector<float> sourceMag(half + 1);
    std::vector<float> logPrefix(half + 2);
    std::vector<float> envelope(half + 1);
    std::vector<float> flatMag(half + 1);
    std::vector<float> shiftedMag(half + 1);
    std::vector<float> output(targetLength + fftSize, 0.0f);
    std::vector<float> norm(targetLength + fftSize, 0.0f);

    const auto framePeriod = std::max(0.1, framePeriodMs);
    const auto wideRadius = std::max(2, static_cast<int>(std::round(
        520.0 * fftSize / sampleRate)));    // vocal-tract envelope radius

    auto outPos = 0;
    // Safety cap so a pathological hop cannot spin forever.
    int guard = 0;
    const auto guardMax = 64 * targetLength;

    while (outPos < targetLength && guard++ < guardMax)
    {
        const auto targetSeconds = static_cast<double>(outPos) / sampleRate;
        const auto sourceSeconds = sourceTimeAtTarget(timeMap, targetSeconds);
        const auto srcCentre = std::max(0, static_cast<int>(sourceSeconds * sampleRate));

        // -------- windowed analysis --------
        for (int i = 0; i < fftSize; ++i)
        {
            const auto srcIdx = srcCentre - pad + i;
            const auto x = srcIdx >= 0 && srcIdx < inputLength ? input[srcIdx] : 0.0f;
            timeFrame[i] = std::complex<float>(x * window[i], 0.0f);
        }
        fft.perform(timeFrame.data(), spectrum.data(), false);
        for (int b = 0; b <= half; ++b)
            sourceMag[b] = std::abs(spectrum[b]);

        // -------- pitch / formant controls at the audible centre --------
        const auto audibleTargetSeconds = targetSeconds + (fftSize / 4) / sampleRate;
        const auto curvePos = audibleTargetSeconds * 1000.0 / framePeriod;
        const auto sourcePitch = curveAt(sourceMidi, curvePos);
        const auto targetPitch = curveAt(targetMidi, curvePos);
        const auto formantSemi = curveAt(formantSemitones, curvePos);

        if (sourcePitch <= 0.0f || targetPitch <= 0.0f)
        {
            // Unvoiced: pass the source spectrum straight through.
            for (int b = 0; b <= half; ++b)
                shiftedFrame[b] = spectrum[b];
        }
        else
        {
            const auto sourceHz = 440.0f * std::pow(2.0f, (sourcePitch - 69.0f) / 12.0f);
            const auto targetHz = 440.0f * std::pow(2.0f, (targetPitch - 69.0f) / 12.0f);
            const auto pitchRatio = targetHz / sourceHz;            // >1 = shift up
            const auto withinSemitones = juce::jlimit(-24.0f, 24.0f,
                                                     targetPitch - sourcePitch);
            // User formant ratio (1.0 = preserve the source vocal-tract).
            const auto formantFactor = std::exp2(formantSemi / 12.0f);

            // -------- spectral envelope (vocal-tract response) --------
            logPrefix[0] = 0.0f;
            for (int b = 0; b <= half; ++b)
                logPrefix[b + 1] = logPrefix[b]
                    + std::log(std::max(1.0e-20f, sourceMag[b]));
            for (int b = 0; b <= half; ++b)
            {
                const auto lo = std::max(0, b - wideRadius);
                const auto hi = std::min(half + 1, b + wideRadius + 1);
                envelope[b] = std::exp((logPrefix[hi] - logPrefix[lo])
                                      / std::max(1, hi - lo));
            }

            // -------- flatten & shift the harmonic fine structure --------
            for (int b = 0; b <= half; ++b)
                flatMag[b] = sourceMag[b] / std::max(1.0e-9f, envelope[b]);

            std::fill(shiftedMag.begin(), shiftedMag.end(), 0.0f);
            // Bandlimited extraction/distribution kernel width, ~1 bin at
            // 48 kHz / 4096 — wider at low pitch to keep the kernel stable.
            const auto binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
            const auto kernelRadius = std::max(1.5f, std::min(6.0f,
                (sourceHz / binHz) * 0.5f));

            // Walk source bins and distribute their flattened energy to the
            // target bin positions = srcBin * pitchRatio (formantFactor
            // scales which envelope to re-apply, not where the energy lands).
            for (int s = 1; s <= half; ++s)
            {
                if (flatMag[s] <= 1.0e-12f) continue;
                const auto tgtCenter = static_cast<float>(s) * pitchRatio;
                if (tgtCenter > static_cast<float>(half + kernelRadius)) break;
                const auto tLo = std::max(0, static_cast<int>(tgtCenter - kernelRadius));
                const auto tHi = std::min(half, static_cast<int>(tgtCenter + kernelRadius) + 1);
                for (int t = tLo; t < tHi; ++t)
                {
                    const auto dt = std::fabs(static_cast<float>(t) - tgtCenter);
                    const auto k = kernel01(1.0f - dt / kernelRadius);
                    if (k <= 0.0f) continue;
                    shiftedMag[t] += k * flatMag[s];
                }
            }

            // -------- re-apply envelope (formant preservation) --------
            // The vocal-tract response stays at the source frequency for a
            // neutral formant shift: target envelope at bin t is sampled
            // from the source envelope at t / pitchRatio.  A positive
            // formantFactor moves the envelope up by that multiplicative
            // factor (a downward env sample position).
            for (int t = 0; t <= half; ++t)
            {
                const auto envSourceBin = static_cast<float>(t) / pitchRatio
                                          / std::max(1.0e-3f, formantFactor);
                const auto env = envelope[clampBin(envSourceBin, half)];
                shiftedMag[t] *= env;
            }

            // -------- high-band anti-alias on upward moves --------
            // Melodyne does not fold energy above the pitched-band ceiling;
            // a smooth rolloff past the shifted Nyquist edge keeps upward
            // notes from turning brash.
            if (withinSemitones > 0.0f)
            {
                const auto cutoffBin = std::max(8, std::min(half,
                    static_cast<int>(std::round(half * 0.49 / std::exp2(withinSemitones / 12.0f)))));
                for (int b = cutoffBin; b < half; ++b)
                    shiftedMag[b] *= std::exp(-(static_cast<float>(b - cutoffBin) * 0.18f));
            }

            // -------- per-bin real gain mask, preserve source phase --------
            for (int b = 0; b <= half; ++b)
            {
                const auto denominator = std::max(1.0e-9f, sourceMag[b]);
                const auto mask = std::max(0.0f, shiftedMag[b] / denominator);
                shiftedFrame[b] = spectrum[b] * mask;
            }
        }

        // Conjugate mirror so the inverse stays real; zero out spurious
        // imaginary parts at DC and Nyquist.
        for (int b = 1; b < half; ++b)
            shiftedFrame[fftSize - b] = std::conj(shiftedFrame[b]);
        shiftedFrame[0] = std::complex<float>(shiftedFrame[0].real(), 0.0f);
        shiftedFrame[half] = std::complex<float>(shiftedFrame[half].real(), 0.0f);

        fft.perform(shiftedFrame.data(), inverse.data(), true);

        // -------- OLA following the local time map --------
        const auto warp = localWarpRatio(timeMap, targetSeconds);
        const auto targetHop = std::max(1, static_cast<int>(
            std::round(static_cast<float>(analysisHop) * warp)));
        for (int i = 0; i < fftSize; ++i)
        {
            const auto dest = outPos - pad + i;
            if (dest < 0 || dest >= targetLength) continue;
            const auto w = window[i];
            output[dest] += inverse[i].real() * w * windowGain;
            norm[dest] += w * w;
        }
        outPos += targetHop;
    }

    std::vector<float> result(targetLength, 0.0f);
    for (int i = 0; i < targetLength; ++i)
        result[i] = norm[i] > 1.0e-6f ? output[i] / norm[i] : 0.0f;
    return result;
}
} // namespace

// ---------------------------------------------------------------------------
//  Public entry point.
// ---------------------------------------------------------------------------
juce::AudioBuffer<float> Mld5Renderer::render(const Mld5RenderRequest& request) const
{
    if (request.input == nullptr || request.input->getNumSamples() == 0)
        return {};

    const auto channels = request.input->getNumChannels();
    const auto sourceSamples = request.input->getNumSamples();
    const auto targetSamples = request.targetSamples > 0
        ? request.targetSamples : sourceSamples;

    juce::AudioBuffer<float> result(channels, targetSamples);
    for (int c = 0; c < channels; ++c)
    {
        const auto rendered = mulssProcessChannel(
            request.input->getReadPointer(c), sourceSamples, targetSamples,
            request.sampleRate, request.framePeriodMs,
            request.sourceMidi, request.targetMidi, request.formantSemitones,
            request.timeMap);
        result.copyFrom(c, 0, rendered.data(), static_cast<int>(rendered.size()));
    }

    // ----- block power normalisation (binary 0x181a0636d, /1.5 factor) -----
    double sourcePower = 0.0, outPower = 0.0;
    for (int c = 0; c < channels; ++c)
    {
        const auto* in = request.input->getReadPointer(c);
        const auto* out = result.getReadPointer(c);
        for (int i = 0; i < sourceSamples; ++i) sourcePower += double(in[i]) * in[i];
        for (int i = 0; i < targetSamples; ++i) outPower += double(out[i]) * out[i];
    }
    if (sourcePower > 1.0e-12 && outPower > 1.0e-12)
    {
        const auto correction = juce::jlimit(0.55, 1.8,
            std::sqrt(sourcePower / outPower) / 1.5);
        result.applyGain(static_cast<float>(correction));
    }

    // ----- per-frame amplitude curve (independent of pitch) -----
    if (!request.noteGain.empty())
    {
        const auto framePeriod = std::max(0.1, request.framePeriodMs);
        for (int c = 0; c < channels; ++c)
        {
            auto* out = result.getWritePointer(c);
            for (int i = 0; i < targetSamples; ++i)
            {
                const auto pos = static_cast<double>(i) / request.sampleRate
                                 * 1000.0 / framePeriod;
                out[i] *= std::clamp(curveAt(request.noteGain, pos), 0.0f, 4.0f);
            }
        }
    }

    return result;
}
} // namespace hachi::backend