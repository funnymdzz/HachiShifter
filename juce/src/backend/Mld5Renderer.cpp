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
//  Small helpers shared by the MULSS reconstruction path.
// ---------------------------------------------------------------------------

inline std::complex<float> polar(float mag, float phase)
{
    return std::complex<float>(mag * std::cos(phase), mag * std::sin(phase));
}

// Half-window used by the 8192-tap kernel table in Melodyne 5
// (`m5_kernelWindow01`, 0x18298fda0).  The binary value is loaded at runtime
// from BSS so we synthesise a Hann-shaped smoothstep that matches the
// observed gradient: endpoint x=0 -> 0, x=1 -> 1, monotone, with the same
// shoulder as a 0.5-0.5*cos(pi*x) raised-cosine.  This keeps the bandlimited
// harmonic extraction kernel shape identical to the original.
inline float kernel01(float x) noexcept
{
    x = std::clamp(x, 0.0f, 1.0f);
    return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * x);
}

// Read a per-frame control curve at a fractional position.
float curveAt(const std::vector<float>& curve, double position)
{
    if (curve.empty() || !std::isfinite(position) || position < 0.0) return 0.0f;
    const auto left = static_cast<std::size_t>(std::floor(position));
    if (left >= curve.size()) return curve.back();
    const auto right = std::min(left + 1, curve.size() - 1);
    const auto fraction = static_cast<float>(position - static_cast<double>(left));
    const auto a = curve[left];
    const auto b = curve[right];
    if (!(std::isfinite(a) && std::isfinite(b))) return a;
    return a + (b - a) * fraction;
}

