#include "SosBiquad.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace fxsound {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinEqFreqHz = 5.0f;     // 0x1402ad858
constexpr float kMaxEqFreqHz = 20000.0f; // 0x1402ac7b4
} // namespace

void BiquadSection::designPeakingEq(float sampleRateHz, float centerFreqHz, float boostDb, float baseQ) noexcept {
    if (std::fabs(boostDb) < 0.01f || sampleRateHz <= 0.0f) {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
        bypass = true;
        return;
    }

    const float fs = std::max(sampleRateHz, 8000.0f);
    const float f0 = std::clamp(centerFreqHz, kMinEqFreqHz, std::min(kMaxEqFreqHz, 0.47f * fs));
    float q = baseQ;

    // Disassembled from FiltbiqdSos @ 0x1400ddd2c:
    // If f0 < 60.0 Hz, clamp Q <= 1.0 + ((f0 - 20.0) * 19.0) / 40.0
    if (f0 < 60.0f) {
        const float maxLowFreqQ = std::max(0.5f, 1.0f + ((f0 - 20.0f) * 19.0f) / 40.0f);
        if (q > maxLowFreqQ) {
            q = maxLowFreqQ;
        }
    }

    // Disassembled from FiltbiqdSos @ 0x1400ddd9f:
    // If |boostDb| < 6.0 dB, clamp Q <= 0.2 + (|boostDb| * 19.8) / 6.0
    const float absBoost = std::fabs(boostDb);
    if (absBoost < 6.0f) {
        const float maxLowBoostQ = 0.2f + (absBoost * 19.8f) / 6.0f;
        if (q > maxLowBoostQ) {
            q = maxLowBoostQ;
        }
    }

    // Orfanidis / RBJ bilinear peaking biquad matched to FiltbiqdSos
    const float A = std::pow(10.0f, boostDb / 40.0f); // sqrt(10^(dB/20))
    const float w0 = 2.0f * kPi * f0 / fs;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * std::max(q, 0.1f));

    const float a0 = 1.0f + alpha / A;
    const float invA0 = 1.0f / a0;

    b0 = (1.0f + alpha * A) * invA0;
    b1 = (-2.0f * cosW0) * invA0;
    b2 = (1.0f - alpha * A) * invA0;
    a1 = b1; // Note: in peaking EQ, normalized a1 == b1
    a2 = (1.0f - alpha / A) * invA0;
    bypass = false;
}

void BiquadSection::designLowShelf(float sampleRateHz, float cornerFreqHz, float gainDb, float slopeQ) noexcept {
    if (std::fabs(gainDb) < 0.01f || sampleRateHz <= 0.0f) {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
        bypass = true;
        return;
    }

    const float fs = std::max(sampleRateHz, 8000.0f);
    const float f0 = std::clamp(cornerFreqHz, 15.0f, 0.45f * fs);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * f0 / fs;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * slopeQ);
    const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

    const float a0 = (A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAAlpha;
    const float invA0 = 1.0f / a0;

    b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAAlpha)) * invA0;
    b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW0)) * invA0;
    b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAAlpha)) * invA0;
    a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosW0)) * invA0;
    a2 = ((A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAAlpha) * invA0;
    bypass = false;
}

void BiquadSection::designHighShelf(float sampleRateHz, float cornerFreqHz, float gainDb, float slopeQ) noexcept {
    if (std::fabs(gainDb) < 0.01f || sampleRateHz <= 0.0f) {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
        bypass = true;
        return;
    }

    const float fs = std::max(sampleRateHz, 8000.0f);
    const float f0 = std::clamp(cornerFreqHz, 200.0f, 0.46f * fs);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * f0 / fs;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * slopeQ);
    const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

    const float a0 = (A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAAlpha;
    const float invA0 = 1.0f / a0;

    b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAAlpha)) * invA0;
    b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0)) * invA0;
    b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAAlpha)) * invA0;
    a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0)) * invA0;
    a2 = ((A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAAlpha) * invA0;
    bypass = false;
}

void BiquadSection::designBandPass(float sampleRateHz, float centerFreqHz, float q) noexcept {
    const float fs = std::max(sampleRateHz, 8000.0f);
    const float f0 = std::clamp(centerFreqHz, 15.0f, 0.46f * fs);
    const float w0 = 2.0f * kPi * f0 / fs;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * std::max(q, 0.2f));

    const float a0 = 1.0f + alpha;
    const float invA0 = 1.0f / a0;

    b0 = alpha * invA0;
    b1 = 0.0f;
    b2 = -alpha * invA0;
    a1 = (-2.0f * cosW0) * invA0;
    a2 = (1.0f - alpha) * invA0;
    bypass = false;
}

