#include "DfxDspEngine.hpp"

#include <algorithm>
#include <cmath>

namespace fxsound {

namespace {

// 20 logarithmically spaced visualizer center frequencies (31.5 Hz .. 16 kHz)
constexpr std::array<float, DfxDspEngine::kSpectrumBandCount> kSpectrumCenterFreqs = {
    31.5f,   45.0f,   63.0f,   90.0f,   125.0f,
    180.0f,  250.0f,  355.0f,  500.0f,  710.0f,
    1000.0f, 1400.0f, 2000.0f, 2800.0f, 4000.0f,
    5600.0f, 8000.0f, 10500.0f, 13000.0f, 16000.0f
};

// Perceptual music-spectrum weights (pink-noise / modern mastered music distribution)
// Used to compute a linear, non-pumping cross-preset loudness trim
constexpr std::array<float, DfxDspEngine::kSpectrumBandCount> kMusicBandWeights = {
    0.03f, 0.05f, 0.08f, 0.10f, 0.10f,
    0.09f, 0.09f, 0.08f, 0.08f, 0.07f,
    0.06f, 0.05f, 0.04f, 0.03f, 0.02f,
    0.01f, 0.01f, 0.005f, 0.003f, 0.002f
};

// Base Lexicon/Schroeder room reflection tap lengths at 44100 Hz (R1s44r20..R1s48r00)
constexpr std::array<size_t, 4> kCombTapsBaseL = {1116, 1188, 1277, 1356};
constexpr std::array<size_t, 4> kCombTapsBaseR = {1139, 1211, 1300, 1379};
constexpr std::array<size_t, 2> kAllPassTapsBaseL = {556, 441};
constexpr std::array<size_t, 2> kAllPassTapsBaseR = {579, 464};

} // namespace

float DfxDspEngine::loudnessModeTargetScale(LoudnessMode mode) noexcept {
    switch (mode) {
        case LoudnessMode::Quiet:      return 0.72f; // -2.85 dB comfortable quiet listening level
        case LoudnessMode::Normal:     return 1.00f; //  0.00 dB unity normalized level (matches bypass loudness)
        case LoudnessMode::HighNormal: return 1.85f; // +5.35 dB strong high-normal presence level
        default:                       return 1.00f;
    }
}

DfxDspEngine::DfxDspEngine() {
    for (auto& lvl : spectrumLevels_) {
        lvl.store(0.0f, std::memory_order_relaxed);
    }
    setSampleRate(48000.0f);
    FacPreset defaultPreset;
    applyPreset(defaultPreset);
}

void DfxDspEngine::setSampleRate(float sampleRateHz) {
    const float clampedFs = std::clamp(sampleRateHz, 8000.0f, 192000.0f);
    sampleRateHz_.store(clampedFs, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(paramMutex_);
    eq_.setSampleRate(clampedFs);
    original_.setSignalFormat(32, 2, static_cast<int>(clampedFs), 32);

    const float scale = clampedFs / 44100.0f;
    for (size_t i = 0; i < 4; ++i) {
        combsL_[i].init(static_cast<size_t>(std::lround(kCombTapsBaseL[i] * scale)));
        combsR_[i].init(static_cast<size_t>(std::lround(kCombTapsBaseR[i] * scale)));
    }
    for (size_t i = 0; i < 2; ++i) {
        allpassL_[i].init(static_cast<size_t>(std::lround(kAllPassTapsBaseL[i] * scale)));
        allpassR_[i].init(static_cast<size_t>(std::lround(kAllPassTapsBaseR[i] * scale)));
    }

    // Phase-coherent all-pass diffuser for Surround Sound (~1.4 ms tap, zero comb filtering)
    surroundAllPass_.init(static_cast<size_t>(std::lround(64.0f * scale)));
    surroundAllPass_.feedback = 0.5f;

    // Spectrum analyzer bandpass filters
    for (size_t i = 0; i < kSpectrumBandCount; ++i) {
        spectrumFilters_[i].designBandPass(clampedFs, kSpectrumCenterFreqs[i], 2.6f);
    }

    rebuildModulationFiltersLocked();
}

void DfxDspEngine::applyPreset(const FacPreset& preset) {
    std::lock_guard<std::mutex> lock(paramMutex_);

    clarityAmount_      = preset.getEffectNormalized(EffectIndex::Clarity);
    ambienceAmount_     = preset.getEffectNormalized(EffectIndex::Ambience);
    surroundAmount_     = preset.getEffectNormalized(EffectIndex::SurroundSound);
    dynamicBoostAmount_ = preset.getEffectNormalized(EffectIndex::DynamicBoost);
    bassBoostAmount_    = preset.getEffectNormalized(EffectIndex::BassBoost);

    original_.setNumBands(10);
    original_.eqOn(preset.eqEnabled);
    original_.powerOn(true);
    const float effects[] = {clarityAmount_, ambienceAmount_, surroundAmount_, dynamicBoostAmount_, bassBoostAmount_};
    for (int e = 0; e < 5; ++e)
        original_.setEffectValue(static_cast<::DfxDsp::Effect>(e), effects[e] * 10.0f);
    for (int b = 0; b < 10; ++b) {
        original_.setEqBandFrequency(b, preset.bands[b].centerFreqHz);
        original_.setEqBandBoostCut(b, preset.bands[b].boostDb);
    }
    eq_.setEnabled(preset.eqEnabled);
    for (size_t b = 0; b < SosEqualizer::kNumBands; ++b) {
        eq_.setBandParameters(b, preset.bands[b].centerFreqHz, preset.bands[b].boostDb);
    }

    rebuildModulationFiltersLocked();
}

void DfxDspEngine::rebuildModulationFiltersLocked() {
    const float fs = sampleRateHz_.load(std::memory_order_relaxed);

    // 1. Clarity (aural0): clean studio high-shelf presence lift + upper-air exciter band
    const float clarityShelfDb = clarityAmount_ * 5.5f;
    clarityHighShelf_.designHighShelf(fs, 4200.0f, clarityShelfDb, 0.707f);
    clarityExciterBand_.designBandPass(fs, 5600.0f, 1.1f);

    // 2. Surround Sound (wid0 + BinauralSyn): Mid/Side high-shelf keeping sub-bass anchored in mono
    const float sideShelfDb = surroundAmount_ * 5.0f;
    surroundSideShelf_.designHighShelf(fs, 220.0f, sideShelfDb, 0.707f);

    // 3. Ambience (lex0): band-limited room reverb input (prevents sub-bass rumble in reverb tank)
    reverbInputBandPass_.designBandPass(fs, 900.0f, 0.55f);
    const float roomFeedback = 0.70f + 0.18f * ambienceAmount_;
    for (size_t i = 0; i < 4; ++i) {
        combsL_[i].feedback = roomFeedback;
        combsR_[i].feedback = roomFeedback;
        combsL_[i].damp1 = 0.28f;
        combsL_[i].damp2 = 0.72f;
        combsR_[i].damp1 = 0.28f;
        combsR_[i].damp2 = 0.72f;
    }

    // 4. Bass Boost (max0): smooth Butterworth low-shelf + controlled 62.5 Hz punch
    const float bassShelfDb = bassBoostAmount_ * 6.0f;
    const float bassPunchDb = bassBoostAmount_ * 2.5f;
    bassLowShelf_.designLowShelf(fs, 75.0f, bassShelfDb, 0.707f);
    bassPunchPeak_.designPeakingEq(fs, 62.5f, bassPunchDb, 1.15f);

    // 5. Analytical Linear Preset Loudness Compensation (eliminates fast RMS pumping & block zipper noise)
    float weightedPowerGain = 0.0f;
    for (size_t i = 0; i < kSpectrumCenterFreqs.size(); ++i) {
        const float freq = kSpectrumCenterFreqs[i];
        float totalDb = eq_.evaluateResponseDb(freq)
                      + bassLowShelf_.evaluateMagnitudeDb(fs, freq)
                      + bassPunchPeak_.evaluateMagnitudeDb(fs, freq)
                      + clarityHighShelf_.evaluateMagnitudeDb(fs, freq);
        const float lin = std::pow(10.0f, totalDb / 20.0f);
        weightedPowerGain += kMusicBandWeights[i] * (lin * lin);
    }

    // Account for Surround Side expansion, Ambience wet tail energy, and Dynamic Boost gain
    weightedPowerGain *= (1.0f + 0.18f * surroundAmount_ + 0.14f * ambienceAmount_ + 0.28f * dynamicBoostAmount_);
    presetStaticTrim_ = std::clamp(1.0f / std::sqrt(std::max(weightedPowerGain, 0.15f)), 0.35f, 1.85f);

    // Initialize output power tracker relative to input power so preset switches start at the analytical trim
    normOutputPower_ = std::max(normInputPower_ / std::max(presetStaticTrim_ * presetStaticTrim_, 0.1f), 1e-6f);
}

void DfxDspEngine::resetAllStates() {
    std::lock_guard<std::mutex> lock(paramMutex_);
    eq_.resetStates();
    clarityHighShelf_.resetState();
    clarityExciterBand_.resetState();
    surroundSideShelf_.resetState();
    surroundAllPass_.clear();
    reverbInputBandPass_.resetState();
    for (auto& c : combsL_) c.clear();
    for (auto& c : combsR_) c.clear();
    for (auto& a : allpassL_) a.clear();
    for (auto& a : allpassR_) a.clear();
    bassLowShelf_.resetState();
    bassPunchPeak_.resetState();
    dynEnvRms_ = 0.18f;
    dynGainSmooth_ = 1.0f;
    normInputPower_ = 0.025f;
    normOutputPower_ = std::max(normInputPower_ / std::max(presetStaticTrim_ * presetStaticTrim_, 0.1f), 1e-6f);
    const bool normalizeLoudness = loudnessNormalized_.load(std::memory_order_relaxed);
    const LoudnessMode mode = static_cast<LoudnessMode>(loudnessMode_.load(std::memory_order_relaxed));
    smoothedMasterGain_ = (normalizeLoudness ? presetStaticTrim_ : 1.0f) * loudnessModeTargetScale(mode);
}

void DfxDspEngine::processStereoInterleaved(float* interleavedStereo, size_t numFrames) noexcept {
    if (!interleavedStereo || numFrames == 0) {
        return;
    }

    // When FxSound Power is OFF, audio passes through untouched with zero DSP overhead
    if (!powerEnabled_.load(std::memory_order_relaxed)) {
        if (visualizerActive_.load(std::memory_order_relaxed)) {
            for (auto& lvl : spectrumLevels_) {
                lvl.store(lvl.load(std::memory_order_relaxed) * 0.82f, std::memory_order_relaxed);
            }
        }
        return;
    }

    std::lock_guard<std::mutex> lock(paramMutex_);

    const float clarity = clarityAmount_;
    const float ambience = ambienceAmount_;
    const float surround = surroundAmount_;
    const float dynBoost = dynamicBoostAmount_;
    const float bassBoost = bassBoostAmount_;

    const bool normalizeLoudness = loudnessNormalized_.load(std::memory_order_relaxed);
    const LoudnessMode mode = static_cast<LoudnessMode>(loudnessMode_.load(std::memory_order_relaxed));
    const float modeScale = loudnessModeTargetScale(mode);

    // High-headroom soft-knee ceiling limiter (only engages above 0.95 full-scale to prevent digital overs)
    auto softKneeCeiling = [](float x) noexcept -> float {
        const float ax = std::fabs(x);
        if (ax <= 0.95f) {
            return x;
        }
        const float sign = (x >= 0.0f) ? 1.0f : -1.0f;
        const float compressed = 0.95f + 0.05f * std::tanh((ax - 0.95f) / 0.05f);
        return std::clamp(sign * compressed, -1.0f, 1.0f);
    };

    std::vector<float> dry(interleavedStereo, interleavedStereo + numFrames * 2);
    original_.processAudio(reinterpret_cast<short*>(interleavedStereo), reinterpret_cast<short*>(interleavedStereo), static_cast<int>(numFrames), 0);
    for (size_t i = 0; i < numFrames; ++i) {
        float L = interleavedStereo[2*i];
        float R = interleavedStereo[2*i+1];
        const float inPowerSample = 0.5f * (dry[2*i]*dry[2*i] + dry[2*i+1]*dry[2*i+1]);
        // Continuous per-sample Loudness Trim + Quiet/Normal/HighNormal Volume Mode (zero block zipper noise)
        float targetMasterGain = modeScale;
        if (normalizeLoudness) {
            const float outPowerSample = 0.5f * (L * L + R * R);
            if (inPowerSample > 1e-6f && outPowerSample > 1e-7f) {
                normInputPower_  = 0.9992f * normInputPower_  + 0.0008f * inPowerSample;
                normOutputPower_ = 0.9992f * normOutputPower_ + 0.0008f * outPowerSample;
            }
            const float rmsTrim = std::sqrt(normInputPower_ / std::max(normOutputPower_, 1e-7f));
            // Bound adaptive trim smoothly around analytical presetStaticTrim_ so drum hits never pump
            const float boundedTrim = std::clamp(
                rmsTrim,
                0.55f * presetStaticTrim_,
                1.45f * presetStaticTrim_
            );
            targetMasterGain = boundedTrim * modeScale;
        }

        smoothedMasterGain_ += 0.0025f * (targetMasterGain - smoothedMasterGain_);
        L = softKneeCeiling(L * smoothedMasterGain_);
        R = softKneeCeiling(R * smoothedMasterGain_);

        interleavedStereo[2 * i]     = L;
        interleavedStereo[2 * i + 1] = R;
    }

    // Stage 7: Real-Time 20-Band Spectrum Visualizer (spectrumProcess @ 0x1400cf140)
    if (visualizerActive_.load(std::memory_order_relaxed)) {
        updateSpectrumFromBlock(interleavedStereo, numFrames);
    }
}

void DfxDspEngine::updateSpectrumFromBlock(const float* interleavedStereo, size_t numFrames) noexcept {
    std::array<float, kSpectrumBandCount> bandPeaks{};
    bandPeaks.fill(0.0f);

    const size_t step = 1; // Filters must receive every sample at their design rate.
    for (size_t i = 0; i < numFrames; i += step) {
        const float mono = 0.5f * (interleavedStereo[2 * i] + interleavedStereo[2 * i + 1]);
        for (size_t b = 0; b < kSpectrumBandCount; ++b) {
            const float filtered = spectrumFilters_[b].processLeft(mono);
            bandPeaks[b] += filtered * filtered;
        }
    }

    for (size_t b = 0; b < kSpectrumBandCount; ++b) {
        const float rms = std::sqrt(bandPeaks[b] / std::max(size_t(1), numFrames));
        const float db = 20.0f * std::log10(std::max(rms, 1e-6f));
        const float target = std::clamp((db + 65.0f) / 60.0f, 0.0f, 1.0f);
        const float prev = spectrumLevels_[b].load(std::memory_order_relaxed);
        const float smoothed = (target > prev)
                                   ? (0.45f * prev + 0.55f * target)
                                   : (0.82f * prev + 0.18f * target);
        spectrumLevels_[b].store(smoothed, std::memory_order_relaxed);
    }
}

std::array<float, DfxDspEngine::kSpectrumBandCount> DfxDspEngine::getSpectrumBands() const noexcept {
    std::array<float, kSpectrumBandCount> out{};
    for (size_t i = 0; i < kSpectrumBandCount; ++i) {
        out[i] = spectrumLevels_[i].load(std::memory_order_relaxed);
    }
    return out;
}

float DfxDspEngine::evaluateEqResponseDb(float freqHz) const noexcept {
    std::lock_guard<std::mutex> lock(paramMutex_);
    return eq_.evaluateResponseDb(freqHz);
}

} // namespace fxsound
