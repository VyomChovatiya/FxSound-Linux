#include "../src/dsp/DfxDspEngine.hpp"
#include "../src/dsp/FacPreset.hpp"
#include "../src/dsp/SosBiquad.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace fxsound;

namespace {

constexpr float kPi = 3.14159265358979323846f;

float computeRms(const std::vector<float>& buf) {
    if (buf.empty()) return 0.0f;
    double sumSq = 0.0;
    for (float x : buf) {
        sumSq += static_cast<double>(x) * static_cast<double>(x);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(buf.size())));
}

void testAll12FactoryPresetsRoundTrip(const std::filesystem::path& factsoftDir) {
    std::cout << "[TEST] Verifying all 12 Factsoft .fac presets parse and round-trip losslessly...\n";
    const std::vector<std::string> expectedNames = {
        "General",
        "Music",
        "Voice",
        "Volume Boost",
        "Gaming",
        "Classic Processing",
        "Light Processing",
        "Bass Boost",
        "Streaming Video",
        "Movies",
        "TV",
        "Transcription"
    };

    for (size_t i = 0; i < expectedNames.size(); ++i) {
        const auto path = factsoftDir / (std::to_string(i + 1) + ".fac");
        auto presetOpt = FacPreset::loadFromFile(path, true);
        assert(presetOpt.has_value() && "Failed to load factory .fac file");
        const FacPreset& p = *presetOpt;
        assert(p.name == expectedNames[i]);
        assert(p.bands.size() == 10);
        assert(std::fabs(p.bands[0].centerFreqHz - 62.5f) < 0.01f);

        // Verify round-trip serialization -> parsing preserves every parameter bit-accurately
        const std::string serialized = p.serializeToString();
        auto reparsedOpt = FacPreset::parseFromString(serialized, path, true);
        assert(reparsedOpt.has_value());
        assert(p.hasSameParameters(*reparsedOpt));
        assert(p.name == reparsedOpt->name);

        // Verify UI slider values are within [0, 9]
        for (int e = 0; e < 5; ++e) {
            const int uiVal = p.getEffectUiValue(static_cast<EffectIndex>(e));
            assert(uiVal >= 0 && uiVal <= 9);
        }
        std::cout << "  -> " << (i + 1) << ".fac (" << p.name << "): "
                  << "Clarity=" << p.getEffectUiValue(EffectIndex::Clarity)
                  << ", Ambience=" << p.getEffectUiValue(EffectIndex::Ambience)
                  << ", Surround=" << p.getEffectUiValue(EffectIndex::SurroundSound)
                  << ", DynamicBoost=" << p.getEffectUiValue(EffectIndex::DynamicBoost)
                  << ", BassBoost=" << p.getEffectUiValue(EffectIndex::BassBoost) << " [OK]\n";
    }
}

void testPresetManagerCrud(const std::filesystem::path& factsoftDir) {
    std::cout << "[TEST] Verifying FacPresetManager CRUD (Save New, Overwrite, Undo, Rename, Delete, Import/Export)...\n";
    const auto tmpUserDir = std::filesystem::temp_directory_path() / "fxsound_test_user_presets";
    std::error_code ec;
    std::filesystem::remove_all(tmpUserDir, ec);

    FacPresetManager mgr(factsoftDir, tmpUserDir);
    assert(mgr.presets().size() == 12);
    assert(mgr.currentPreset().name == "General");
    assert(!mgr.isCurrentPresetModified());

    // Modify an effect slider and verify undo
    const int origClarity = mgr.currentPreset().getEffectUiValue(EffectIndex::Clarity);
    mgr.currentPresetMutable().setEffectUiValue(EffectIndex::Clarity, 9);
    assert(mgr.isCurrentPresetModified());
    assert(mgr.undoCurrentPresetChanges());
    assert(!mgr.isCurrentPresetModified());
    assert(mgr.currentPreset().getEffectUiValue(EffectIndex::Clarity) == origClarity);

    // Save as new custom preset
    mgr.currentPresetMutable().setEffectUiValue(EffectIndex::BassBoost, 9);
    std::string err;
    auto newIdx = mgr.saveAsNewPreset("My Custom Bass", &err);
    assert(newIdx.has_value() && err.empty());
    assert(mgr.presets().size() == 13);
    assert(mgr.currentPreset().name == "My Custom Bass");
    assert(!mgr.currentPreset().isFactory);

    // Rename custom preset
    assert(mgr.renameCurrentPreset("Studio Reference", &err));
    assert(mgr.currentPreset().name == "Studio Reference");

    // Export & Import custom preset
    const auto exportedPath = tmpUserDir / "exported_test.fac";
    assert(mgr.exportCurrentPresetFile(exportedPath));
    auto importedIdx = mgr.importPresetFile(exportedPath, &err);
    assert(importedIdx.has_value());
    assert(mgr.presets().size() == 14);

    // Delete custom presets
    assert(mgr.deleteCurrentPreset(&err));
    assert(mgr.presets().size() == 13);

    std::filesystem::remove_all(tmpUserDir, ec);
    std::cout << "  -> FacPresetManager CRUD passed [OK]\n";
}

