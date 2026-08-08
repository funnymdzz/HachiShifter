// Standalone algorithm test harness for mld5 (MULSS) and mld3 (PSOLA).
// No JUCE dependency; self-contained FFT + WAV IO so we can iterate on
// the DSP in pure C++ without the full GUI build.
//
// Build:  g++ -O2 -std=c++17 -o test_algo test_algo.cpp
// Use:    ./test_algo <in.wav> <out.wav> <mld5|mld3> <shift_semitones> [formant_semi] [start_s] [dur_s] [reference.wav] [score_dur_s] [score_start_s]
//
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using cpx = std::complex<float>;
using std::vector;

// ---------------------------------------------------------------------------
// WAV IO (16-bit PCM + 32-bit float; mono/stereo; any sample rate).
// ---------------------------------------------------------------------------
struct Wav {
    int sr = 0, ch = 0, frames = 0;
    vector<float> samples;   // interleaved
};

bool readWav(const std::string& path, Wav& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char hdr[12]; if (std::fread(hdr, 1, 12, f) != 12) { std::fclose(f); return false; }
    if (std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) { std::fclose(f); return false; }
    int fmt_sr = 0, fmt_ch = 0, fmt_bits = 0; int fmt_code = 0;
    bool data_found = false; long data_off = -1, data_size = 0;
    while (true) {
        char id[4]; uint32_t sz;
        if (std::fread(id, 1, 4, f) != 4) break;
        if (std::fread(&sz, 4, 1, f) != 1) break;
        long pos = std::ftell(f);
        if (!std::memcmp(id, "fmt ", 4)) {
            uint16_t code, ch, bits; uint32_t sr, byteRate; uint16_t block;
            std::fread(&code, 2, 1, f); std::fread(&ch, 2, 1, f);
            std::fread(&sr, 4, 1, f); std::fread(&byteRate, 4, 1, f); std::fread(&block, 2, 1, f);
            std::fread(&bits, 2, 1, f);
            (void) byteRate;
            fmt_code = code; fmt_sr = (int)sr; fmt_ch = (int)ch; fmt_bits = (int)bits;
        } else if (!std::memcmp(id, "data", 4)) {
            data_found = true; data_off = std::ftell(f); data_size = (long)sz;
        }
        std::fseek(f, pos + sz + (sz & 1), SEEK_SET);
    }
    if (!data_found || fmt_sr == 0 || fmt_ch == 0) { std::fclose(f); return false; }
    out.sr = fmt_sr; out.ch = fmt_ch;
    std::fseek(f, data_off, SEEK_SET);
    if (fmt_code == 1 && fmt_bits == 16) {
        int n = (int)(data_size / (fmt_ch * 2));
        out.frames = n; out.samples.resize((size_t)n * fmt_ch);
        for (int i = 0; i < n * fmt_ch; ++i) {
            int16_t v = 0; std::fread(&v, 2, 1, f);
            out.samples[i] = v / 32768.0f;
        }
    } else if (fmt_code == 3 && fmt_bits == 32) {
        int n = (int)(data_size / (fmt_ch * 4));
        out.frames = n; out.samples.resize((size_t)n * fmt_ch);
        std::fread(out.samples.data(), 4, n * fmt_ch, f);
    } else { std::fclose(f); return false; }
    std::fclose(f);
    return true;
}

void writeWav(const std::string& path, const Wav& w) {
    FILE* f = std::fopen(path.c_str(), "wb");
    uint32_t data_sz = (uint32_t)w.samples.size() * 2;
    uint32_t riff_sz = 36 + data_sz;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff_sz, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmt_sz = 16; uint16_t code = 1, ch = (uint16_t)w.ch, bits = 16;
    uint32_t sr = (uint32_t)w.sr, bps = w.sr * w.ch * 2; uint16_t block = w.ch * 2;
    std::fwrite(&fmt_sz, 4, 1, f); std::fwrite(&code, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&sr, 4, 1, f); std::fwrite(&bps, 4, 1, f); std::fwrite(&block, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&data_sz, 4, 1, f);
    for (float x : w.samples) {
        float c = std::clamp(x, -1.0f, 1.0f);
        int16_t v = (int16_t)(c * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

// 24-bit uncompressed BMP writer for visual spectrogram inspection.
void writeBmp(const std::string& path, int width, int height, const vector<uint8_t>& rgb) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    int stride = (width * 3 + 3) & ~3;
    uint32_t dataSize = stride * height, fileSize = 54 + dataSize;
    uint8_t h[54]{};
    h[0] = 'B'; h[1] = 'M';
    std::memcpy(h + 2, &fileSize, 4);
    uint32_t offset = 54; std::memcpy(h + 10, &offset, 4);
    uint32_t dib = 40; std::memcpy(h + 14, &dib, 4);
    std::memcpy(h + 18, &width, 4); std::memcpy(h + 22, &height, 4);
    uint16_t planes = 1, bits = 24;
    std::memcpy(h + 26, &planes, 2); std::memcpy(h + 28, &bits, 2);
    std::memcpy(h + 34, &dataSize, 4);
    std::fwrite(h, 1, 54, f);
    vector<uint8_t> row(stride, 0);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            size_t s = ((size_t)y * width + x) * 3;
            row[x * 3 + 0] = rgb[s + 2];
            row[x * 3 + 1] = rgb[s + 1];
            row[x * 3 + 2] = rgb[s + 0];
        }
        std::fwrite(row.data(), 1, stride, f);
    }
    std::fclose(f);
}

// ---------------------------------------------------------------------------
// Radix-2 iterative FFT (power of two, in place).
// ---------------------------------------------------------------------------
void fft(vector<cpx>& a, bool inv) {
    int n = (int)a.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int b = n >> 1;
        while (j & b) { j ^= b; b >>= 1; }
        j |= b;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = (inv ? 2.0f : -2.0f) * float(M_PI) / len;
        cpx wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cpx w(1, 0);
            for (int k = 0; k < len / 2; ++k) {
                cpx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wlen;
            }
        }
    }
    if (inv) for (auto& x : a) x /= float(n);
}

struct Spectrogram {
    int width = 0, height = 0;
    vector<float> db;
};

Spectrogram makeSpectrogram(const vector<float>& samples) {
    constexpr int fftSize = 2048, hop = 256;
    Spectrogram result;
    result.height = fftSize / 2 + 1;
    result.width = samples.size() >= fftSize
        ? 1 + (int(samples.size()) - fftSize) / hop : 0;
    result.db.resize((size_t)result.width * result.height);
    vector<cpx> frame(fftSize);
    vector<float> window(fftSize);
    float windowSum = 0.0f;
    for (int i = 0; i < fftSize; ++i) {
        window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (fftSize - 1));
        windowSum += window[i];
    }
    for (int x = 0; x < result.width; ++x) {
        int start = x * hop;
        for (int i = 0; i < fftSize; ++i)
            frame[i] = cpx(samples[start + i] * window[i], 0.0f);
        fft(frame, false);
        for (int bin = 0; bin < result.height; ++bin) {
            float amplitude = 2.0f * std::abs(frame[bin]) / windowSum;
            result.db[(size_t)bin * result.width + x] =
                std::max(-100.0f, 20.0f * std::log10(std::max(amplitude, 1e-5f)));
        }
    }
    return result;
}

