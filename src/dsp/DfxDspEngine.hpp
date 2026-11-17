#pragma once

#include "FacPreset.hpp"
#include "DfxDsp.h"
#include "SosBiquad.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace fxsound {

// Master normalized volume level modes
enum class LoudnessMode : int {
    Quiet = 0,      // -2.85 dB (0.72x) comfortable quiet listening level
    Normal = 1,     //  0.00 dB (1.00x) unity loudness matched across all presets
    HighNormal = 2  // +3.50 dB (1.50x) elevated presence level matched across all presets
};

class DfxDspEngine {
public:
    static constexpr size_t kSpectrumBandCount = 20;

    DfxDspEngine();

    void setSampleRate(float sampleRateHz);
    float sampleRate() const noexcept { return sampleRateHz_.load(std::memory_order_relaxed); }

    void setPowerEnabled(bool enabled) noexcept { powerEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isPowerEnabled() const noexcept { return powerEnabled_.load(std::memory_order_relaxed); }

    void setLoudnessMode(LoudnessMode mode) noexcept {
        loudnessMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
    }
    LoudnessMode loudnessMode() const noexcept {
        return static_cast<LoudnessMode>(loudnessMode_.load(std::memory_order_relaxed));
    }

    void setLoudnessNormalizationEnabled(bool enabled) noexcept {
        loudnessNormalized_.store(enabled, std::memory_order_relaxed);
    }
    bool isLoudnessNormalizationEnabled() const noexcept {
        return loudnessNormalized_.load(std::memory_order_relaxed);
    }

    static float loudnessModeTargetScale(LoudnessMode mode) noexcept;

    // Zero-CPU sleep mode for visualizer when window is minimized/hidden or eye icon toggled off
    void setVisualizerActive(bool active) noexcept { visualizerActive_.store(active, std::memory_order_relaxed); }
    bool isVisualizerActive() const noexcept { return visualizerActive_.load(std::memory_order_relaxed); }

    // Apply complete FacPreset parameters thread-safely
    void applyPreset(const FacPreset& preset);

    // Real-time stereo float32 processing entry point (matches dfxpModifyRealtypeSamples @ 0x1400cefa0)
    void processStereoInterleaved(float* interleavedStereo, size_t numFrames) noexcept;

    // Retrieve current 20-band normalized spectrum levels [0.0f .. 1.0f] for FxVisualizer
    std::array<float, kSpectrumBandCount> getSpectrumBands() const noexcept;

    // Evaluate composite EQ + Clarity/Bass shelf frequency response in dB for testing/inspection
    float evaluateEqResponseDb(float freqHz) const noexcept;

    void resetAllStates();

private:
    void rebuildModulationFiltersLocked();
    void updateSpectrumFromBlock(const float* interleavedStereo, size_t numFrames) noexcept;

    std::atomic<float> sampleRateHz_{48000.0f};
    std::atomic<bool> powerEnabled_{true};
    std::atomic<bool> visualizerActive_{true};
    std::atomic<int> loudnessMode_{static_cast<int>(LoudnessMode::Normal)};
    std::atomic<bool> loudnessNormalized_{true};

    mutable std::mutex paramMutex_;
    ::DfxDsp original_;

    // Normalized [0.0f, 1.0f] effect amounts from .fac Main[0..5]
    float clarityAmount_ = 0.39f;       // Main 0 (aural0)
    float ambienceAmount_ = 0.0f;       // Main 3 (lex0)
    float surroundAmount_ = 0.16f;      // Main 1 (wid0 + BinauralSyn)
    float dynamicBoostAmount_ = 0.47f;  // Main 4 (apt0 + cmp0 + ply0)
    float bassBoostAmount_ = 0.47f;     // Main 5 (max0)

    // Linear, non-pumping preset loudness normalization state
    float presetStaticTrim_ = 1.0f;     // Analytical perceptual loudness trim for current preset
    float smoothedMasterGain_ = 1.0f;   // Per-sample smoothed gain (eliminates block zipper noise)
    float normInputPower_ = 0.02f;      // Slow integrated input RMS power
    float normOutputPower_ = 0.02f;     // Slow integrated pre-trim output RMS power

    // 10-band SOS Equalizer
    SosEqualizer eq_;

    // Stage 2: Clarity (aural0) filters
    BiquadSection clarityHighShelf_;
    BiquadSection clarityExciterBand_;

    // Stage 3: Surround Sound (wid0 + BinauralSyn) Side high-shelf + phase-safe all-pass decorrelator
    BiquadSection surroundSideShelf_;

    // Stage 4: Ambience (lex0 R1s44r20..R1s48r00) stereo comb + allpass reverb network + band-limiting
    BiquadSection reverbInputBandPass_;
    struct CombFilter {
        std::vector<float> buffer;
        size_t pos = 0;
        float filterStore = 0.0f;
        float feedback = 0.72f;
        float damp1 = 0.28f;
        float damp2 = 0.72f;

        void init(size_t size) {
            buffer.assign(std::max<size_t>(size, 16), 0.0f);
            pos = 0;
            filterStore = 0.0f;
        }
        void clear() noexcept {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            filterStore = 0.0f;
            pos = 0;
        }
        inline float process(float input) noexcept {
            const float output = buffer[pos];
            filterStore = (output * damp2) + (filterStore * damp1);
            buffer[pos] = input + (filterStore * feedback);
            if (++pos >= buffer.size()) pos = 0;
            return output;
        }
    };

    struct AllPassFilter {
        std::vector<float> buffer;
        size_t pos = 0;
        float feedback = 0.5f;

        void init(size_t size) {
            buffer.assign(std::max<size_t>(size, 16), 0.0f);
            pos = 0;
        }
        void clear() noexcept {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            pos = 0;
        }
        inline float process(float input) noexcept {
            const float bufOut = buffer[pos];
            const float output = -input + bufOut;
            buffer[pos] = input + (bufOut * feedback);
            if (++pos >= buffer.size()) pos = 0;
            return output;
        }
    };

    std::array<CombFilter, 4> combsL_;
    std::array<CombFilter, 4> combsR_;
    std::array<AllPassFilter, 2> allpassL_;
    std::array<AllPassFilter, 2> allpassR_;
    AllPassFilter surroundAllPass_;

    // Stage 5: Bass Boost (max0) Butterworth low-shelf + sub-bass warmth
    BiquadSection bassLowShelf_;
    BiquadSection bassPunchPeak_;

    // Stage 6: Dynamic Boost (apt0 + cmp0 + ply0) slow studio RMS leveling
    float dynEnvRms_ = 0.15f;
    float dynGainSmooth_ = 1.0f;

    // Stage 7: 20-band real-time logarithmic Spectrum Analyzer (spectrumProcess)
    std::array<BiquadSection, kSpectrumBandCount> spectrumFilters_;
    std::array<std::atomic<float>, kSpectrumBandCount> spectrumLevels_;
};

} // namespace fxsound
