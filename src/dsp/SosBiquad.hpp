#pragma once

#include <array>
#include <cstddef>

namespace fxsound {

// Reversed from FiltbiqdSos (0x1400ddce0) and SosProcess (0x1400e5b79) in FxSound.exe.
// Each section uses Direct Form II Transposed:
//   y  = b0 * x + w1
//   w1 = (x - y) * b1_shared + w2   (for symmetric peaking biquad where b1 == -a1)
//   w2 = b2 * x - a2 * y
struct BiquadSection {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f; // normalized by a0 (standard convention: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2])
    float a2 = 0.0f;

    // Stereo delay states (Left = 0, Right = 1)
    float w1L = 0.0f;
    float w2L = 0.0f;
    float w1R = 0.0f;
    float w2R = 0.0f;

    bool bypass = true;

    void resetState() noexcept {
        w1L = w2L = w1R = w2R = 0.0f;
    }

    // Design peaking EQ section matching FiltbiqdSos (0x1400ddce0) Q-clamping & bilinear warping
    void designPeakingEq(float sampleRateHz, float centerFreqHz, float boostDb, float baseQ = 1.41421356f) noexcept;

    // Design low-shelf or high-shelf biquad for max0 (Bass) and aural0 (Clarity)
    void designLowShelf(float sampleRateHz, float cornerFreqHz, float gainDb, float slopeQ = 0.70710678f) noexcept;
    void designHighShelf(float sampleRateHz, float cornerFreqHz, float gainDb, float slopeQ = 0.70710678f) noexcept;
    void designBandPass(float sampleRateHz, float centerFreqHz, float q) noexcept;

    // Evaluate magnitude response in dB at freqHz
    float evaluateMagnitudeDb(float sampleRateHz, float freqHz) const noexcept;

    inline float processLeft(float x) noexcept {
        if (bypass) return x;
        const float y = b0 * x + w1L;
        w1L = b1 * x - a1 * y + w2L;
        w2L = b2 * x - a2 * y;
        return y;
    }

    inline float processRight(float x) noexcept {
        if (bypass) return x;
        const float y = b0 * x + w1R;
        w1R = b1 * x - a1 * y + w2R;
        w2R = b2 * x - a2 * y;
        return y;
    }

    inline void processStereo(float& left, float& right) noexcept {
        if (bypass) return;
        const float yL = b0 * left + w1L;
        w1L = b1 * left - a1 * yL + w2L;
        w2L = b2 * left - a2 * yL;
        left = yL;

        const float yR = b0 * right + w1R;
        w1R = b1 * right - a1 * yR + w2R;
        w2R = b2 * right - a2 * yR;
        right = yR;
    }
};

// 10-band Second-Order Section Graphic/Parametric Equalizer (Band 1..10)
class SosEqualizer {
public:
    static constexpr size_t kNumBands = 10;

    SosEqualizer();

    void setSampleRate(float sampleRateHz) noexcept;
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isEnabled() const noexcept { return enabled_; }

    void setBandParameters(size_t bandIdx, float centerFreqHz, float boostDb) noexcept;
    void resetStates() noexcept;

    void processStereoFrame(float& left, float& right) noexcept;
    void processInterleavedBuffer(float* interleavedStereo, size_t numFrames) noexcept;

    // Computes total composite EQ magnitude response in dB at an arbitrary frequency fHz
    float evaluateResponseDb(float freqHz) const noexcept;

private:
    void recomputeBand(size_t bandIdx) noexcept;

    float sampleRateHz_ = 48000.0f;
    bool enabled_ = true;
    std::array<float, kNumBands> centerFreqsHz_;
    std::array<float, kNumBands> boostsDb_;
    std::array<BiquadSection, kNumBands> sections_;
};

} // namespace fxsound
