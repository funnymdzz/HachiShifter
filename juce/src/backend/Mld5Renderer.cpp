#include "Mld5Renderer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>

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

// Catmull-Rom interpolation (harness `catmullRom`), used to resample the
// pitch-stretched synthesis buffer back onto the target-time grid.
inline float catmullRom(const std::vector<float>& x, int n, float pos) noexcept
{
    if (n <= 0) return 0.0f;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > static_cast<float>(n - 1)) pos = static_cast<float>(n - 1);
    const auto l = static_cast<int>(pos);
    const auto f = pos - static_cast<float>(l);
    const auto at = [&](int i) { return x[static_cast<std::size_t>(std::clamp(i, 0, n - 1))]; };
    const auto a = at(l - 1), b = at(l), c = at(l + 1), d = at(l + 2);
    return b + 0.5f * (c - a) * f
         + (2.0f * a - 5.0f * b + 4.0f * c - d) * f * f * 0.5f
         + (3.0f * (b - c) + d - a) * f * f * f * 0.5f;
}

// Tuning knobs mirror the standalone harness (reverse/algo_test/test_algo.cpp).
// Every MLD5_* environment variable the harness honours is honoured here with
// the same default, so a configuration tuned in the harness reproduces exactly
// when the editor is launched with those environment variables set.
float envFloat(const char* name, float fallback) noexcept
{
    const char* value = std::getenv(name);
    return value ? static_cast<float>(std::atof(value)) : fallback;
}

bool envBool(const char* name, bool fallback) noexcept
{
    return envFloat(name, fallback ? 1.0f : 0.0f) > 0.5f;
}

// Normalised-autocorrelation F0 estimate over one analysis window.
// Returns 0 when the window is unvoiced / aperiodic.
float estimateF0(const float* x, int n, double sr) noexcept
{
    int minLag = std::max(4, static_cast<int>(sr / 1200.0));
    int maxLag = std::min(n / 2, static_cast<int>(sr / 55.0));
    if (maxLag <= minLag) return 0.0f;
    int bestLag = 0;
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double c = 0.0, e0 = 0.0, e1 = 0.0;
        for (int i = 0; i + lag < n; ++i)
        {
            c += double(x[i]) * x[i + lag];
            e0 += double(x[i]) * x[i];
            e1 += double(x[i + lag]) * x[i + lag];
        }
        double nc = (e0 > 0.0 && e1 > 0.0) ? c / std::sqrt(e0 * e1) : 0.0;
        if (nc > best) { best = nc; bestLag = lag; }
    }
    if (bestLag <= 0 || best < 0.35) return 0.0f;
    return static_cast<float>(sr / bestLag);
}

std::vector<std::pair<double, double>> parseEq(const char* cfg)
{
    std::vector<std::pair<double, double>> points;
    if (cfg == nullptr) return points;
    const char* p = cfg;
    while (*p)
    {
        double hz = std::strtod(p, nullptr);
        while (*p && *p != ':') ++p;
        if (*p == ':') ++p;
        double db = std::strtod(p, nullptr);
        points.emplace_back(hz, db);
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
    }
    std::sort(points.begin(), points.end());
    return points;
}

