#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fxsound {

// UI Effect indices matching the 5 horizontal sliders on the left of FxProView
enum class EffectIndex : int {
    Clarity = 0,       // Main 0 (valFidelity / aural0), Integer[0] bypass
    Ambience = 1,      // Main 3 (valAmbience / lex0), Integer[2] bypass
    SurroundSound = 2, // Main 1 (valSurround / wid0 + BinauralSyn), Integer[1] bypass
    DynamicBoost = 3,  // Main 4 (valDynamicBoost / apt0 + cmp0 + ply0), Integer[3] bypass
    BassBoost = 4,     // Main 5 (valBassBoost / max0), Integer[4] bypass
    Count = 5
};

struct EqBandParam {
    int bandNumber = 1;       // 1..10 (Band 1 = fixed 62.5 Hz, Bands 2..10 = 9 UI EQ sliders)
    float centerFreqHz = 62.5f;
    float boostDb = 0.0f;     // [-12.0f, +12.0f]

    bool operator==(const EqBandParam& other) const;
    bool operator!=(const EqBandParam& other) const { return !(*this == other); }
};

struct UiEqBandBounds {
    int uiIndex;        // 0..8
    int dspBandNumber;  // 2..10
    float minHz;
    float defaultHz;
    float maxHz;
    float stepHz;
    const char* tooltipRange;
    const char* tooltipDescription;
};

// Returns the bounds and metadata for the 9 interactive UI Equalizer bands (Bands 2..10)
const std::array<UiEqBandBounds, 9>& getUiEqBandBounds();

// Format frequency in FxSound UI style ("115 Hz", "1.25 kHz", "13.0 kHz")
std::string formatFrequencyLabel(float hz);

struct FacPreset {
    std::string name = "General";
    std::filesystem::path sourcePath;
    bool isFactory = true;
    bool modified = false;

    // Raw Factsoft .fac fields preserved for lossless round-tripping
    std::array<int, 6> mainParams = {50, 20, 0, 0, 60, 60};          // 0..127 quantized
    std::array<int, 7> elemParams = {0, 0, 0, 0, 0, 0, 0};           // Param 0..6
    std::array<int, 7> appIntegers = {1, 1, 0, 1, 1, 0, 2};          // Integer[0..6] (1 = effect enabled, 0 = bypassed)
    bool eqEnabled = true;
    std::array<EqBandParam, 10> bands; // Band 1..10

    FacPreset();

    // Parse a .fac file from disk or string content
    static std::optional<FacPreset> loadFromFile(const std::filesystem::path& path, bool isFactoryPreset = false);
    static std::optional<FacPreset> parseFromString(const std::string& content, const std::filesystem::path& path = {}, bool isFactoryPreset = false);

    // Serialize back to authentic Factsoft .fac format
    std::string serializeToString() const;
    bool saveToFile(const std::filesystem::path& path) const;

    // Disassembled mapping between UI Effect Sliders (0..9 or 0..10) and .fac Main[0..5] (0..127)
    // Effect 0 (Clarity)        <-> Main[0], bypass Integer[0]
    // Effect 1 (Ambience)       <-> Main[3], bypass Integer[2]
    // Effect 2 (Surround Sound) <-> Main[1], bypass Integer[1]
    // Effect 3 (Dynamic Boost)  <-> Main[4], bypass Integer[3]
    // Effect 4 (Bass Boost)     <-> Main[5], bypass Integer[4]
    static int effectToMainIndex(EffectIndex idx);
    static int effectToBypassIndex(EffectIndex idx);

    int getEffectUiValue(EffectIndex idx) const;       // 0..9 (or 10)
    float getEffectNormalized(EffectIndex idx) const;  // 0.0f..1.0f
    void setEffectUiValue(EffectIndex idx, int uiVal); // updates Main[] and Integer[]

    bool isEffectBypassed(EffectIndex idx) const;
    void setEffectBypassed(EffectIndex idx, bool bypassed);

    // UI Equalizer bands (0..8 map to DSP Band 2..10)
    const EqBandParam& getUiEqBand(int uiBandIdx) const;
    void setUiEqBandBoostDb(int uiBandIdx, float boostDb);
    void setUiEqBandCenterFreq(int uiBandIdx, float freqHz);
    void stepUiEqBandCenterFreq(int uiBandIdx, int direction); // -1 = prev, +1 = next

    bool hasSameParameters(const FacPreset& other) const;
};

class FacPresetManager {
public:
    FacPresetManager(const std::filesystem::path& factoryDir, const std::filesystem::path& userDir);

    void reloadAll();
    void setBonusPresetsEnabled(bool enabled);
    bool resetToFactoryDefaults();

    const std::vector<FacPreset>& presets() const { return presets_; }
    int currentIndex() const { return currentIndex_; }
    const FacPreset& currentPreset() const;
    FacPreset& currentPresetMutable();

    bool selectPreset(int index);
    bool selectNextPreset();
    bool selectPreviousPreset();

    // Undo changes on current preset back to its saved disk snapshot (or factory original)
    bool undoCurrentPresetChanges();
    bool resetCurrentPresetToFactory();
    bool isCurrentPresetModified() const;

    // CRUD operations for presets (matching Windows FxSound hamburger menu)
    bool saveCurrentPresetOverwrite();
    std::optional<int> saveAsNewPreset(const std::string& newName, std::string* errorOut = nullptr);
    bool renameCurrentPreset(const std::string& newName, std::string* errorOut = nullptr);
    bool deleteCurrentPreset(std::string* errorOut = nullptr);

    // Import / Export .fac files
    std::optional<int> importPresetFile(const std::filesystem::path& filePath, std::string* errorOut = nullptr);
    bool exportCurrentPresetFile(const std::filesystem::path& filePath) const;

    int countUserPresets() const;
    static constexpr int kMaxUserPresets = 10;

private:
    std::filesystem::path factoryDir_;
    std::filesystem::path userDir_;
    std::vector<FacPreset> presets_;
    std::vector<FacPreset> savedSnapshots_;
    int currentIndex_ = 0;
    bool bonusEnabled_ = false;
};

} // namespace fxsound