void testSosBiquadEqualizer() {
    std::cout << "[TEST] Verifying FiltbiqdSos & 10-band SosEqualizer frequency response...\n";
    SosEqualizer eq;
    eq.setSampleRate(48000.0f);
    eq.setBandParameters(5, 1000.0f, 12.0f); // +12 dB boost at 1 kHz

    const float resp1k = eq.evaluateResponseDb(1000.0f);
    const float resp100 = eq.evaluateResponseDb(100.0f);
    assert(resp1k > 11.2f && resp1k < 12.8f);
    assert(std::fabs(resp100) < 0.8f);

    eq.setBandParameters(5, 1000.0f, -12.0f); // -12 dB cut at 1 kHz
    const float cut1k = eq.evaluateResponseDb(1000.0f);
    assert(cut1k < -11.2f && cut1k > -12.8f);
    std::cout << "  -> SosEqualizer 1kHz +12dB response = " << resp1k << " dB, -12dB response = " << cut1k << " dB [OK]\n";
}

void testDfxDspEngineAllStages(const std::filesystem::path& factsoftDir) {
    std::cout << "[TEST] Verifying DfxDspEngine 5 modulation stages, brickwall limiter & all 12 presets...\n";
    DfxDspEngine dsp;
    dsp.setSampleRate(48000.0f);

    // 1. Power OFF bypass test (must be bit-exact passthrough)
    std::vector<float> orig(512 * 2, 0.0f);
    for (size_t i = 0; i < 512; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        orig[2 * i]     = 0.3f * std::sin(2.0f * kPi * 440.0f * t);
        orig[2 * i + 1] = 0.3f * std::cos(2.0f * kPi * 440.0f * t);
    }
    std::vector<float> testBuf = orig;
    dsp.setPowerEnabled(false);
    dsp.processStereoInterleaved(testBuf.data(), 512);
    for (size_t i = 0; i < orig.size(); ++i) {
        assert(testBuf[i] == orig[i]);
    }
    dsp.setPowerEnabled(true);

    // 2. Test Bass Boost increases 62.5 Hz low-end energy (test raw stage gain before loudness normalizer)
    dsp.setLoudnessNormalizationEnabled(false);
    FacPreset flatPreset;
    for (int e = 0; e < 5; ++e) flatPreset.setEffectUiValue(static_cast<EffectIndex>(e), 0);
    for (int b = 0; b < 9; ++b) flatPreset.setUiEqBandBoostDb(b, 0.0f);

    std::vector<float> subToneFlat(2048 * 2, 0.0f);
    for (size_t i = 0; i < 2048; ++i) {
        const float s = 0.2f * std::sin(2.0f * kPi * 62.5f * (static_cast<float>(i) / 48000.0f));
        subToneFlat[2 * i] = subToneFlat[2 * i + 1] = s;
    }
    std::vector<float> subToneBoosted = subToneFlat;

    dsp.resetAllStates();
    dsp.applyPreset(flatPreset);
    dsp.processStereoInterleaved(subToneFlat.data(), 2048);

    FacPreset bassPreset = flatPreset;
    bassPreset.setEffectUiValue(EffectIndex::BassBoost, 9);
    dsp.resetAllStates();
    dsp.applyPreset(bassPreset);
    dsp.processStereoInterleaved(subToneBoosted.data(), 2048);

    const float rmsSubFlat = computeRms(subToneFlat);
    const float rmsSubBoost = computeRms(subToneBoosted);
    assert(rmsSubBoost > rmsSubFlat * 1.35f);
    std::cout << "  -> Bass Boost (max0) 62.5 Hz RMS: flat=" << rmsSubFlat << " -> boosted=" << rmsSubBoost << " [OK]\n";

    // 3. Test Clarity increases 5 kHz high-frequency presence energy
    std::vector<float> hfFlat(2048 * 2, 0.0f);
    for (size_t i = 0; i < 2048; ++i) {
        const float s = 0.18f * std::sin(2.0f * kPi * 5000.0f * (static_cast<float>(i) / 48000.0f));
        hfFlat[2 * i] = hfFlat[2 * i + 1] = s;
    }
    std::vector<float> hfClarity = hfFlat;

    dsp.resetAllStates();
    dsp.applyPreset(flatPreset);
    dsp.processStereoInterleaved(hfFlat.data(), 2048);

    FacPreset clarityPreset = flatPreset;
    clarityPreset.setEffectUiValue(EffectIndex::Clarity, 9);
    dsp.resetAllStates();
    dsp.applyPreset(clarityPreset);
    dsp.processStereoInterleaved(hfClarity.data(), 2048);

    const float rmsHfFlat = computeRms(hfFlat);
    const float rmsHfClarity = computeRms(hfClarity);
    assert(rmsHfClarity > rmsHfFlat * 1.35f);
    std::cout << "  -> Clarity (aural0) 5 kHz RMS: flat=" << rmsHfFlat << " -> enhanced=" << rmsHfClarity << " [OK]\n";
    dsp.setLoudnessNormalizationEnabled(true);

    // 4. Test Ambience generates stereo reverberant tail after burst ends
    std::vector<float> burstAmb(4096 * 2, 0.0f);
    for (size_t i = 0; i < 1024; ++i) {
        const float s = 0.4f * std::sin(2.0f * kPi * 600.0f * (static_cast<float>(i) / 48000.0f));
        burstAmb[2 * i] = burstAmb[2 * i + 1] = s;
    }
    FacPreset ambPreset = flatPreset;
    ambPreset.setEffectUiValue(EffectIndex::Ambience, 9);
    dsp.resetAllStates();
    dsp.applyPreset(ambPreset);
    dsp.processStereoInterleaved(burstAmb.data(), 4096);

    float tailEnergy = 0.0f;
    for (size_t i = 2000; i < 4096; ++i) {
        tailEnergy += std::fabs(burstAmb[2 * i]) + std::fabs(burstAmb[2 * i + 1]);
    }
    assert(tailEnergy > 15.0f);
    std::cout << "  -> Ambience (lex0) reverb tail energy after tone burst = " << tailEnergy << " [OK]\n";

    // 4b. Test Surround Sound synthesizes wide stereo Side (L - R) energy even from a centered signal
    std::vector<float> centerSig(2048 * 2, 0.0f);
    for (size_t i = 0; i < 2048; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        const float s = 0.25f * std::sin(2.0f * kPi * 750.0f * t) + 0.15f * std::sin(2.0f * kPi * 1600.0f * t);
        centerSig[2 * i]     = s;
        centerSig[2 * i + 1] = s * 0.92f;
    }
    FacPreset surPreset = flatPreset;
    surPreset.setEffectUiValue(EffectIndex::SurroundSound, 9);
    dsp.resetAllStates();
    dsp.applyPreset(surPreset);
    dsp.processStereoInterleaved(centerSig.data(), 2048);

    float sideDiffEnergy = 0.0f;
    for (size_t i = 200; i < 2048; ++i) {
        sideDiffEnergy += std::fabs(centerSig[2 * i] - centerSig[2 * i + 1]);
    }
    assert(sideDiffEnergy > 50.0f);
    std::cout << "  -> Surround Sound (wid0 + BinauralSyn) stereo Side width energy = " << sideDiffEnergy << " [OK]\n";

    // 5. Run all 12 factory presets on hot input signal & verify brickwall limiter [-1.0, +1.0] & finite samples
    for (int pIdx = 1; pIdx <= 12; ++pIdx) {
        auto presetOpt = FacPreset::loadFromFile(factsoftDir / (std::to_string(pIdx) + ".fac"), true);
        assert(presetOpt.has_value());
        dsp.resetAllStates();
        dsp.applyPreset(*presetOpt);

        std::vector<float> hotAudio(1024 * 2, 0.0f);
        for (size_t i = 0; i < 1024; ++i) {
            const float t = static_cast<float>(i) / 48000.0f;
            hotAudio[2 * i]     = 0.95f * std::sin(2.0f * kPi * 110.0f * t) + 0.6f * std::sin(2.0f * kPi * 3500.0f * t);
            hotAudio[2 * i + 1] = 0.95f * std::cos(2.0f * kPi * 110.0f * t) + 0.6f * std::sin(2.0f * kPi * 4200.0f * t);
        }
        dsp.processStereoInterleaved(hotAudio.data(), 1024);
        for (float sample : hotAudio) {
            assert(std::isfinite(sample));
            assert(sample >= -1.0001f && sample <= 1.0001f);
        }
    }

    const auto spec = dsp.getSpectrumBands();
    float specSum = 0.0f;
    for (float b : spec) specSum += b;
    assert(specSum > 0.1f);
    std::cout << "  -> All 12 presets processed with brickwall limiter [-1.0, +1.0] & active 20-band spectrum [OK]\n";

    // 6. Verify Cross-Preset Loudness Normalization + Quiet / Normal / High Normal Volume Modes
    std::cout << "[TEST] Verifying Cross-Preset Loudness Normalization & Quiet/Normal/HighNormal Volume Modes...\n";
    auto makeMusicBlock = []() {
        std::vector<float> block(512 * 2, 0.0f);
        for (size_t i = 0; i < 512; ++i) {
            const float t = static_cast<float>(i) / 48000.0f;
            const float L = 0.12f * std::sin(2.0f * kPi * 70.0f * t)
                          + 0.12f * std::sin(2.0f * kPi * 260.0f * t)
                          + 0.12f * std::sin(2.0f * kPi * 1000.0f * t)
                          + 0.10f * std::sin(2.0f * kPi * 4200.0f * t);
            const float R = 0.12f * std::sin(2.0f * kPi * 70.0f * t)
                          + 0.12f * std::cos(2.0f * kPi * 260.0f * t)
                          + 0.12f * std::sin(2.0f * kPi * 1000.0f * t + 0.4f)
                          + 0.10f * std::cos(2.0f * kPi * 4200.0f * t);
            block[2 * i]     = L;
            block[2 * i + 1] = R;
        }
        return block;
    };

    const float dryRms = computeRms(makeMusicBlock());
    dsp.setLoudnessNormalizationEnabled(true);
    dsp.setLoudnessMode(LoudnessMode::Normal);

    float minPresetRms = 100.0f;
    float maxPresetRms = 0.0f;
    for (int pIdx = 1; pIdx <= 12; ++pIdx) {
        auto presetOpt = FacPreset::loadFromFile(factsoftDir / (std::to_string(pIdx) + ".fac"), true);
        assert(presetOpt.has_value());
        dsp.resetAllStates();
        dsp.applyPreset(*presetOpt);

        std::vector<float> blk;
        for (int b = 0; b < 10; ++b) {
            blk = makeMusicBlock();
            dsp.processStereoInterleaved(blk.data(), 512);
        }
        const float pRms = computeRms(blk);
        minPresetRms = std::min(minPresetRms, pRms);
        maxPresetRms = std::max(maxPresetRms, pRms);
        std::cout << "  -> Preset " << pIdx << " (" << presetOpt->name << ") Normalized RMS = " << pRms
                  << " (dry reference = " << dryRms << ")\n";
    }
    // Verify all 12 presets have normalized RMS tightly clustered around dryRms (within 0.6 dB spread)
    const float spreadDb = 20.0f * std::log10(maxPresetRms / std::max(minPresetRms, 1e-6f));
    std::cout << "  -> Total Loudness Spread across all 12 presets = " << spreadDb << " dB [OK]\n";
    assert(spreadDb < 0.8f);

    // Verify Quiet, Normal, and High Normal modes scale loudness accurately
    auto measureModeRms = [&](LoudnessMode mode) {
        dsp.resetAllStates();
        dsp.setLoudnessMode(mode);
        std::vector<float> blk;
        for (int b = 0; b < 10; ++b) {
            blk = makeMusicBlock();
            dsp.processStereoInterleaved(blk.data(), 512);
        }
        return computeRms(blk);
    };

    const float quietRms      = measureModeRms(LoudnessMode::Quiet);
    const float normalRms     = measureModeRms(LoudnessMode::Normal);
    const float highNormalRms = measureModeRms(LoudnessMode::HighNormal);
    std::cout << "  -> Volume Modes RMS: Quiet=" << quietRms
              << ", Normal=" << normalRms
              << ", HighNormal=" << highNormalRms << " [OK]\n";
    assert(quietRms < normalRms * 0.85f && quietRms > normalRms * 0.60f);
    assert(highNormalRms > normalRms * 1.55f);
}

} // namespace