float BiquadSection::evaluateMagnitudeDb(float sampleRateHz, float freqHz) const noexcept {
    if (bypass || sampleRateHz <= 0.0f) {
        return 0.0f;
    }
    const float w = 2.0f * kPi * std::clamp(freqHz, 10.0f, 0.49f * sampleRateHz) / sampleRateHz;
    const std::complex<float> z1 = std::polar(1.0f, -w);
    const std::complex<float> z2 = std::polar(1.0f, -2.0f * w);
    const std::complex<float> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<float> den = 1.0f + a1 * z1 + a2 * z2;
    const float mag = std::abs(num) / std::max(std::abs(den), 1e-9f);
    return 20.0f * std::log10(std::max(mag, 1e-6f));
}

SosEqualizer::SosEqualizer() {
    constexpr std::array<float, kNumBands> kDefaultFreqs = {
        62.5f, 115.0f, 250.0f, 450.0f, 630.0f, 1250.0f, 2700.0f, 5300.0f, 7500.0f, 13000.0f
    };
    centerFreqsHz_ = kDefaultFreqs;
    boostsDb_.fill(0.0f);
    for (size_t i = 0; i < kNumBands; ++i) {
        recomputeBand(i);
    }
}

void SosEqualizer::setSampleRate(float sampleRateHz) noexcept {
    if (std::fabs(sampleRateHz_ - sampleRateHz) > 1.0f && sampleRateHz >= 8000.0f) {
        sampleRateHz_ = sampleRateHz;
        for (size_t i = 0; i < kNumBands; ++i) {
            recomputeBand(i);
        }
        resetStates();
    }
}

void SosEqualizer::setBandParameters(size_t bandIdx, float centerFreqHz, float boostDb) noexcept {
    if (bandIdx >= kNumBands) {
        return;
    }
    const float clampedFreq = std::clamp(centerFreqHz, kMinEqFreqHz, kMaxEqFreqHz);
    const float clampedBoost = std::clamp(boostDb, -15.0f, 15.0f);
    if (std::fabs(centerFreqsHz_[bandIdx] - clampedFreq) > 0.05f ||
        std::fabs(boostsDb_[bandIdx] - clampedBoost) > 0.01f) {
        centerFreqsHz_[bandIdx] = clampedFreq;
        boostsDb_[bandIdx] = clampedBoost;
        recomputeBand(bandIdx);
    }
}

void SosEqualizer::recomputeBand(size_t bandIdx) noexcept {
    // Disassembled from DfxDspEq.cpp: constant-Q ~1.414 (1 octave bandwidth) across 10 bands
    sections_[bandIdx].designPeakingEq(sampleRateHz_, centerFreqsHz_[bandIdx], boostsDb_[bandIdx], 1.45f);
}

void SosEqualizer::resetStates() noexcept {
    for (auto& s : sections_) {
        s.resetState();
    }
}

void SosEqualizer::processStereoFrame(float& left, float& right) noexcept {
    if (!enabled_) {
        return;
    }
    for (auto& section : sections_) {
        section.processStereo(left, right);
    }
}

void SosEqualizer::processInterleavedBuffer(float* interleavedStereo, size_t numFrames) noexcept {
    if (!enabled_ || !interleavedStereo) {
        return;
    }
    for (size_t i = 0; i < numFrames; ++i) {
        float L = interleavedStereo[2 * i];
        float R = interleavedStereo[2 * i + 1];
        for (auto& section : sections_) {
            section.processStereo(L, R);
        }
        interleavedStereo[2 * i] = L;
        interleavedStereo[2 * i + 1] = R;
    }
}

float SosEqualizer::evaluateResponseDb(float freqHz) const noexcept {
    if (!enabled_) {
        return 0.0f;
    }
    const float w = 2.0f * kPi * std::clamp(freqHz, 10.0f, 0.49f * sampleRateHz_) / sampleRateHz_;
    const std::complex<float> z1 = std::polar(1.0f, -w);
    const std::complex<float> z2 = std::polar(1.0f, -2.0f * w);

    float totalDb = 0.0f;
    for (const auto& sec : sections_) {
        if (sec.bypass) continue;
        const std::complex<float> num = sec.b0 + sec.b1 * z1 + sec.b2 * z2;
        const std::complex<float> den = 1.0f + sec.a1 * z1 + sec.a2 * z2;
        const float mag = std::abs(num) / std::max(std::abs(den), 1e-9f);
        totalDb += 20.0f * std::log10(std::max(mag, 1e-6f));
    }
    return totalDb;
}

} // namespace fxsound