void writeSpectrogram(const std::string& path, const Spectrogram& spec) {
    vector<uint8_t> rgb((size_t)spec.width * spec.height * 3);
    for (int y = 0; y < spec.height; ++y) {
        int bin = spec.height - 1 - y;
        for (int x = 0; x < spec.width; ++x) {
            float level = (spec.db[(size_t)bin * spec.width + x] + 100.0f) / 100.0f;
            uint8_t v = (uint8_t)std::lround(255.0f * std::clamp(level, 0.0f, 1.0f));
            size_t p = ((size_t)y * spec.width + x) * 3;
            rgb[p] = rgb[p + 1] = rgb[p + 2] = v;
        }
    }
    writeBmp(path, spec.width, spec.height, rgb);
}

void writeSpectrogramComparison(const std::string& basePath,
                                const vector<float>& input,
                                const vector<float>& output,
                                const char* label) {
    Spectrogram inSpec = makeSpectrogram(input), outSpec = makeSpectrogram(output);
    int width = std::min(inSpec.width, outSpec.width);
    int height = std::min(inSpec.height, outSpec.height);
    if (width <= 0 || height <= 0) return;
    writeSpectrogram(basePath + ".input.bmp", inSpec);
    writeSpectrogram(basePath + ".output.bmp", outSpec);

    vector<float> differences;
    differences.reserve((size_t)width * height);
    vector<uint8_t> rgb((size_t)width * height * 3);
    for (int y = 0; y < height; ++y) {
        int bin = height - 1 - y;
        for (int x = 0; x < width; ++x) {
            float d = std::fabs(inSpec.db[(size_t)bin * inSpec.width + x]
                              - outSpec.db[(size_t)bin * outSpec.width + x]);
            differences.push_back(d);
            float t = std::clamp(d / 40.0f, 0.0f, 1.0f);
            size_t p = ((size_t)y * width + x) * 3;
            rgb[p] = (uint8_t)std::lround(255.0f * t);
            rgb[p + 1] = (uint8_t)std::lround(255.0f * std::max(0.0f, 1.0f - std::fabs(2.0f * t - 1.0f)));
            rgb[p + 2] = (uint8_t)std::lround(255.0f * (1.0f - t));
        }
    }
    writeBmp(basePath + ".diff.bmp", width, height, rgb);
    double sum = 0.0;
    for (float d : differences) sum += d;
    std::sort(differences.begin(), differences.end());
    size_t p95Index = (size_t)std::floor(0.95 * (differences.size() - 1));
    printf("[spectrogram:%s] fft=2048 hop=256 scale=-100..0dBFS mae=%.3fdB p95=%.3fdB\n",
           label, sum / differences.size(), differences[p95Index]);
}

// ---------------------------------------------------------------------------
// Helpers shared by both algorithms.
// ---------------------------------------------------------------------------
inline float kernel01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return 0.5f - 0.5f * std::cos(float(M_PI) * x);
}
inline int clampBin(float b, int half) { return std::max(0, std::min(half, (int)std::round(b))); }

// Catmull-Rom interpolation over a real buffer, clamped at the edges.
inline float catmullRom(const vector<float>& x, int n, float pos) {
    if (n <= 0) return 0.0f;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > float(n - 1)) pos = float(n - 1);
    int l = (int)pos;
    float f = pos - l;
    auto at = [&](int i) { return x[(size_t)std::clamp(i, 0, n - 1)]; };
    float a = at(l - 1), b = at(l), c = at(l + 1), d = at(l + 2);
    return b + 0.5f * (c - a) * f
         + (2.0f * a - 5.0f * b + 4.0f * c - d) * f * f * 0.5f
         + (3.0f * (b - c) + d - a) * f * f * f * 0.5f;
}

inline float sincResample(const vector<float>& x, int n, float pos, int taps, float cutoff) {
    if (n <= 0) return 0.0f;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > float(n - 1)) pos = float(n - 1);
    int centre = (int)pos;
    float f = pos - centre;
    double acc = 0.0, weightSum = 0.0;
    for (int k = centre - taps; k <= centre + taps; ++k) {
        double t = double(k) + double(f) - double(pos);
        double a = t;
        double w = std::sin(M_PI * a) / (M_PI * a);
        if (std::fabs(a) < 1e-6) w = 1.0;
        w *= 0.5 + 0.5 * std::cos((M_PI * a) / (taps + 1));
        if (k < 0 || k >= n) continue;
        acc += w * x[(size_t)k];
        weightSum += w;
    }
    return weightSum > 0.0 ? float(acc / weightSum) : 0.0f;
}

// Normalised-autocorrelation F0 estimate over one analysis window.
// Returns 0 when the window is unvoiced / aperiodic.
inline float estimateF0(const float* x, int n, double sr) {
    int minLag = std::max(4, (int)(sr / 1200.0));
    int maxLag = std::min(n / 2, (int)(sr / 55.0));
    if (maxLag <= minLag) return 0.0f;
    int bestLag = 0;
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double c = 0.0, e0 = 0.0, e1 = 0.0;
        for (int i = 0; i + lag < n; ++i) {
            c += double(x[i]) * x[i + lag];
            e0 += double(x[i]) * x[i];
            e1 += double(x[i + lag]) * x[i + lag];
        }
        double nc = (e0 > 0.0 && e1 > 0.0) ? c / std::sqrt(e0 * e1) : 0.0;
        if (nc > best) { best = nc; bestLag = lag; }
    }
    if (bestLag <= 0 || best < 0.35) return 0.0f;
    return float(sr / bestLag);
}