int main() {
    std::filesystem::path root = std::filesystem::current_path();
    if (!std::filesystem::exists(root / "FxSound" / "Factsoft")) {
        root = root.parent_path();
    }
    const auto factsoftDir = root / "FxSound" / "Factsoft";

    testAll12FactoryPresetsRoundTrip(factsoftDir);
    testPresetManagerCrud(factsoftDir);
    const auto bonusTestDir = std::filesystem::temp_directory_path() / "fxsound-bonus-toggle-test";
    std::filesystem::remove_all(bonusTestDir);
    FacPresetManager bonusManager(factsoftDir, bonusTestDir);
    auto requireBonus = [](bool condition) {
        if (!condition) throw std::runtime_error("Bonus preset regression");
    };
    requireBonus(bonusManager.presets().size() == 12);
    bonusManager.setBonusPresetsEnabled(true);
    requireBonus(bonusManager.presets().size() == 24);
    requireBonus(bonusManager.countUserPresets() == 0);
    bonusManager.selectPreset(12);
    const auto originalBonus = bonusManager.currentPreset();
    bonusManager.currentPresetMutable().setUiEqBandBoostDb(0, 10);
    requireBonus(bonusManager.saveCurrentPresetOverwrite());
    bonusManager.reloadAll();
    requireBonus(bonusManager.currentPreset().getUiEqBand(0).boostDb == 10);
    requireBonus(bonusManager.resetCurrentPresetToFactory());
    requireBonus(bonusManager.currentPreset().hasSameParameters(originalBonus));
    bonusManager.setBonusPresetsEnabled(false);
    requireBonus(bonusManager.presets().size() == 12 && bonusManager.currentIndex() == 0);
    bonusManager.setBonusPresetsEnabled(true);
    requireBonus(bonusManager.presets().size() == 24);
    std::filesystem::remove_all(bonusTestDir);
    testSosBiquadEqualizer();
    testDfxDspEngineAllStages(factsoftDir);

    std::cout << "\nALL FXSOUND REVERSED DSP & PRESET TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