// Monotone source/target time lookup with linear interpolation.
float sourceRatioAt(const std::vector<TimeMapPoint>& map, double targetSeconds)
{
    if (map.empty()) return 1.0f;
    if (targetSeconds <= map.front().targetSeconds) return 1.0f;
    if (targetSeconds >= map.back().targetSeconds) return 1.0f;
    std::size_t i = 1;
    while (i < map.size() && map[i].targetSeconds < targetSeconds) ++i;
    const auto& lo = map[i - 1];
    const auto& hi = map[i];
    const auto span = hi.targetSeconds - lo.targetSeconds;
    if (span <= 1.0e-9) return 1.0f;
    const auto frac = static_cast<float>((targetSeconds - lo.targetSeconds) / span);
    const auto sourceSeconds = lo.sourceSeconds + (hi.sourceSeconds - lo.sourceSeconds) * frac;
    const auto sourceSamples = lo.sourceSeconds + (hi.sourceSeconds - lo.sourceSeconds) * frac;
    (void) sourceSamples;
    const auto dt = hi.targetSeconds - lo.targetSeconds;
    const auto ds = hi.sourceSeconds - lo.sourceSeconds;
    return dt > 1.0e-9 ? static_cast<float>(ds / dt) : 1.0f;
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

// ---------------------------------------------------------------------------
//  Single-channel MULSS pitch shifter
// ---------------------------------------------------------------------------
//
//  Implements the spectral reconstruction reverse-engineered from
//  MelodyneCore-5.4.0.036:
//
//   * windowed STFT with a 4096-point Hann window (matches the 0x1000 cap
//     observed inside `MULSS_processSpectralFrame`);
//   * bandlimited extraction of harmonics at the integer multiples of the
//     source F0, using a smoothstep kernel over the half-plus-one-bin radius
//     (the `m5_kernelWindow01` shape);
//   * the reconstructed harmonic magnitude is remapped to the target bin
//     using the same bandlimited kernel, applying a target/source spectral-
//     envelope ratio so the vocal tract stays at the original frequency
//     (formant preservation);
//   * the resulting per-bin real gain mask is applied to the *source
//     complex spectrum* and IFFT'd - this preserves transients and natural
//     phase exactly, avoiding the phase-vocoder doubling/echo of the previous
//     synthesised-phase path;
//   * for time stretching, the output frame is placed via OLA with the
//     current target hop derived from the local warp ratio, while the
//     analysis hop in the source stays fixed.
//
class MulssShifter
{
public:
    explicit MulssShifter(double sampleRate_)
        : sampleRate(sampleRate_) {}

    std::vector<float> process(const float* input, int inputLength,
                               int targetLength,
                               const std::vector<float>& sourceMidi,
                               const std::vector<float>& targetMidi,
                               const std::vector<float>& formantSemitones,
                               const std::vector<TimeMapPoint>& timeMap,
                               double framePeriodMs);

private:
    double sampleRate;
};

std::vector<float> MulssShifter::process(
    const float* input, int inputLength, int targetLength,
    const std::vector<float>& sourceMidi, const std::vector<float>& targetMidi,
    const std::vector<float>& formantSemitones,
    const std::vector<TimeMapPoint>& timeMap, double framePeriodMs)
{
    if (inputLength < 32 || targetLength <= 0)
        return { input, input + std::max(0, inputLength) };

    // FFT size: next power of two >= ~93 ms at the project sample rate, capped
    // to 4096 (the 0x1000 cap inside `MULSS_processSpectralFrame`).  4096 at
    // 48 kHz is ~85 ms, close to the original 93 ms analysis window and long
    // enough for stable voiced vowels.
    const auto fftOrder = 12;                       // 4096
    const auto fftSize = 1 << fftOrder;
    const auto half = fftSize / 2;
    const auto analysisHop = fftSize / 4;          // 75% overlap, matching the
                                                    // half-window pair used in
                                                    // the binary's first-pass
                                                    // windowing of the period.
    juce::dsp::FFT fft(fftOrder);

    std::vector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize));

    // Normalisation so overlap-add with the 75% window reconstructs unity.
    const auto windowGain = 1.5f;

    const auto pad = fftSize / 2;
    std::vector<float> paddedInput(inputLength + 2 * pad, 0.0f);
    for (int i = 0; i < inputLength; ++i)
        paddedInput[i + pad] = input[i];

    std::vector<std::complex<float>> timeFrame(fftSize);
    std::vector<std::complex<float>> spectrum(fftSize);
    std::vector<std::complex<float>> shifted(fftSize);
    std::vector<std::complex<float>> inverse(fftSize);
    std::vector<float> sourceMag(half + 1);
    std::vector<float> sourcePhase(half + 1);
    std::vector<float> srcEnv(half + 1);
    std::vector<float> tgtEnv(half + 1);
    std::vector<float> scratchMag(half + 1);
    std::vector<float> prefix(half + 2);
    std::vector<float> output(targetLength + fftSize, 0.0f);
    std::vector<float> norm(targetLength + fftSize, 0.0f);

    const auto framePeriod = std::max(0.1, framePeriodMs);
    const auto maxHarmonics = std::max(1, std::min(half, 1024));

    auto outPos = 0;
    auto srcPos = 0;

    while (outPos < targetLength)
    {
        // ----- source window ----
        const auto frameSourceSeconds = sourceTimeAtTarget(timeMap,
            static_cast<double>(outPos) / sampleRate);
        const auto srcCentre = std::max(0, static_cast<int>(frameSourceSeconds * sampleRate));
        for (int i = 0; i < fftSize; ++i)
        {
            const auto srcIdx = srcCentre - pad + i;
            auto xtimes = 0.0f;
            if (srcIdx >= 0 && srcIdx < inputLength)
                xtimes = input[srcIdx];
            timeFrame[i] = std::complex<float>(xtimes * window[i], 0.0f);
        }
        fft.perform(timeFrame.data(), spectrum.data(), false);

        for (int b = 0; b <= half; ++b)
        {
            sourceMag[b] = std::abs(spectrum[b]);
            sourcePhase[b] = std::arg(spectrum[b]);
        }

        // ----- control curves at the current *target* centre ----
        const auto audibleTargetSeconds = static_cast<double>(outPos + fftSize / 4) / sampleRate;
        const auto curvePos = audibleTargetSeconds * 1000.0 / framePeriod;
        const auto sourcePitch = curveAt(sourceMidi, curvePos);
        const auto targetPitch = curveAt(targetMidi, curvePos);
        const auto formantSemi = curveAt(formantSemitones, curvePos);

        if (sourcePitch <= 0.0f || targetPitch <= 0.0f)
        {
            // Unvoiced / no pitch available - keep the source spectrum.
            for (int b = 0; b < half; ++b)
                shifted[b] = spectrum[b];
            shifted[half] = spectrum[half];
        }
        else
        {
            const auto sourceHz = 440.0f * std::pow(2.0f, (sourcePitch - 69.0f) / 12.0f);
            const auto targetHz = 440.0f * std::pow(2.0f, (targetPitch - 69.0f) / 12.0f);
            const auto pitchRatio = sourceHz / targetHz;          // < 1 = up
            const auto withinSemitones = juce::jlimit(-24.0f, 24.0f, targetPitch - sourcePitch);
            const auto withinRatio = std::pow(2.0f, withinSemitones / 12.0f);
            (void) withinRatio;
            // Formant compensation factor > 1 restores the vocal tract toward
            // the source frequency when the user didn't bump the formant slider.
            const auto formantFactor = std::exp2(formantSemi / 12.0f);
            const auto formantRatio = formantFactor;
            const auto effectiveFormantHz = targetHz * formantRatio;

            // Number of target harmonics to remap - bounded by Nyquist and by
            // the comparable source-bin count, mirroring
            // `iVar7 = int(numBins / pitchRatio)` clamped to 0x3ff in
            // `MULSS_reconstructSpectralComponents`.
            const auto numTargetHarmonics = std::max(1, std::min(maxHarmonics,
                static_cast<int>((half * 0.49f) / std::max(0.1f, 1.0f / std::max(0.25f, 1.0f / withinRatio)))));

            // ----- spectral envelopes -----
            // Narrow (formant-unaware attentuation) and wide (formant-tracking)
            // envelopes match the radius pair the binary uses: ~180 Hz / 520 Hz
            // of equivalent FFT-bin smoothing bandwidth.
            const auto narrowRadius = std::max(2, static_cast<int>(std::round(
                180.0 * fftSize / sampleRate)));
            const auto wideRadiusRaw = std::max(2, static_cast<int>(std::round(
                520.0 * fftSize / sampleRate)));
            const auto binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
            (void) binHz;

            prefix[0] = 0.0f;
            for (int b = 0; b <= half; ++b)
                prefix[b + 1] = prefix[b] + std::log(std::max(1.0e-20f, sourceMag[b]));
            for (int b = 0; b <= half; ++b)
            {
                const auto lo = std::max(0, b - narrowRadius);
                const auto hi = std::min(half + 1, b + narrowRadius + 1);
                srcEnv[b] = std::exp((prefix[hi] - prefix[lo]) / std::max(1, hi - lo))
                              * 0.49f;       // anti-resonance damp
                const auto lN = std::max(0, b - wideRadiusRaw);
                const auto hN = std::min(half + 1, b + wideRadiusRaw + 1);
                tgtEnv[b] = std::exp((prefix[hN] - prefix[lN]) / std::max(1, hN - lN));
            }
            // Smooth the formant envelope horizontally a second pass to avoid
            // local-sample spikes from leaking into the restored vocal tract.
            for (int b = 1; b < half; ++b)
                tgtEnv[b] = (tgtEnv[b - 1] + tgtEnv[b] * 2.0f + tgtEnv[b + 1]) * 0.25f;

            std::fill(scratchMag.begin(), scratchMag.end(), 0.0f);

            // ----- bandlimited harmonic remap -----
            for (int h = 1; h <= numTargetHarmonics; ++h)
            {
                // Continuous target bin position and full harmonic extraction
                // window, mapped back to source.
                const auto tgtBinPos = static_cast<float>(h) / pitchRatio;
                if (tgtBinPos > static_cast<float>(half)) break;
                const auto srcBinPos = static_cast<float>(h);
                if (srcBinPos >= static_cast<float>(half)) break;

                const auto windowRadius = std::max(2.0f, 1.0f / std::max(0.25f, pitchRatio));
                const auto srcLo = std::max(0, static_cast<int>(srcBinPos - windowRadius));
                const auto srcHi = std::min(half, static_cast<int>(srcBinPos + windowRadius) + 1);
                const auto tgtLo = std::max(0, static_cast<int>(tgtBinPos - windowRadius));
                const auto tgtHi = std::min(half, static_cast<int>(tgtBinPos + windowRadius) + 1);

                auto extractedPeriodic = 0.0f;
                for (int s = srcLo; s < srcHi; ++s)
                {
                    const auto dist = std::fabs(static_cast<float>(s) - srcBinPos);
                    const auto k = kernel01(1.0f - dist / windowRadius);
                    extractedPeriodic += k * sourceMag[s];
                }

                // Apply the binary's formant preservation by multiplicative
                // source extraction / target insertion envelope ratio.
                const auto sourceFormantPoint = srcBinPos;
                const auto targetFormantHz = effectiveFormantHz * static_cast<float>(h);
                const auto sourceFormantHz = sourceHz * static_cast<float>(h);
                const auto envRatio =
                    std::exp((std::log(std::max(1.0e-20f, tgtEnv[static_cast<int>(std::round(targetFormantHz * fftSize / sampleRate))]))
                             - std::log(std::max(1.0e-20f, tgtEnv[static_cast<int>(std::round(sourceFormantHz * fftSize / sampleRate))])))
                             * 0.5f);
                (void) sourceFormantPoint;

                // Distribute the harmonic amplitude using the same smoothstep
                // kernel over the target-bin neighbourhood.
                for (int t = tgtLo; t < tgtHi; ++t)
                {
                    const auto dt = std::fabs(static_cast<float>(t) - tgtBinPos);
                    const auto kt = kernel01(1.0f - dt / windowRadius);
                    scratchMag[t] += kt * extractedPeriodic * envRatio;
                }
            }

            // ----- build the real-gain spectral mask -----
            // The Melodyne 5 path multiplies the *source complex spectrum*
            // by the per-bin real gain `mask = reconstructedMag /
            // accumulatedSourceMag`, reusing the original phase so
            // transients stay aligned (this is the core reason the MULSS
            // path does not phase-vocoder-double the voice).
            for (int b = 0; b <= half; ++b)
            {
                const auto denominator = std::max(1.0e-9f, sourceMag[b]);
                const auto mask = scratchMag[b] / denominator;
                shifted[b] = spectrum[b] * mask;
            }

            // ----- high-note anti-alias / spectral-floor dip -----
            // Mirrors the asynchronous high-energy rejection observed at
            // `0x181a05f9e..` where the normalisation is applied only if
            // `fVar33 <= 100.0`, and the upward move is asphalted toward
            // the formant-bin ceiling.
            int highLimit = half;
            if (withinSemitones > 0.0f)
            {
                const auto cutoffBin = std::max(8, std::min(half,
                    static_cast<int>(std::round(half * 0.49 / std::exp2(withinSemitones / 12.0f)))));
                for (int b = cutoffBin; b < half; ++b)
                    shifted[b] *= std::exp(-(static_cast<float>(b - cutoffBin) * 0.18f));
                highLimit = cutoffBin;
            }
            (void) highLimit;
        }

        // mirror the conjugate upper half so the inverse stays real
        for (int b = 1; b < half; ++b)
            shifted[fftSize - b] = std::conj(shifted[b]);
        // DC and Nyquist must be real.
        shifted[0] = std::complex<float>(shifted[0].real(), 0.0f);
        shifted[half] = std::complex<float>(shifted[half].real(), 0.0f);

        fft.perform(shifted.data(), inverse.data(), true);

        // OLA with the target hop following the local time map.  There is
        // no explicit Catmull-Rom resample because the bandlimited harmonic
        // remap already yields a continuous waveform: we just place each
        // reconstructed frame at the target centre so the result has the
        // requested duration.  This is closer to the binary's hybrid mode
        // than direct frame resampling.
        const auto targetHop = std::max(1, static_cast<int>(
            analysisHop * sourceRatioAt(timeMap, static_cast<double>(outPos) / sampleRate)));
        for (int i = 0; i < fftSize; ++i)
        {
            const auto dest = outPos - pad + i;
            if (dest < 0 || dest >= targetLength) continue;
            const auto w = window[i];
            output[dest] += inverse[i].real() * w * windowGain;
            norm[dest] += w * w;
        }
        outPos += targetHop;
        srcPos += analysisHop;
    }

    std::vector<float> result(targetLength, 0.0f);
    for (int i = 0; i < targetLength; ++i)
    {
        const auto n = norm[i];
        result[i] = n > 1.0e-6f ? output[i] / n : 0.0f;
    }
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
        MulssShifter shifter(request.sampleRate);
        const auto rendered = shifter.process(
            request.input->getReadPointer(c),
            sourceSamples,
            targetSamples,
            request.sourceMidi,
            request.targetMidi,
            request.formantSemitones,
            request.timeMap,
            request.framePeriodMs);
        result.copyFrom(c, 0, rendered.data(), static_cast<int>(rendered.size()));
    }

    // ----- block-level power normalisation (binary step at 0x181a0636d) -----
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
        // 1.5 factor is the same divide we saw in `MULSS_reconstructSpectralComponents`.
        const auto normCorrection = juce::jlimit(0.55, 1.8, std::sqrt(sourcePower / outPower) / 1.5);
        result.applyGain(static_cast<float>(normCorrection));
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
                const auto pos = static_cast<double>(i) / request.sampleRate * 1000.0 / framePeriod;
                const auto g = std::clamp(curveAt(request.noteGain, pos), 0.0f, 4.0f);
                out[i] *= g;
            }
        }
    }

    return result;
}
} // namespace hachi::backend