// ---------------------------------------------------------------------------
// mld5: MULSS-style formant pre-compensation + time-domain Catmull-Rom
// resampling + OLA.  The spectral mask moves the vocal-tract envelope down by
// the pitch ratio (and applies the user formant offset) so that the
// subsequent time resampling restores it to the source frequency.  At
// pitchRatio=1 and formantFactor=1 the mask is 1 and the resample is a
// no-op, so the output is the input (identity gate).
// ---------------------------------------------------------------------------
vector<float> mld5(const vector<float>& in, double sr, float shift_semi, float formant_semi) {
    if (std::fabs(shift_semi) < 1e-6f && std::fabs(formant_semi) < 1e-6f)
        return in;
    const auto envFloat = [](const char* name, float fallback) {
        const char* value = std::getenv(name);
        return value ? (float)std::atof(value) : fallback;
    };
    const float defaultFft = std::fabs(shift_semi) < 8.0f ? 4096.0f : 2048.0f;
    const int N = envFloat("MLD5_FFT", defaultFft) >= 3072.0f ? 4096 : 2048;
    const int analysisHop = N / 8, H = N / 2;
    const int len = (int)in.size();
    const float pitchRatio = std::pow(2.0f, (shift_semi + envFloat("MLD5_PITCH_CENTS", 0.0f) / 100.0f) / 12.0f);
    const float formantFactor = std::pow(2.0f, formant_semi / 12.0f);
    const float envelopeHz = envFloat("MLD5_ENV_HZ", 300.0f);
    const float maskBlend = std::clamp(envFloat("MLD5_MASK_BLEND", 1.0f), 0.0f, 1.0f);
    const float peakThreshold = std::pow(10.0f, envFloat("MLD5_LOCK_DB", -60.0f) / 20.0f);
    const float outputGain = std::max(0.0f, envFloat("MLD5_GAIN", 1.0f));
    const int sincTaps = (int)envFloat("MLD5_SINC_TAPS", 0.0f);
    const char* envelopeMode = std::getenv("MLD5_ENV_RMS");
    const bool rmsEnvelope = envelopeMode ? std::atof(envelopeMode) > 0.5
                                          : pitchRatio < 1.0f;
    const bool componentMap = envFloat("MLD5_COMPONENT_MAP", 0.0f) > 0.5f;
    const bool harmonicMap = envFloat("MLD5_HARMONIC_MAP", 0.0f) > 0.5f;
    const float harmonicMix = std::clamp(envFloat("MLD5_HARMONIC_MIX", 1.0f), 0.0f, 1.0f);
    const bool peakSynth = envFloat("MLD5_PEAK_SYNTH", 0.0f) > 0.5f;
    const float sourceF0Override = envFloat("MLD5_SOURCE_F0", 0.0f);
    const float componentScale = std::max(0.25f, envFloat("MLD5_COMPONENT_SCALE", 1.0f));
    const float componentExponent = std::max(0.0f, envFloat("MLD5_COMPONENT_EXP", 1.0f));
    const bool blockNormalisation = envFloat("MLD5_BLOCK_NORM", 1.0f) > 0.5f;
    const bool postEnvelope = envFloat("MLD5_POST_ENV", 0.0f) > 0.5f;
    const float tiltDb = envFloat("MLD5_TILT_DB", 0.0f);
    const int tiltEndHz = std::max(200, (int)envFloat("MLD5_TILT_HZ", 8000.0f));
    int tiltEndBin = std::clamp((int)std::lround(tiltEndHz * N / sr), 1, H);
    std::vector<std::pair<double, double>> eqPoints;
    if (const char* eqCfg = std::getenv("MLD5_EQ")) {
        const char* p = eqCfg;
        while (*p) {
            double hz = std::strtod(p, nullptr);
            while (*p && *p != ':') ++p;
            if (*p == ':') ++p;
            double db = std::strtod(p, nullptr);
            eqPoints.emplace_back(hz, db);
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
        std::sort(eqPoints.begin(), eqPoints.end());
    }
    const int frames = (len + analysisHop - 1) / analysisHop + 1;
    const int stretchedLength = (int)std::ceil(len * pitchRatio) + N * 2;
    vector<float> stretched(stretchedLength, 0.0f), norm(stretchedLength, 0.0f);
    vector<float> window(N), srcMag(H + 1), logp(H + 2), powerPrefix(H + 2), env(H + 1);
    vector<float> previousPhase(H + 1), synthesisPhase(H + 1);
    const int envelopeRadius = std::max(2, (int)std::round(envelopeHz * N / sr));
    for (int i = 0; i < N; ++i)
        window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (N - 1));

    int previousSynthesisPosition = 0;
    for (int frameIndex = 0; frameIndex < frames; ++frameIndex) {
        const int analysisPosition = frameIndex * analysisHop;
        const int synthesisPosition = (int)std::lround(analysisPosition * pitchRatio);
        const int synthesisHop = frameIndex == 0 ? 0
            : synthesisPosition - previousSynthesisPosition;
        vector<cpx> fr(N);
        vector<float> frameTime(N);
        for (int i = 0; i < N; ++i) {
            int sourceIndex = analysisPosition + i - N / 2;
            float sample = sourceIndex >= 0 && sourceIndex < len ? in[sourceIndex] : 0.0f;
            frameTime[i] = sample;
            fr[i] = cpx(sample * window[i], 0.0f);
        }
        fft(fr, false);
        for (int b = 0; b <= H; ++b) srcMag[b] = std::abs(fr[b]);
        logp[0] = 0.0f;
        powerPrefix[0] = 0.0f;
        for (int b = 0; b <= H; ++b) {
            logp[b + 1] = logp[b] + std::log(std::max(1e-20f, srcMag[b]));
            powerPrefix[b + 1] = powerPrefix[b] + srcMag[b] * srcMag[b];
        }
        if (componentMap) {
            env = srcMag;
            env[0] = 0.0f;
            const float smoothing = std::exp(-2.5f / pitchRatio);
            for (int b = 1; b <= H; ++b)
                env[b] = (1.0f - smoothing) * env[b] + smoothing * env[b - 1];
            for (int b = H - 1; b >= 0; --b)
                env[b] = (1.0f - smoothing) * env[b] + smoothing * env[b + 1];
        } else {
            for (int b = 0; b <= H; ++b) {
                int lo = std::max(0, b - envelopeRadius), hi = std::min(H + 1, b + envelopeRadius + 1);
                env[b] = rmsEnvelope
                    ? std::sqrt((powerPrefix[hi] - powerPrefix[lo]) / std::max(1, hi - lo))
                    : std::exp((logp[hi] - logp[lo]) / std::max(1, hi - lo));
            }
        }

        vector<cpx> spectrum(N);
        vector<float> currentPhase(H + 1), masks(H + 1), harmonicMasks(H + 1, 1.0f);
        if (harmonicMap) {
            float sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override
                : estimateF0(frameTime.data(), N, sr);
            float pitchBins = componentScale * sourceF0 * N / float(sr);
            if (pitchBins >= 2.0f) {
                int radius = std::max(2, (int)std::ceil(pitchBins));
                int harmonicCount = std::min(1023, (int)std::ceil(H / pitchBins));
                float envelopeRatio = pitchRatio / std::max(1e-3f, formantFactor);
                vector<float> sourceAccum(H + 1), targetAccum(H + 1);
                for (int harmonic = 0; harmonic <= harmonicCount; ++harmonic) {
                    float center = harmonic * pitchBins;
                    float targetCenter = center * envelopeRatio;
                    int lo = std::max(0, (int)std::floor(center) - radius);
                    int hi = std::min(H, (int)std::ceil(center) + radius);
                    int targetLo = std::max(0, (int)std::floor(targetCenter) - radius);
                    int targetHi = std::min(H, (int)std::ceil(targetCenter) + radius);
                    float sourceEnergy = 0.0f, targetEnergy = 0.0f;
                    for (int b = lo; b <= hi; ++b) {
                        float weight = kernel01(1.0f - std::fabs(b - center) / pitchBins);
                        sourceEnergy += weight * srcMag[b];
                    }
                    for (int b = targetLo; b <= targetHi; ++b) {
                        float weight = kernel01(1.0f - std::fabs(b - targetCenter) / pitchBins);
                        targetEnergy += weight * srcMag[b];
                    }
                    float gain = sourceEnergy > 1e-7f
                        ? std::clamp(targetEnergy / sourceEnergy, 0.05f, 20.0f)
                        : 1.0f;
                    for (int b = lo; b <= hi; ++b) {
                        float weight = kernel01(1.0f - std::fabs(b - center) / pitchBins);
                        float component = weight * srcMag[b];
                        sourceAccum[b] += component;
                        targetAccum[b] += gain * component;
                    }
                }
                float sourceSum = 0.0f, targetSum = 0.0f;
                for (int b = 1; b <= H; ++b) {
                    sourceSum += sourceAccum[b];
                    targetSum += targetAccum[b];
                }
                const bool legacyNorm = envFloat("MLD5_LEGACY_NORM", 0.0f) > 0.5f;
                if (legacyNorm) {
                    float componentNorm = targetSum > 1e-7f
                        ? std::min(100.0f, sourceSum / targetSum)
                        : 1.0f;
                    for (int b = 0; b <= H; ++b)
                        if (sourceAccum[b] > 1e-7f)
                            harmonicMasks[b] = std::clamp(
                                componentNorm * targetAccum[b] / sourceAccum[b], 0.05f, 20.0f);
                } else {
                    float totalSourceMag = 0.0f, totalTargetMag = 0.0f;
                    for (int b = 1; b <= H; ++b) {
                        totalSourceMag += srcMag[b];
                        totalTargetMag += targetAccum[b];
                    }
                    float blockNorm = totalTargetMag > 1e-7f
                        ? std::min(100.0f, totalSourceMag / totalTargetMag)
                        : 1.0f;
                    for (int b = 0; b <= H; ++b)
                        if (srcMag[b] > 1e-7f)
                            harmonicMasks[b] = std::clamp(
                                blockNorm * targetAccum[b] / srcMag[b], 0.05f, 20.0f);
                }
            }
        }
        for (int b = 0; b <= H; ++b) {
            float envPos = float(b) * pitchRatio / std::max(1e-3f, formantFactor);
            float envVal = env[clampBin(envPos, H)];
            float rawMask = std::clamp(envVal / std::max(1e-9f, env[b]), 0.05f, 20.0f);
            float selectedMask = harmonicMap
                ? std::pow(rawMask, 1.0f - harmonicMix)
                    * std::pow(harmonicMasks[b], harmonicMix * componentExponent)
                : rawMask;
            masks[b] = std::pow(selectedMask, maskBlend);
            float phase = currentPhase[b] = std::arg(fr[b]);
            if (frameIndex == 0) {
                previousPhase[b] = phase;
                synthesisPhase[b] = phase;
            } else {
                float expected = 2.0f * float(M_PI) * b * analysisHop / N;
                float delta = phase - previousPhase[b] - expected;
                delta -= 2.0f * float(M_PI) * std::round(delta / (2.0f * float(M_PI)));
                float trueFrequency = 2.0f * float(M_PI) * b / N + delta / analysisHop;
                synthesisPhase[b] += trueFrequency * synthesisHop;
                previousPhase[b] = phase;
            }
        }
        float maximumMagnitude = *std::max_element(srcMag.begin(), srcMag.end());
        vector<int> peaks;
        for (int b = 1; b < H; ++b)
            if (srcMag[b] >= maximumMagnitude * peakThreshold
                && srcMag[b] >= srcMag[b - 1] && srcMag[b] > srcMag[b + 1])
                peaks.push_back(b);
        for (int b = 0, peakIndex = 0; b <= H; ++b) {
            float outputPhase = synthesisPhase[b];
            if (!peaks.empty()) {
                while (peakIndex + 1 < (int)peaks.size()
                    && std::abs(peaks[peakIndex + 1] - b) < std::abs(peaks[peakIndex] - b))
                    ++peakIndex;
                int peak = peaks[peakIndex];
                outputPhase = synthesisPhase[peak] + currentPhase[b] - currentPhase[peak];
            }
            spectrum[b] = std::polar(srcMag[b] * masks[b], outputPhase);
        }
        const float floorDb = envFloat("MLD5_FLOOR_DB", -200.0f);
        if (floorDb > -190.0f) {
            float framePeak = *std::max_element(srcMag.begin(), srcMag.end());
            float floorLevel = framePeak * std::pow(10.0f, floorDb / 20.0f);
            for (int b = 1; b <= H; ++b)
                if (std::abs(spectrum[b]) < floorLevel) spectrum[b] = cpx(0.0f, 0.0f);
        }
        if (std::fabs(tiltDb) > 0.5f) {
            for (int b = 1; b <= H; ++b) {
                float factor = b <= tiltEndBin
                    ? std::pow(10.0f, tiltDb * float(b) / (20.0f * float(tiltEndBin + 1)))
                    : std::pow(10.0f, tiltDb / 20.0f);
                spectrum[b] *= factor;
            }
        }
        if (!eqPoints.empty()) {
            for (int b = 1; b <= H; ++b) {
                double fhz = double(b) * sr / double(N);
                double db = eqPoints.front().second;
                if (fhz >= eqPoints.front().first
                    && fhz <= eqPoints.back().first && eqPoints.size() > 1) {
                    for (size_t k = 1; k < eqPoints.size(); ++k)
                        if (fhz <= eqPoints[k].first) {
                            double t = (std::log(fhz) - std::log(eqPoints[k - 1].first))
                                / (std::log(eqPoints[k].first) - std::log(eqPoints[k - 1].first));
                            db = eqPoints[k - 1].second
                                + t * (eqPoints[k].second - eqPoints[k - 1].second);
                            break;
                        }
                    if (fhz > eqPoints.back().first) db = eqPoints.back().second;
                } else if (fhz < eqPoints.front().first) {
                    db = eqPoints.front().second;
                } else if (fhz > eqPoints.back().first) {
                    db = eqPoints.back().second;
                }
                spectrum[b] *= std::pow(10.0f, float(db) / 20.0f);
            }
        }
        const float peakBoostDb = envFloat("MLD5_PEAK_BOOST", 0.0f);
        if (peakBoostDb > 0.1f && harmonicMap) {
            float sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override
                : estimateF0(frameTime.data(), N, sr);
            float pitchBins = componentScale * sourceF0 * N / float(sr);
            if (pitchBins >= 2.0f) {
                float boostFactor = std::pow(10.0f, peakBoostDb / 20.0f);
                int peakRadius = std::max(1, (int)std::ceil(pitchBins / 3.0f));
                int harmonicCount = std::min(1023, (int)std::ceil(H / pitchBins));
                for (int harmonic = 1; harmonic <= harmonicCount; ++harmonic) {
                    int b = (int)std::lround(harmonic * pitchBins);
                    if (b < 1 || b > H) continue;
                    for (int j = -peakRadius; j <= peakRadius; ++j)
                        if (b + j >= 1 && b + j <= H)
                            spectrum[b + j] *= boostFactor;
                }
            }
        }
        const float valleyCutDb = envFloat("MLD5_VALLEY_CUT", 0.0f);
        if (valleyCutDb > 0.1f && harmonicMap) {
            float sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override
                : estimateF0(frameTime.data(), N, sr);
            float pitchBins = componentScale * sourceF0 * N / float(sr);
            if (pitchBins >= 2.0f) {
                float cutFactor = std::pow(10.0f, -valleyCutDb / 20.0f);
                int peakRadius = std::max(1, (int)std::ceil(pitchBins / 3.0f));
                int harmonicCount = std::min(1023, (int)std::ceil(H / pitchBins));
                for (int b = 1; b <= H; ++b) {
                    if (float(b) < pitchBins) continue;
                    float h = float(b) / pitchBins;
                    float nearest = std::fabs(h - std::round(h));
                    if (nearest * pitchBins > peakRadius)
                        spectrum[b] *= cutFactor;
                }
            }
        }
        if (peakSynth && harmonicMap) {
            float sourceF0 = sourceF0Override > 0.0f
                ? sourceF0Override
                : estimateF0(frameTime.data(), N, sr);
            float pitchBins = componentScale * sourceF0 * N / float(sr);
            if (pitchBins >= 2.0f) {
                int harmonicCount = std::min(1023, (int)std::ceil(H / pitchBins));
                for (int b = 0; b <= H; ++b) spectrum[b] = cpx(0.0f, 0.0f);
                for (int harmonic = 1; harmonic <= harmonicCount; ++harmonic) {
                    int b = (int)std::lround(harmonic * pitchBins);
                    if (b < 1 || b > H) continue;
                    float outputPhase = synthesisPhase[b];
                    if (!peaks.empty()) {
                        int nearest = b;
                        for (int pk : peaks)
                            if (std::abs(pk - b) < std::abs(nearest - b)) nearest = pk;
                        outputPhase = synthesisPhase[nearest] + currentPhase[b] - currentPhase[nearest];
                    }
                    spectrum[b] = std::polar(srcMag[b] * masks[b], outputPhase);
                }
            }
        }
        for (int b = 1; b < H; ++b) spectrum[N - b] = std::conj(spectrum[b]);
        spectrum[0] = cpx(spectrum[0].real(), 0.0f);
        spectrum[H] = cpx(spectrum[H].real(), 0.0f);
        fft(spectrum, true);
        for (int i = 0; i < N; ++i) {
            int destination = synthesisPosition + i;
            if (destination < 0 || destination >= stretchedLength) continue;
            stretched[destination] += spectrum[i].real() * window[i];
            norm[destination] += window[i] * window[i];
        }
        previousSynthesisPosition = synthesisPosition;
    }
    for (int i = 0; i < stretchedLength; ++i)
        if (norm[i] > 1e-6f) stretched[i] /= norm[i];

    vector<float> output(len);
    const float latency = float(N / 2) * pitchRatio;
    for (int i = 0; i < len; ++i) {
        float position = latency + i * pitchRatio;
        float value = sincTaps > 0
            ? sincResample(stretched, stretchedLength, position, sincTaps,
                           0.45f * std::min(1.0f, 1.0f / pitchRatio))
            : catmullRom(stretched, stretchedLength, position);
        output[i] = outputGain * value;
    }
    if (postEnvelope) {
        constexpr int postN = 2048, postHop = 256, postH = postN / 2;
        const int postRadius = std::max(2, (int)std::lround(envelopeHz * postN / sr));
        vector<float> corrected(len + postN, 0.0f), correctedNorm(len + postN, 0.0f);
        vector<float> postWindow(postN), sourceMag(postH + 1), outputMag(postH + 1);
        vector<float> sourcePrefix(postH + 2), outputPrefix(postH + 2);
        for (int i = 0; i < postN; ++i)
            postWindow[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (postN - 1));
        for (int position = 0; position < len; position += postHop) {
            vector<cpx> sourceFrame(postN), outputFrame(postN);
            for (int i = 0; i < postN; ++i) {
                int index = position + i - postN / 2;
                sourceFrame[i] = cpx(index >= 0 && index < len ? in[index] * postWindow[i] : 0.0f, 0.0f);
                outputFrame[i] = cpx(index >= 0 && index < len ? output[index] * postWindow[i] : 0.0f, 0.0f);
            }
            fft(sourceFrame, false); fft(outputFrame, false);
            sourcePrefix[0] = outputPrefix[0] = 0.0f;
            for (int b = 0; b <= postH; ++b) {
                sourceMag[b] = std::abs(sourceFrame[b]); outputMag[b] = std::abs(outputFrame[b]);
                sourcePrefix[b + 1] = sourcePrefix[b] + std::log(std::max(1e-20f, sourceMag[b]));
                outputPrefix[b + 1] = outputPrefix[b] + std::log(std::max(1e-20f, outputMag[b]));
            }
            for (int b = 0; b <= postH; ++b) {
                int sourceBin = clampBin(float(b) / std::max(1e-3f, formantFactor), postH);
                int slo = std::max(0, sourceBin - postRadius);
                int shi = std::min(postH + 1, sourceBin + postRadius + 1);
                int olo = std::max(0, b - postRadius);
                int ohi = std::min(postH + 1, b + postRadius + 1);
                float sourceEnvelope = std::exp((sourcePrefix[shi] - sourcePrefix[slo])
                                                / std::max(1, shi - slo));
                float outputEnvelope = std::exp((outputPrefix[ohi] - outputPrefix[olo])
                                                / std::max(1, ohi - olo));
                float gain = std::clamp(sourceEnvelope / std::max(1e-9f, outputEnvelope),
                                        0.1f, 10.0f);
                outputFrame[b] *= gain;
            }
            for (int b = 1; b < postH; ++b) outputFrame[postN - b] = std::conj(outputFrame[b]);
            outputFrame[0] = cpx(outputFrame[0].real(), 0.0f);
            outputFrame[postH] = cpx(outputFrame[postH].real(), 0.0f);
            fft(outputFrame, true);
            for (int i = 0; i < postN; ++i) {
                int index = position + i - postN / 2;
                if (index < 0 || index >= len) continue;
                corrected[index] += outputFrame[i].real() * postWindow[i];
                correctedNorm[index] += postWindow[i] * postWindow[i];
            }
        }
        for (int i = 0; i < len; ++i)
            if (correctedNorm[i] > 1e-6f) output[i] = corrected[i] / correctedNorm[i];
    }
    if (blockNormalisation) {
        constexpr int powerWindow = 1024, powerHop = 256;
        const int points = (len + powerHop - 1) / powerHop + 1;
        vector<float> gains(points, 1.0f);
        float powerFactor = pitchRatio >= 1.0f
            ? std::min(1.15f, 1.0f + 0.14f * std::log2(pitchRatio))
            : std::pow(pitchRatio, 0.52f);
        for (int point = 0; point < points; ++point) {
            int center = point * powerHop;
            int lo = std::max(0, center - powerWindow / 2);
            int hi = std::min(len, center + powerWindow / 2);
            double inputPower = 0.0, outputPower = 0.0;
            for (int i = lo; i < hi; ++i) {
                inputPower += double(in[i]) * in[i];
                outputPower += double(output[i]) * output[i];
            }
            gains[point] = outputPower > 1e-12
                ? std::clamp(powerFactor * (float)std::sqrt(inputPower / outputPower), 0.25f, 4.0f)
                : 1.0f;
        }
        for (int i = 0; i < len; ++i) {
            float position = float(i) / powerHop;
            int left = std::min(points - 1, (int)position);
            int right = std::min(points - 1, left + 1);
            float amount = position - left;
            output[i] *= gains[left] + (gains[right] - gains[left]) * amount;
        }
    }
    return output;
}