// ---------------------------------------------------------------------------
//  Tuned mld5 single-channel shifter.
//
//  Faithful port of the harness algorithm (reverse/algo_test/test_algo.cpp
//  `mld5`) into the production renderer:
//    1. windowed STFT (Hann, 4096-bin frame, 8x-oversampled hop);
//    2. wide log-domain (or RMS for downward moves) spectral envelope — the
//       vocal-tract response;
//    3. formant pre-compensation mask: target envelope sampled at
//       b * pitchRatio / formantFactor, so the vocal-tract stays at the
//       source frequency for a neutral formant shift;
//    4. optional harmonic-component map (per-harmonic energy redistribution,
//       component scale/exponent, legacy or block normalisation);
//    5. phase-vocoder synthesis: instantaneous bin frequency from phase
//       unwrap, accumulated over the *pitch-stretched* hop (harness-exact),
//       so the stretched buffer carries the original frequency;
//    6. spectral-peak locking (synthesisPhase of the nearest peak plus the
//       current relative phase), optional peak boost / valley cut / pure
//       harmonic synthesis, floor, tilt and EQ;
//    7. IFFT and overlap-add onto the pitch-stretched source grid, then
//       Catmull-Rom resample back onto the target-time grid (this is what
//       raises the pitch by pitchRatio); optional post-envelope and
//       per-block power normalisation.
//
//  Unlike the earlier MULSS binary reimplementation this is the phase-vocoder
//  form the harness was tuned on, and the MLD5_* knobs drive the same results.
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

    // Identity gate (mirrors the harness): no pitch edit, no formant edit and
    // no time warp means the output is the input, byte for byte.
    auto hasEdit = false;
    const auto curveCount = std::min(sourceMidi.size(), targetMidi.size());
    for (std::size_t k = 0; k < curveCount && !hasEdit; ++k)
        if (sourceMidi[k] > 0.0f && targetMidi[k] > 0.0f
            && std::abs(targetMidi[k] - sourceMidi[k]) > 1.0e-4f)
            hasEdit = true;
    for (const auto v : formantSemitones)
        if (std::abs(v) > 1.0e-4f) { hasEdit = true; break; }
    if (targetLength == inputLength && !hasEdit)
        for (const auto& point : timeMap)
            if (std::abs(point.sourceSeconds - point.targetSeconds) > 1.0e-6)
            { hasEdit = true; break; }
    if (!hasEdit && targetLength == inputLength)
        return { input, input + inputLength };

    // ---- FFT size: 4096 for small shifts, 2048 above |8| semi (harness
    //      default), overridable with MLD5_FFT. ----
    double maxAbsShift = 0.0;
    for (std::size_t k = 0; k < curveCount; ++k)
        if (sourceMidi[k] > 0.0f && targetMidi[k] > 0.0f)
            maxAbsShift = std::max(maxAbsShift, std::abs(
                static_cast<double>(targetMidi[k]) - sourceMidi[k]));
    const auto defaultFft = maxAbsShift < 8.0 ? 4096.0 : 2048.0;
    const int fftSize = envFloat("MLD5_FFT", static_cast<float>(defaultFft)) >= 3072.0f
        ? 4096 : 2048;
    auto fftOrder = 0;
    for (auto s = fftSize; s > 1; s >>= 1) ++fftOrder;
    const int half = fftSize / 2;
    const int analysisHop = fftSize / 8;
    const int pad = fftSize / 2;

    const float envelopeHz = envFloat("MLD5_ENV_HZ", 300.0f);
    const float maskBlend = std::clamp(envFloat("MLD5_MASK_BLEND", 1.0f), 0.0f, 1.0f);
    const float peakThreshold = std::pow(10.0f, envFloat("MLD5_LOCK_DB", -60.0f) / 20.0f);
    const float outputGain = std::max(0.0f, envFloat("MLD5_GAIN", 1.0f));
    const float pitchCents = envFloat("MLD5_PITCH_CENTS", 0.0f);
    const bool rmsOverride = std::getenv("MLD5_ENV_RMS") != nullptr
        && envFloat("MLD5_ENV_RMS", 0.0f) > 0.5f;
    const bool componentMap = envBool("MLD5_COMPONENT_MAP", false);
    const bool harmonicMap = envBool("MLD5_HARMONIC_MAP", false);
    const float harmonicMix = std::clamp(envFloat("MLD5_HARMONIC_MIX", 1.0f), 0.0f, 1.0f);
    const bool peakSynth = envBool("MLD5_PEAK_SYNTH", false);
    const float sourceF0Override = envFloat("MLD5_SOURCE_F0", 0.0f);
    const float componentScale = std::max(0.25f, envFloat("MLD5_COMPONENT_SCALE", 1.0f));
    const float componentExponent = std::max(0.0f, envFloat("MLD5_COMPONENT_EXP", 1.0f));
    const bool blockNormalisation = envBool("MLD5_BLOCK_NORM", true);
    const bool postEnvelope = envBool("MLD5_POST_ENV", false);
    const float tiltDb = envFloat("MLD5_TILT_DB", 0.0f);
    const int tiltEndHz = std::max(200, static_cast<int>(envFloat("MLD5_TILT_HZ", 8000.0f)));
    const int tiltEndBin = std::clamp(static_cast<int>(std::lround(tiltEndHz * fftSize / sampleRate)),
                                      1, half);
    const float floorDb = envFloat("MLD5_FLOOR_DB", -200.0f);
    const float peakBoostDb = envFloat("MLD5_PEAK_BOOST", 0.0f);
    const float valleyCutDb = envFloat("MLD5_VALLEY_CUT", 0.0f);
    const auto eqPoints = parseEq(std::getenv("MLD5_EQ"));

    const int envelopeRadius = std::max(2, static_cast<int>(std::round(envelopeHz * fftSize / sampleRate)));
    const auto framePeriod = std::max(0.1, framePeriodMs);

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize - 1));

    // Upper bound on the pitch ratio across the whole target, used to size the
    // pitch-stretched synthesis buffer.
    float maxPitchRatio = 1.0f;
    for (int t = 0; t < targetLength; t += analysisHop)
    {
        const auto cp = static_cast<double>(t) / sampleRate * 1000.0 / framePeriod;
        const auto sp = curveAt(sourceMidi, cp);
        const auto tp = curveAt(targetMidi, cp);
        if (sp > 0.0f && tp > 0.0f)
            maxPitchRatio = std::max(maxPitchRatio,
                std::pow(2.0f, (tp - sp + pitchCents / 100.0f) / 12.0f));
    }
    const auto stretchedLength = static_cast<int>(
        std::ceil(targetLength * maxPitchRatio)) + fftSize * 4;
    std::vector<float> stretched(stretchedLength, 0.0f);
    std::vector<float> norm(stretchedLength, 0.0f);
    std::vector<float> output(targetLength, 0.0f);
    std::vector<std::complex<float>> timeFrame(fftSize);
    std::vector<std::complex<float>> spectrum(fftSize);
    std::vector<std::complex<float>> shiftedFrame(fftSize);
    std::vector<std::complex<float>> inverse(fftSize);
    std::vector<float> frameTime(fftSize);
    std::vector<float> srcMag(half + 1);
    std::vector<float> logp(half + 2);
    std::vector<float> powerPrefix(half + 2);
    std::vector<float> env(half + 1);
    std::vector<float> masks(half + 1);
    std::vector<float> harmonicMasks(half + 1, 1.0f);
    std::vector<float> prevPhase(half + 1);
    std::vector<float> synthesisPhase(half + 1);
    std::vector<float> currentPhase(half + 1);

    auto outPos = 0;
    auto prevSrcCentre = -1;
    auto prevOutPos = -1;
    auto prevSynthesisPosition = -1;
    int guard = 0;
    const auto guardMax = 64 * std::max(targetLength, 1);

    while (outPos < targetLength && guard++ < guardMax)
    {
        const auto targetSeconds = static_cast<double>(outPos) / sampleRate;
        const auto sourceSeconds = sourceTimeAtTarget(timeMap, targetSeconds);
        const auto srcCentre = std::max(0, static_cast<int>(sourceSeconds * sampleRate));

        // -------- pitch / formant at the audible centre --------
        const auto audible = targetSeconds + (analysisHop * 0.5) / sampleRate;
        const auto curvePos = audible * 1000.0 / framePeriod;
        const auto sourcePitch = curveAt(sourceMidi, curvePos);
        const auto targetPitch = curveAt(targetMidi, curvePos);
        const auto formantSemi = curveAt(formantSemitones, curvePos);

        const auto voiced = sourcePitch > 0.0f && targetPitch > 0.0f;
        float pitchRatio = 1.0f, formantFactor = 1.0f;
        if (voiced)
        {
            pitchRatio = std::pow(2.0f,
                (targetPitch - sourcePitch + pitchCents / 100.0f) / 12.0f);
            formantFactor = std::pow(2.0f, formantSemi / 12.0f);
        }

        // Pitch-stretched position of this frame on the synthesis grid. The
        // harness places frame m at lround(analysisPosition * pitchRatio);
        // here the *target* clock (outPos) is stretched, so a time warp only
        // changes which source content each frame reads (via srcCentre) while
        // the pitch shift stays exactly pitchRatio — the two stay independent.
        // The phase vocoder advances by the stretched hop so the stretched
        // buffer carries the *original* frequency, which the resample stage
        // (below) raises by pitchRatio.
        const auto synthesisPosition = static_cast<int>(std::lround(outPos * pitchRatio));
        const auto synthesisHop = prevSynthesisPosition >= 0
            ? std::max(1, synthesisPosition - prevSynthesisPosition) : analysisHop;

        // -------- windowed analysis --------
        for (int i = 0; i < fftSize; ++i)
        {
            const auto srcIdx = srcCentre - pad + i;
            const auto x = srcIdx >= 0 && srcIdx < inputLength ? input[srcIdx] : 0.0f;
            frameTime[i] = x;
            timeFrame[i] = std::complex<float>(x * window[i], 0.0f);
        }
        fft.perform(timeFrame.data(), spectrum.data(), false);
        for (int b = 0; b <= half; ++b)
            srcMag[b] = std::abs(spectrum[b]);

        // -------- spectral envelope (vocal-tract response) --------
        logp[0] = 0.0f;
        powerPrefix[0] = 0.0f;
        for (int b = 0; b <= half; ++b)
        {
            logp[b + 1] = logp[b] + std::log(std::max(1.0e-20f, srcMag[b]));
            powerPrefix[b + 1] = powerPrefix[b] + srcMag[b] * srcMag[b];
        }
        if (componentMap)
        {
            env = srcMag;
            env[0] = 0.0f;
            const auto smoothing = std::exp(-2.5f / std::max(1.0e-3f, pitchRatio));
            for (int b = 1; b <= half; ++b)
                env[b] = (1.0f - smoothing) * env[b] + smoothing * env[b - 1];
            for (int b = half - 1; b >= 0; --b)
                env[b] = (1.0f - smoothing) * env[b] + smoothing * env[b + 1];
        }
        else
        {
            const auto rms = rmsOverride || pitchRatio < 1.0f;
            for (int b = 0; b <= half; ++b)
            {
                const auto lo = std::max(0, b - envelopeRadius);
                const auto hi = std::min(half + 1, b + envelopeRadius + 1);
                env[b] = rms
                    ? std::sqrt((powerPrefix[hi] - powerPrefix[lo]) / std::max(1, hi - lo))
                    : std::exp((logp[hi] - logp[lo]) / std::max(1, hi - lo));
            }
        }

        // -------- optional harmonic-component map --------
        if (harmonicMap && voiced)
        {
            std::fill(harmonicMasks.begin(), harmonicMasks.end(), 1.0f);
            const auto sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override : estimateF0(frameTime.data(), fftSize, sampleRate);
            const auto pitchBins = componentScale * sourceF0 * fftSize / sampleRate;
            if (pitchBins >= 2.0f)
            {
                const auto radius = std::max(2, static_cast<int>(std::ceil(pitchBins)));
                const auto harmonicCount = std::min(1023,
                    static_cast<int>(std::ceil(half / pitchBins)));
                const auto envelopeRatio = pitchRatio / std::max(1.0e-3f, formantFactor);
                std::vector<float> sourceAccum(half + 1), targetAccum(half + 1);
                for (int harmonic = 0; harmonic <= harmonicCount; ++harmonic)
                {
                    const auto centre = harmonic * pitchBins;
                    const auto targetCentre = centre * envelopeRatio;
                    const auto lo = std::max(0, static_cast<int>(std::floor(centre) - radius));
                    const auto hi = std::min(half, static_cast<int>(std::ceil(centre) + radius));
                    const auto targetLo = std::max(0, static_cast<int>(std::floor(targetCentre) - radius));
                    const auto targetHi = std::min(half, static_cast<int>(std::ceil(targetCentre) + radius));
                    float sourceEnergy = 0.0f, targetEnergy = 0.0f;
                    for (int b = lo; b <= hi; ++b)
                    {
                        const auto weight = kernel01(1.0f - std::fabs(b - centre) / pitchBins);
                        sourceEnergy += weight * srcMag[b];
                    }
                    for (int b = targetLo; b <= targetHi; ++b)
                    {
                        const auto weight = kernel01(1.0f - std::fabs(b - targetCentre) / pitchBins);
                        targetEnergy += weight * srcMag[b];
                    }
                    const auto gain = sourceEnergy > 1.0e-7f
                        ? std::clamp(targetEnergy / sourceEnergy, 0.05f, 20.0f) : 1.0f;
                    for (int b = lo; b <= hi; ++b)
                    {
                        const auto weight = kernel01(1.0f - std::fabs(b - centre) / pitchBins);
                        const auto component = weight * srcMag[b];
                        sourceAccum[b] += component;
                        targetAccum[b] += gain * component;
                    }
                }
                float sourceSum = 0.0f, targetSum = 0.0f;
                for (int b = 1; b <= half; ++b)
                {
                    sourceSum += sourceAccum[b];
                    targetSum += targetAccum[b];
                }
                const auto legacyNorm = envBool("MLD5_LEGACY_NORM", false);
                if (legacyNorm)
                {
                    const auto componentNorm = targetSum > 1.0e-7f
                        ? std::min(100.0f, sourceSum / targetSum) : 1.0f;
                    for (int b = 0; b <= half; ++b)
                        if (sourceAccum[b] > 1.0e-7f)
                            harmonicMasks[b] = std::clamp(
                                componentNorm * targetAccum[b] / sourceAccum[b], 0.05f, 20.0f);
                }
                else
                {
                    float totalSourceMag = 0.0f, totalTargetMag = 0.0f;
                    for (int b = 1; b <= half; ++b)
                    {
                        totalSourceMag += srcMag[b];
                        totalTargetMag += targetAccum[b];
                    }
                    const auto blockNorm = totalTargetMag > 1.0e-7f
                        ? std::min(100.0f, totalSourceMag / totalTargetMag) : 1.0f;
                    for (int b = 0; b <= half; ++b)
                        if (srcMag[b] > 1.0e-7f)
                            harmonicMasks[b] = std::clamp(
                                blockNorm * targetAccum[b] / srcMag[b], 0.05f, 20.0f);
                }
            }
        }

        // -------- formant pre-compensation mask --------
        for (int b = 0; b <= half; ++b)
        {
            float selectedMask = 1.0f;
            if (voiced)
            {
                const auto envPos = static_cast<float>(b) * pitchRatio / std::max(1.0e-3f, formantFactor);
                const auto envVal = env[clampBin(envPos, half)];
                const auto rawMask = std::clamp(envVal / std::max(1.0e-9f, env[b]), 0.05f, 20.0f);
                selectedMask = harmonicMap
                    ? std::pow(rawMask, 1.0f - harmonicMix)
                        * std::pow(harmonicMasks[b], harmonicMix * componentExponent)
                    : rawMask;
            }
            masks[b] = std::pow(selectedMask, maskBlend);
        }

        // -------- phase-vocoder tracking --------
        for (int b = 0; b <= half; ++b)
            currentPhase[b] = std::arg(spectrum[b]);
        const auto srcHop = prevSrcCentre >= 0 ? std::max(1, srcCentre - prevSrcCentre) : analysisHop;
        if (prevSrcCentre < 0)
        {
            for (int b = 0; b <= half; ++b)
            {
                prevPhase[b] = currentPhase[b];
                synthesisPhase[b] = currentPhase[b];
            }
        }
        else
        {
            for (int b = 0; b <= half; ++b)
            {
                const auto twoPi = 2.0f * juce::MathConstants<float>::pi;
                const auto expected = twoPi * b * srcHop / fftSize;
                auto delta = currentPhase[b] - prevPhase[b] - expected;
                delta -= twoPi * std::round(delta / twoPi);
                const auto trueFrequency = twoPi * b / fftSize + delta / srcHop;
                synthesisPhase[b] += trueFrequency * synthesisHop;
                prevPhase[b] = currentPhase[b];
            }
        }

        // -------- spectral peaks & peak locking --------
        const auto maximumMagnitude = *std::max_element(srcMag.begin(), srcMag.end());
        std::vector<int> peaks;
        for (int b = 1; b < half; ++b)
            if (srcMag[b] >= maximumMagnitude * peakThreshold
                && srcMag[b] >= srcMag[b - 1] && srcMag[b] > srcMag[b + 1])
                peaks.push_back(b);
        for (int b = 0, peakIndex = 0; b <= half; ++b)
        {
            auto outputPhase = synthesisPhase[b];
            if (!peaks.empty())
            {
                while (peakIndex + 1 < static_cast<int>(peaks.size())
                    && std::abs(peaks[peakIndex + 1] - b) < std::abs(peaks[peakIndex] - b))
                    ++peakIndex;
                const auto peak = peaks[peakIndex];
                outputPhase = synthesisPhase[peak] + currentPhase[b] - currentPhase[peak];
            }
            shiftedFrame[b] = std::polar(srcMag[b] * masks[b], outputPhase);
        }

        // -------- floor / tilt / EQ / peak boost / valley cut / harmonic synth --------
        if (floorDb > -190.0f)
        {
            const auto framePeak = *std::max_element(srcMag.begin(), srcMag.end());
            const auto floorLevel = framePeak * std::pow(10.0f, floorDb / 20.0f);
            for (int b = 1; b <= half; ++b)
                if (std::abs(shiftedFrame[b]) < floorLevel) shiftedFrame[b] = std::complex<float>(0.0f, 0.0f);
        }
        if (std::fabs(tiltDb) > 0.5f)
        {
            for (int b = 1; b <= half; ++b)
            {
                const auto factor = b <= tiltEndBin
                    ? std::pow(10.0f, tiltDb * b / (20.0f * (tiltEndBin + 1)))
                    : std::pow(10.0f, tiltDb / 20.0f);
                shiftedFrame[b] *= factor;
            }
        }
        if (!eqPoints.empty())
        {
            for (int b = 1; b <= half; ++b)
            {
                const auto fhz = static_cast<double>(b) * sampleRate / fftSize;
                auto db = eqPoints.front().second;
                if (fhz >= eqPoints.front().first && fhz <= eqPoints.back().first && eqPoints.size() > 1)
                {
                    for (std::size_t k = 1; k < eqPoints.size(); ++k)
                        if (fhz <= eqPoints[k].first)
                        {
                            const auto t = (std::log(fhz) - std::log(eqPoints[k - 1].first))
                                / (std::log(eqPoints[k].first) - std::log(eqPoints[k - 1].first));
                            db = eqPoints[k - 1].second
                                + t * (eqPoints[k].second - eqPoints[k - 1].second);
                            break;
                        }
                }
                else if (fhz > eqPoints.back().first)
                    db = eqPoints.back().second;
                shiftedFrame[b] *= std::pow(10.0f, static_cast<float>(db) / 20.0f);
            }
        }
        if (peakBoostDb > 0.1f && harmonicMap)
        {
            const auto sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override : estimateF0(frameTime.data(), fftSize, sampleRate);
            const auto pitchBins = componentScale * sourceF0 * fftSize / sampleRate;
            if (pitchBins >= 2.0f)
            {
                const auto boostFactor = std::pow(10.0f, peakBoostDb / 20.0f);
                const auto peakRadius = std::max(1, static_cast<int>(std::ceil(pitchBins / 3.0f)));
                const auto harmonicCount = std::min(1023, static_cast<int>(std::ceil(half / pitchBins)));
                for (int harmonic = 1; harmonic <= harmonicCount; ++harmonic)
                {
                    const auto b = static_cast<int>(std::lround(harmonic * pitchBins));
                    if (b < 1 || b > half) continue;
                    for (int j = -peakRadius; j <= peakRadius; ++j)
                        if (b + j >= 1 && b + j <= half)
                            shiftedFrame[b + j] *= boostFactor;
                }
            }
        }
        if (valleyCutDb > 0.1f && harmonicMap)
        {
            const auto sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override : estimateF0(frameTime.data(), fftSize, sampleRate);
            const auto pitchBins = componentScale * sourceF0 * fftSize / sampleRate;
            if (pitchBins >= 2.0f)
            {
                const auto cutFactor = std::pow(10.0f, -valleyCutDb / 20.0f);
                const auto peakRadius = std::max(1, static_cast<int>(std::ceil(pitchBins / 3.0f)));
                const auto harmonicCount = std::min(1023, static_cast<int>(std::ceil(half / pitchBins)));
                for (int b = 1; b <= half; ++b)
                {
                    if (static_cast<float>(b) < pitchBins) continue;
                    const auto h = static_cast<float>(b) / pitchBins;
                    const auto nearest = std::fabs(h - std::round(h));
                    if (nearest * pitchBins > peakRadius)
                        shiftedFrame[b] *= cutFactor;
                }
            }
        }
        if (peakSynth && harmonicMap)
        {
            const auto sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override : estimateF0(frameTime.data(), fftSize, sampleRate);
            const auto pitchBins = componentScale * sourceF0 * fftSize / sampleRate;
            if (pitchBins >= 2.0f)
            {
                const auto harmonicCount = std::min(1023, static_cast<int>(std::ceil(half / pitchBins)));
                for (int b = 0; b <= half; ++b) shiftedFrame[b] = std::complex<float>(0.0f, 0.0f);
                for (int harmonic = 1; harmonic <= harmonicCount; ++harmonic)
                {
                    const auto b = static_cast<int>(std::lround(harmonic * pitchBins));
                    if (b < 1 || b > half) continue;
                    auto outputPhase = synthesisPhase[b];
                    if (!peaks.empty())
                    {
                        auto nearest = b;
                        for (const auto pk : peaks)
                            if (std::abs(pk - b) < std::abs(nearest - b)) nearest = pk;
                        outputPhase = synthesisPhase[nearest] + currentPhase[b] - currentPhase[nearest];
                    }
                    shiftedFrame[b] = std::polar(srcMag[b] * masks[b], outputPhase);
                }
            }
        }

        // Conjugate mirror so the inverse stays real; zero out spurious
        // imaginary parts at DC and Nyquist.
        for (int b = 1; b < half; ++b)
            shiftedFrame[fftSize - b] = std::conj(shiftedFrame[b]);
        shiftedFrame[0] = std::complex<float>(shiftedFrame[0].real(), 0.0f);
        shiftedFrame[half] = std::complex<float>(shiftedFrame[half].real(), 0.0f);

        fft.perform(shiftedFrame.data(), inverse.data(), true);

    // -------- OLA onto the pitch-stretched target grid --------
    // Frames are placed at synthesisPosition (the target position scaled by
    // pitchRatio), mirroring the harness; the resample stage below reads the
    // stretched buffer back onto the target-time grid, which is what raises
    // the pitch by pitchRatio. The target hop still advances the target
    // clock through the local time map.
        const auto warp = localWarpRatio(timeMap, targetSeconds);
        const auto targetHop = std::max(1, static_cast<int>(
            std::round(static_cast<float>(analysisHop) * warp)));
        for (int i = 0; i < fftSize; ++i)
        {
            const auto dest = synthesisPosition + i;
            if (dest < 0 || dest >= stretchedLength) continue;
            const auto w = window[i];
            stretched[dest] += inverse[i].real() * w;
            norm[dest] += w * w;
        }
        prevSrcCentre = srcCentre;
        prevOutPos = outPos;
        prevSynthesisPosition = synthesisPosition;
        outPos += targetHop;
    }

    for (int i = 0; i < stretchedLength; ++i)
        if (norm[i] > 1.0e-6f) stretched[i] /= norm[i];

    // -------- resample the stretched buffer onto the target-time grid --------
    // output[t] = stretched[latency + t * pitchRatio(t)] with
    // latency = fftSize/2 * pitchRatio(t), exactly the harness resample
    // (stretched[latency + i * pitchRatio]) generalised to a time-varying
    // ratio. Reading on the *target* clock (not sourceTimeAtTarget) keeps the
    // pitch shift equal to pitchRatio independent of the time warp, while the
    // content still follows the time map through the frame placement above.
    for (int t = 0; t < targetLength; ++t)
    {
        const auto cp = static_cast<double>(t) / sampleRate * 1000.0 / framePeriod;
        const auto sp = curveAt(sourceMidi, cp);
        const auto tp = curveAt(targetMidi, cp);
        const auto ratio = sp > 0.0f && tp > 0.0f
            ? std::pow(2.0f, (tp - sp + pitchCents / 100.0f) / 12.0f) : 1.0f;
        const auto position = (static_cast<float>(t) + 0.5f * static_cast<float>(fftSize)) * ratio;
        output[t] = catmullRom(stretched, stretchedLength, position);
    }

    // -------- optional post-envelope correction --------
    if (postEnvelope)
    {
        constexpr int postN = 2048, postHop = 256, postH = postN / 2;
        juce::dsp::FFT postFft(11); // fixed 2048-point pass, independent of the
                                    // main fftSize so 4096 frames stay in range
        const auto postRadius = std::max(2, static_cast<int>(std::lround(envelopeHz * postN / sampleRate)));
        std::vector<float> corrected(targetLength + postN, 0.0f);
        std::vector<float> correctedNorm(targetLength + postN, 0.0f);
        std::vector<float> postWindow(postN), sourceMag(postH + 1), outputMag(postH + 1);
        std::vector<float> sourcePrefix(postH + 2), outputPrefix(postH + 2);
        for (int i = 0; i < postN; ++i)
            postWindow[i] = 0.5f - 0.5f * std::cos(
                juce::MathConstants<float>::twoPi * i / (postN - 1));
        for (int pos = 0; pos < targetLength; pos += postHop)
        {
            std::vector<std::complex<float>> sourceFrame(postN), outputFrame(postN);
            const auto sourceCentre = static_cast<int>(sourceTimeAtTarget(
                timeMap, static_cast<double>(pos) / sampleRate) * sampleRate);
            for (int i = 0; i < postN; ++i)
            {
                const auto sIdx = sourceCentre - postN / 2 + i;
                const auto oIdx = pos - postN / 2 + i;
                const auto s = sIdx >= 0 && sIdx < inputLength ? input[sIdx] : 0.0f;
                const auto o = oIdx >= 0 && oIdx < targetLength ? output[oIdx] : 0.0f;
                sourceFrame[i] = std::complex<float>(s * postWindow[i], 0.0f);
                outputFrame[i] = std::complex<float>(o * postWindow[i], 0.0f);
            }
            postFft.perform(sourceFrame.data(), sourceFrame.data(), false);
            postFft.perform(outputFrame.data(), outputFrame.data(), false);
            sourcePrefix[0] = outputPrefix[0] = 0.0f;
            for (int b = 0; b <= postH; ++b)
            {
                sourceMag[b] = std::abs(sourceFrame[b]);
                outputMag[b] = std::abs(outputFrame[b]);
                sourcePrefix[b + 1] = sourcePrefix[b] + std::log(std::max(1.0e-20f, sourceMag[b]));
                outputPrefix[b + 1] = outputPrefix[b] + std::log(std::max(1.0e-20f, outputMag[b]));
            }
            const auto curvePos = static_cast<double>(pos) / sampleRate * 1000.0 / framePeriod;
            const auto formantFactor = std::exp2(curveAt(formantSemitones, curvePos) / 12.0f);
            for (int b = 0; b <= postH; ++b)
            {
                const auto sourceBin = clampBin(static_cast<float>(b) / std::max(1.0e-3f, formantFactor), postH);
                const auto slo = std::max(0, sourceBin - postRadius);
                const auto shi = std::min(postH + 1, sourceBin + postRadius + 1);
                const auto olo = std::max(0, b - postRadius);
                const auto ohi = std::min(postH + 1, b + postRadius + 1);
                const auto sourceEnvelope = std::exp((sourcePrefix[shi] - sourcePrefix[slo]) / std::max(1, shi - slo));
                const auto outputEnvelope = std::exp((outputPrefix[ohi] - outputPrefix[olo]) / std::max(1, ohi - olo));
                const auto gain = std::clamp(sourceEnvelope / std::max(1.0e-9f, outputEnvelope), 0.1f, 10.0f);
                outputFrame[b] *= gain;
            }
            for (int b = 1; b < postH; ++b) outputFrame[postN - b] = std::conj(outputFrame[b]);
            outputFrame[0] = std::complex<float>(outputFrame[0].real(), 0.0f);
            outputFrame[postH] = std::complex<float>(outputFrame[postH].real(), 0.0f);
            postFft.perform(outputFrame.data(), outputFrame.data(), true);
            for (int i = 0; i < postN; ++i)
            {
                const auto idx = pos - postN / 2 + i;
                if (idx < 0 || idx >= targetLength) continue;
                corrected[idx] += outputFrame[i].real() * postWindow[i];
                correctedNorm[idx] += postWindow[i] * postWindow[i];
            }
        }
        for (int i = 0; i < targetLength; ++i)
            if (correctedNorm[i] > 1.0e-6f) output[i] = corrected[i] / correctedNorm[i];
    }

    // -------- per-block power normalisation (harness MLD5_BLOCK_NORM) --------
    if (blockNormalisation)
    {
        constexpr int powerWindow = 1024, powerHop = 256;
        const auto points = (targetLength + powerHop - 1) / powerHop + 1;
        std::vector<float> gains(points, 1.0f);
        for (int point = 0; point < points; ++point)
        {
            const auto center = point * powerHop;
            const auto sourceCentre = static_cast<int>(sourceTimeAtTarget(
                timeMap, static_cast<double>(center) / sampleRate) * sampleRate);
            const auto slo = std::max(0, sourceCentre - powerWindow / 2);
            const auto shi = std::min(inputLength, sourceCentre + powerWindow / 2);
            const auto olo = std::max(0, center - powerWindow / 2);
            const auto ohi = std::min(targetLength, center + powerWindow / 2);
            double inputPower = 0.0, outputPower = 0.0;
            for (int i = slo; i < shi; ++i)
                inputPower += double(input[i]) * input[i];
            for (int i = olo; i < ohi; ++i)
                outputPower += double(output[i]) * output[i];
            const auto curvePos = static_cast<double>(center) / sampleRate * 1000.0 / framePeriod;
            const auto sourcePitch = curveAt(sourceMidi, curvePos);
            const auto targetPitch = curveAt(targetMidi, curvePos);
            const auto ratio = sourcePitch > 0.0f && targetPitch > 0.0f
                ? std::pow(2.0f, (targetPitch - sourcePitch) / 12.0f) : 1.0f;
            const auto powerFactor = ratio >= 1.0f
                ? std::min(1.15f, 1.0f + 0.14f * std::log2(ratio))
                : std::pow(ratio, 0.52f);
            gains[point] = outputPower > 1.0e-12
                ? std::clamp(powerFactor * static_cast<float>(std::sqrt(inputPower / outputPower)), 0.25f, 4.0f)
                : 1.0f;
        }
        for (int i = 0; i < targetLength; ++i)
        {
            const auto position = static_cast<float>(i) / powerHop;
            const auto left = std::min(points - 1, static_cast<int>(position));
            const auto right = std::min(points - 1, left + 1);
            const auto amount = position - left;
            output[i] *= gains[left] + (gains[right] - gains[left]) * amount;
        }
    }

    std::vector<float> result(targetLength, 0.0f);
    for (int i = 0; i < targetLength; ++i)
        result[i] = output[i] * outputGain;
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

    // ----- block power normalisation (whole-clip safety net; the per-block
    //      harness normalisation above already matches loudness locally, so
    //      this only guards the clip against residual block-norm clamps). -----
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
            std::sqrt(sourcePower / outPower));
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