// ---------------------------------------------------------------------------
// mld3: TD-PSOLA pitch-synchronous overlap-add with per-epoch F0 tracking.
// Voiced grains are two source periods, period-aligned to the nearest peak,
// resampled to the target period by Catmull-Rom and overlap-added at the
// target-period hop.  Unvoiced frames pass through unchanged.  At
// pitchRatio=1 this reproduces the input (PSOLA identity).
// ---------------------------------------------------------------------------
vector<float> mld3(const vector<float>& in, double sr, float shift_semi, float formant_semi) {
    if (std::fabs(shift_semi) < 1e-6f && std::fabs(formant_semi) < 1e-6f)
        return in;
    int len = (int)in.size();
    vector<float> out(len, 0.0f), norm(len, 0.0f);
    const float pitchRatio = std::pow(2.0f, shift_semi / 12.0f);
    const float highGain = std::pow(10.0f, formant_semi / 20.0f);
    const int anaLen = 2048;
    vector<float> ana(anaLen);

    double outPos = 0.0;
    int guard = 0;
    while (outPos < len - 1 && guard++ < len) {
        int c = (int)outPos;
        // estimate F0 over a window centred at the current position
        for (int i = 0; i < anaLen; ++i) {
            int idx = c - anaLen / 2 + i;
            ana[i] = (idx >= 0 && idx < len) ? in[idx] : 0.0f;
        }
        float f0 = estimateF0(ana.data(), anaLen, sr);
        if (f0 <= 0.0f) {
            // unvoiced: copy a small chunk straight through
            int chunk = 512;
            for (int i = 0; i < chunk; ++i) {
                int d = c + i;
                if (d >= len) break;
                out[d] += in[d];
                norm[d] += 1.0f;
            }
            outPos += chunk;
            continue;
        }
        float Psrc = float(sr / f0);
        float Ptgt = std::max(8.0f, Psrc / pitchRatio);
        // period-align to the nearest peak within half a source period
        int sc = c;
        float best = -1.0f;
        int pLo = std::max(0, c - (int)(Psrc / 2)), pHi = std::min(len - 1, c + (int)(Psrc / 2));
        for (int p = pLo; p <= pHi; ++p) {
            float e = std::fabs(in[p]);
            if (e > best) { best = e; sc = p; }
        }
        int G = std::clamp((int)std::round(2.0f * Psrc), 32, 4096);
        int Gt = std::clamp((int)std::round(2.0f * Ptgt), 16, 4096);
        // Hann-windowed grain centred at the source epoch
        vector<float> grain(G);
        for (int i = 0; i < G; ++i) {
            int idx = sc - G / 2 + i;
            float x = (idx >= 0 && idx < len) ? in[idx] : 0.0f;
            float w = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / G);
            grain[i] = x * w;
        }
        // Catmull-Rom resample to the target period
        vector<float> gout(Gt);
        for (int i = 0; i < Gt; ++i)
            gout[i] = catmullRom(grain, G, float(i) * float(G) / float(Gt));
        // simple high-shelf formant offset
        if (std::fabs(formant_semi) > 1e-4f) {
            float cutoff = std::clamp(f0 * pitchRatio * 1.5f, 200.0f, 6000.0f);
            float alpha = std::cos(2.0f * float(M_PI) * cutoff / float(sr));
            float yn = 0.0f;
            for (int i = 0; i < Gt; ++i) {
                yn = yn + alpha * (gout[i] - yn);
                gout[i] = std::clamp(yn + (gout[i] - yn) * highGain, -0.999f, 0.999f);
            }
        }
        // overlap-add centred at the synthesis epoch
        for (int i = 0; i < Gt; ++i) {
            int d = (int)std::llround(outPos) - Gt / 2 + i;
            if (d < 0 || d >= len) continue;
            float w = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / Gt);
            out[d] += gout[i] * w;
            norm[d] += w * w;
        }
        outPos += Ptgt;
    }
    for (int i = 0; i < len; ++i) out[i] = norm[i] > 1e-6f ? out[i] / norm[i] : 0.0f;
    return out;
}

// ---------------------------------------------------------------------------
// Analysis: clip count + peak + rms + zero-cross + spectral centroid.
// ---------------------------------------------------------------------------
void analyse(const std::string& tag, const vector<float>& s, double sr) {
    double p = 0, e = 0; int clips = 0; float peak = 0;
    vector<float> diffs;
    diffs.reserve(s.size() > 1 ? s.size() - 1 : 0);
    for (float x : s) {
        float a = std::fabs(x);
        if (a > peak) peak = a;
        if (a > 0.99f) ++clips;
        p += double(x) * x; e += std::fabs(x);
    }
    for (size_t i = 1; i < s.size(); ++i)
        diffs.push_back(std::fabs(s[i] - s[i - 1]));
    std::sort(diffs.begin(), diffs.end());
    double rms = std::sqrt(p / s.size());
    // crude spectral flatness on a 8192 FFT of the whole signal
    int N = 8192; vector<cpx> sp(N, 0);
    int take = std::min((int)s.size(), N);
    for (int i = 0; i < take; ++i) sp[i] = cpx(s[i], 0);
    fft(sp, false);
    double gm = 0, am = 0; int H = N/2, nonzero = 0;
    for (int b = 1; b <= H; ++b) {
        float m = std::abs(sp[b]);
        if (m < 1e-12f) continue;
        gm += std::log(m); am += m; ++nonzero;
    }
    float flatness = nonzero > 0 && am > 0
        ? std::exp(gm / nonzero) / (am / nonzero) : 0.0f;
    const auto p99 = diffs.empty() ? 0.0f : diffs[std::min(diffs.size() - 1,
        static_cast<size_t>(diffs.size() * 0.99))];
    const auto p999 = diffs.empty() ? 0.0f : diffs[std::min(diffs.size() - 1,
        static_cast<size_t>(diffs.size() * 0.999))];
    const auto maxDiff = diffs.empty() ? 0.0f : diffs.back();
    vector<float> f0s;
    const int f0Window = 2048, f0Hop = 512;
    for (int i = 0; i + f0Window <= (int)s.size(); i += f0Hop) {
        float f0 = estimateF0(s.data() + i, f0Window, sr);
        if (f0 > 0.0f) f0s.push_back(f0);
    }
    std::sort(f0s.begin(), f0s.end());
    auto percentile = [&](double p) {
        if (f0s.empty()) return 0.0f;
        size_t i = std::min(f0s.size() - 1, (size_t)std::floor((f0s.size() - 1) * p));
        return f0s[i];
    };
    float f0p10 = percentile(0.10), f0med = percentile(0.50), f0p90 = percentile(0.90);
    printf("[%s] samples=%d peak=%.4f rms=%.4f clips=%d diff_p99=%.5f diff_p999=%.5f diff_max=%.5f f0_p10=%.2f f0_med=%.2f f0_p90=%.2f voiced=%zu spec_flat=%.4f\n",
           tag.c_str(), (int)s.size(), peak, rms, clips, p99, p999, maxDiff,
           f0p10, f0med, f0p90, f0s.size(), flatness);
    fflush(stdout);
}

void compareIdentity(const vector<float>& input, const vector<float>& output) {
    size_t n = std::min(input.size(), output.size());
    double errorPower = 0.0, inputPower = 0.0, dot = 0.0, outputPower = 0.0;
    float maxError = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        double e = double(output[i]) - input[i];
        errorPower += e * e;
        inputPower += double(input[i]) * input[i];
        outputPower += double(output[i]) * output[i];
        dot += double(input[i]) * output[i];
        maxError = std::max(maxError, std::fabs((float)e));
    }
    double nrmse = inputPower > 1e-20 ? std::sqrt(errorPower / inputPower) : 0.0;
    double corr = inputPower > 1e-20 && outputPower > 1e-20
        ? dot / std::sqrt(inputPower * outputPower) : 0.0;
    printf("[identity] nrmse=%.8f corr=%.8f max_error=%.8f\n", nrmse, corr, maxError);
}

void compareReferenceScore(const vector<float>& reference, const vector<float>& candidate,
                           double sr) {
    constexpr int envelopeWindow = 480, envelopeHop = 120, maxLagFrames = 4;
    const int refEnvelopeCount = std::max(0, ((int)reference.size() - envelopeWindow) / envelopeHop + 1);
    const int outEnvelopeCount = std::max(0, ((int)candidate.size() - envelopeWindow) / envelopeHop + 1);
    int bestLag = 0;
    double bestCorrelation = -2.0;
    for (int lag = -maxLagFrames; lag <= maxLagFrames; ++lag) {
        double refSum = 0.0, outSum = 0.0;
        int count = 0;
        for (int frame = 0; frame < refEnvelopeCount; ++frame) {
            int other = frame + lag;
            if (other < 0 || other >= outEnvelopeCount) continue;
            double refPower = 0.0, outPower = 0.0;
            for (int i = 0; i < envelopeWindow; ++i) {
                double r = reference[(size_t)frame * envelopeHop + i];
                double o = candidate[(size_t)other * envelopeHop + i];
                refPower += r * r; outPower += o * o;
            }
            refSum += std::sqrt(refPower); outSum += std::sqrt(outPower); ++count;
        }
        if (count < 2) continue;
        refSum /= count; outSum /= count;
        double numerator = 0.0, refVar = 0.0, outVar = 0.0;
        for (int frame = 0; frame < refEnvelopeCount; ++frame) {
            int other = frame + lag;
            if (other < 0 || other >= outEnvelopeCount) continue;
            double refPower = 0.0, outPower = 0.0;
            for (int i = 0; i < envelopeWindow; ++i) {
                double r = reference[(size_t)frame * envelopeHop + i];
                double o = candidate[(size_t)other * envelopeHop + i];
                refPower += r * r; outPower += o * o;
            }
            double r = std::sqrt(refPower) - refSum, o = std::sqrt(outPower) - outSum;
            numerator += r * o; refVar += r * r; outVar += o * o;
        }
        double correlation = numerator / std::sqrt(std::max(1e-12, refVar * outVar));
        if (correlation > bestCorrelation) { bestCorrelation = correlation; bestLag = lag; }
    }
    int sampleLag = bestLag * envelopeHop;
    int refStart = std::max(0, -sampleLag), outStart = std::max(0, sampleLag);
    int alignedLength = std::min((int)reference.size() - refStart,
                                 (int)candidate.size() - outStart);
    vector<float> alignedReference(reference.begin() + refStart,
                                   reference.begin() + refStart + alignedLength);
    vector<float> alignedCandidate(candidate.begin() + outStart,
                                   candidate.begin() + outStart + alignedLength);
    printf("[alignment] lag_samples=%d envelope_corr=%.4f\n", sampleLag, bestCorrelation);
    Spectrogram ref = makeSpectrogram(alignedReference), out = makeSpectrogram(alignedCandidate);
    int width = std::min(ref.width, out.width), height = std::min(ref.height, out.height);
    if (width <= 0 || height <= 0) return;
    int firstBin = std::max(1, (int)std::ceil(50.0 * 2048.0 / sr));
    int lastBin = std::min(height - 1, (int)std::floor(16000.0 * 2048.0 / sr));
    bool dumpBands = std::getenv("MLD5_DUMP_BANDS") != nullptr;
    std::vector<double> bandEdgesVec = {50, 100, 200, 400, 800, 1600, 3200, 6400, 12000};
    if (const char* bandCfg = std::getenv("MLD5_BANDS")) {
        bandEdgesVec.clear();
        const char* p = bandCfg;
        while (*p) {
            bandEdgesVec.push_back(std::strtod(p, nullptr));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    }
    const double* bandEdges = bandEdgesVec.data();
    static const int bandCount = 8;
    double bandError[bandCount] = {0}, bandWeight[bandCount] = {0};
    double bandSigned[bandCount] = {0};
    double spectralError = 0.0, spectralWeight = 0.0;
    double envelopeError = 0.0, envelopeWeight = 0.0;
    int activeFrames = 0;
    for (int x = 0; x < width; ++x) {
        float frameMax = -100.0f;
        for (int b = firstBin; b <= lastBin; ++b)
            frameMax = std::max(frameMax, ref.db[(size_t)b * ref.width + x]);
        if (frameMax < -70.0f) continue;
        ++activeFrames;
        for (int b = firstBin; b <= lastBin; ++b) {
            float rd = ref.db[(size_t)b * ref.width + x];
            float cd = out.db[(size_t)b * out.width + x];
            if (rd < frameMax - 60.0f && cd < frameMax - 60.0f) continue;
            double weight = std::pow(10.0, (std::max(rd, cd) - frameMax) / 20.0)
                / std::sqrt(std::max(80.0, b * sr / 2048.0));
            spectralError += weight * std::min(24.0f, std::fabs(rd - cd));
            spectralWeight += weight;
            if (dumpBands) {
                double fhz = double(b) * sr / 2048.0;
                for (int bi = 0; bi < bandCount; ++bi)
                    if (fhz >= bandEdges[bi] && fhz < bandEdges[bi + 1]) {
                        bandError[bi] += weight * std::min(24.0f, std::fabs(rd - cd));
                        bandWeight[bi] += weight;
                        bandSigned[bi] += weight * (cd - rd);
                        break;
                    }
            }

            int lo = std::max(firstBin, b - 8), hi = std::min(lastBin, b + 8);
            double refPower = 0.0, outPower = 0.0;
            for (int k = lo; k <= hi; ++k) {
                refPower += std::pow(10.0, ref.db[(size_t)k * ref.width + x] / 10.0);
                outPower += std::pow(10.0, out.db[(size_t)k * out.width + x] / 10.0);
            }
            float refEnvelope = 10.0f * std::log10(std::max(1e-10, refPower));
            float outEnvelope = 10.0f * std::log10(std::max(1e-10, outPower));
            double envWeight = std::pow(10.0, (rd - frameMax) / 20.0);
            envelopeError += envWeight * std::min(12.0f, std::fabs(refEnvelope - outEnvelope));
            envelopeWeight += envWeight;
        }
    }
    double specMae = spectralWeight > 0.0 ? spectralError / spectralWeight : 24.0;
    double envMae = envelopeWeight > 0.0 ? envelopeError / envelopeWeight : 12.0;
    double specScore = std::clamp(1.0 - specMae / 15.0, 0.0, 1.0);
    double envScore = std::clamp(1.0 - envMae / 8.0, 0.0, 1.0);
    if (dumpBands) {
        printf("[bands]");
        for (int bi = 0; bi < bandCount; ++bi)
            printf(" %d-%dHz=%.2fdB", (int)bandEdges[bi], (int)bandEdges[bi + 1],
                   bandWeight[bi] > 0.0 ? bandError[bi] / bandWeight[bi] : 0.0);
        printf("\n[bands_signed]");
        for (int bi = 0; bi < bandCount; ++bi)
            printf(" %d-%dHz=%+.2fdB", (int)bandEdges[bi], (int)bandEdges[bi + 1],
                   bandWeight[bi] > 0.0 ? bandSigned[bi] / bandWeight[bi] : 0.0);
        printf("\n");
    }
    if (std::getenv("MLD5_DUMP_SPEC")) {
        printf("[spec]");
        for (int b = firstBin; b <= lastBin; b += 8) {
            double refPower = 0.0, outPower = 0.0;
            int n = 0;
            for (int x = 0; x < width; ++x) {
                float frameMax = -100.0f;
                for (int k = firstBin; k <= lastBin; ++k)
                    frameMax = std::max(frameMax, ref.db[(size_t)k * ref.width + x]);
                if (frameMax < -70.0f) continue;
                ++n;
                refPower += std::pow(10.0, ref.db[(size_t)b * ref.width + x] / 10.0);
                outPower += std::pow(10.0, out.db[(size_t)b * out.width + x] / 10.0);
            }
            if (n > 0) {
                double rf = 10.0 * std::log10(refPower / n), of = 10.0 * std::log10(outPower / n);
                printf(" %d:%.1f/%.1f", (int)(b * sr / 2048.0), rf, of);
            }
        }
        printf("\n");
    }
    auto voicedF0 = [&](const vector<float>& samples) {
        vector<float> values;
        constexpr int window = 2048, hop = 512;
        for (int i = 0; i + window <= (int)samples.size(); i += hop) {
            float f0 = estimateF0(samples.data() + i, window, sr);
            if (f0 > 0.0f) values.push_back(f0);
        }
        std::sort(values.begin(), values.end());
        return values;
    };
    vector<float> refF0 = voicedF0(alignedReference), outF0 = voicedF0(alignedCandidate);
    float refMedian = refF0.empty() ? 0.0f : refF0[refF0.size() / 2];
    float outMedian = outF0.empty() ? 0.0f : outF0[outF0.size() / 2];
    double cents = refMedian > 0.0f && outMedian > 0.0f
        ? std::fabs(1200.0 * std::log2(outMedian / refMedian)) : 1200.0;
    double f0Score = std::clamp(1.0 - cents / 100.0, 0.0, 1.0);
    double voicingScore = std::min(refF0.size(), outF0.size())
        / double(std::max<std::size_t>(1, std::max(refF0.size(), outF0.size())));

    auto rms = [](const vector<float>& samples) {
        double power = 0.0;
        for (float value : samples) power += double(value) * value;
        return std::sqrt(power / std::max<std::size_t>(1, samples.size()));
    };
    double refRms = rms(alignedReference), outRms = rms(alignedCandidate);
    double levelError = std::fabs(20.0 * std::log10(std::max(1e-9, outRms)
                                                    / std::max(1e-9, refRms)));
    double levelScore = std::clamp(1.0 - levelError / 6.0, 0.0, 1.0);
    float peak = 0.0f;
    for (float value : candidate) peak = std::max(peak, std::fabs(value));
    double peakPenalty = 8.0 * std::clamp((peak - 0.99f) / 0.10f, 0.0f, 1.0f);
    double geometric = std::pow(std::max(1e-9, specScore), 0.40)
        * std::pow(std::max(1e-9, envScore), 0.20)
        * std::pow(std::max(1e-9, f0Score), 0.20)
        * std::pow(std::max(1e-9, voicingScore), 0.05)
        * std::pow(std::max(1e-9, levelScore), 0.15);
    double score = std::clamp(100.0 * geometric - peakPenalty, 0.0, 100.0);
    printf("[similarity] score=%.2f active_frames=%d spec=%.3f mae=%.3fdB env=%.3f mae=%.3fdB f0=%.3f cents=%.2f voicing=%.3f level=%.3f delta=%.3fdB peak_penalty=%.2f\n",
           score, activeFrames, specScore, specMae, envScore, envMae, f0Score, cents,
           voicingScore, levelScore, levelError, peakPenalty);
    if (std::getenv("MLD5_DUMP_PEAKS")) {
        vector<std::pair<float, int>> peaks;
        for (int b = firstBin + 1; b < lastBin; ++b) {
            double left = 0.0, centre = 0.0, right = 0.0;
            for (int x = 0; x < width; ++x) {
                left += std::pow(10.0, ref.db[(size_t)(b - 1) * ref.width + x] / 10.0);
                centre += std::pow(10.0, ref.db[(size_t)b * ref.width + x] / 10.0);
                right += std::pow(10.0, ref.db[(size_t)(b + 1) * ref.width + x] / 10.0);
            }
            if (centre >= left && centre > right) peaks.emplace_back((float)centre, b);
        }
        std::sort(peaks.begin(), peaks.end(), [](auto left, auto right) { return left.first > right.first; });
        for (int index = 0; index < std::min<int>(12, peaks.size()); ++index) {
            int b = peaks[index].second;
            double refPower = 0.0, outPower = 0.0;
            for (int x = 0; x < width; ++x) {
                refPower += std::pow(10.0, ref.db[(size_t)b * ref.width + x] / 10.0);
                outPower += std::pow(10.0, out.db[(size_t)b * out.width + x] / 10.0);
            }
            float refDb = 10.0f * std::log10(std::max(1e-10, refPower / width));
            float outDb = 10.0f * std::log10(std::max(1e-10, outPower / width));
            printf("[harmonic] hz=%.1f reference=%.2fdB output=%.2fdB delta=%+.2fdB\n",
                   b * sr / 2048.0, refDb, outDb, outDb - refDb);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 5) {
        printf("use: %s <in.wav> <out.wav> <mld5|mld3> <shift_semi> [formant_semi] [start_s] [dur_s] [reference.wav] [score_dur_s] [score_start_s]\n", argv[0]);
        return 1;
    }
    Wav w;
    if (!readWav(argv[1], w)) { printf("cannot read %s\n", argv[1]); return 1; }
    double sr = w.sr;
    float shift = std::atof(argv[4]);
    float fr = argc > 5 ? std::atof(argv[5]) : 0.0f;
    double start = argc > 6 ? std::atof(argv[6]) : 0.0;
    double dur = argc > 7 ? std::atof(argv[7]) : 4.0;
    // take mono mixdown of requested window
    int s0 = (int)(start * sr), n = (int)(dur * sr);
    if (s0 < 0) s0 = 0;
    if (s0 >= w.frames) { printf("start beyond file\n"); return 1; }
    if (s0 + n > w.frames) n = w.frames - s0;
    vector<float> in(n);
    for (int i = 0; i < n; ++i) {
        double a = 0;
        for (int c = 0; c < w.ch; ++c) a += w.samples[(size_t)(s0 + i) * w.ch + c];
        in[i] = float(a / w.ch);
    }
    analyse("input", in, sr);
    std::string algo = argv[3];
    vector<float> out;
    if (algo == "mld5") out = mld5(in, sr, shift, fr);
    else if (algo == "mld3") out = mld3(in, sr, shift, fr);
    else { printf("unknown algo\n"); return 1; }
    // gentle soft-clip to detect if output is clipping-prone (informational)
    analyse((algo + "_raw").c_str(), out, sr);
    if (std::fabs(shift) < 1e-6f && std::fabs(fr) < 1e-6f)
        compareIdentity(in, out);
    writeSpectrogramComparison(argv[2], in, out, "input-output");
    if (argc > 8) {
        Wav referenceWav;
        if (!readWav(argv[8], referenceWav)) {
            printf("cannot read reference %s\n", argv[8]);
            return 1;
        }
        int referenceStart = std::max(0, (int)std::lround(start * referenceWav.sr));
        int referenceFrames = std::min(n, referenceWav.frames - referenceStart);
        if (referenceFrames <= 0) {
            printf("reference window beyond file\n");
            return 1;
        }
        vector<float> reference(referenceFrames);
        for (int i = 0; i < referenceFrames; ++i) {
            double value = 0.0;
            for (int c = 0; c < referenceWav.ch; ++c)
                value += referenceWav.samples[(size_t)(referenceStart + i) * referenceWav.ch + c];
            reference[i] = float(value / referenceWav.ch);
        }
        analyse("reference", reference, referenceWav.sr);
        vector<float> scoreReference = reference, scoreOutput = out;
        if (argc > 9) {
            int scoreFrames = std::max(2048, (int)std::lround(std::atof(argv[9]) * referenceWav.sr));
            int scoreStart = argc > 10
                ? std::max(0, (int)std::lround(std::atof(argv[10]) * referenceWav.sr)) : 0;
            int scoreEnd = std::min<int>({ (int)scoreReference.size(), (int)scoreOutput.size(),
                                           scoreStart + scoreFrames });
            if (scoreStart >= scoreEnd) {
                printf("score window beyond file\n");
                return 1;
            }
            scoreReference = vector<float>(scoreReference.begin() + scoreStart,
                                           scoreReference.begin() + scoreEnd);
            scoreOutput = vector<float>(scoreOutput.begin() + scoreStart,
                                        scoreOutput.begin() + scoreEnd);
        }
        compareReferenceScore(scoreReference, scoreOutput, referenceWav.sr);
        writeSpectrogramComparison(std::string(argv[2]) + ".reference",
                                   scoreReference, scoreOutput, "reference-output");
    }
    // normalise to -3 dBFS for listening AB without level bias
    float pk = 0; for (float x : out) pk = std::max(pk, std::fabs(x));
    if (pk > 1e-9f) { float g = 0.707f / pk; for (auto& x : out) x *= g; }
    analyse((algo + "_norm").c_str(), out, sr);
    Wav o; o.sr = (int)sr; o.ch = 1; o.frames = (int)out.size(); o.samples = out;
    writeWav(argv[2], o);
    printf("wrote %s\n", argv[2]);
    return 0;
}
