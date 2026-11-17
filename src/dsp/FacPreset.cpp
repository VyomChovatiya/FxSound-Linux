#include "FacPreset.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fxsound {

namespace {

// Exact 10-step quantizer table observed across Factsoft/1.fac..12.fac for UI slider steps 0..9
constexpr std::array<int, 10> kUiStepToFacMain = {
    0, 15, 30, 45, 60, 75, 90, 105, 118, 127
};

constexpr std::array<UiEqBandBounds, 9> kUiEqBands = {{
    {0, 2,   80.0f,   115.0f,   165.0f,   5.0f,  "86 Hz - 129 Hz",    "Super-low end bass"},
    {1, 3,  150.0f,   250.0f,   310.0f,  10.0f,  "158 Hz - 250 Hz",   "Center bass"},
    {2, 4,  290.0f,   450.0f,   550.0f,  10.0f,  "293 Hz - 450 Hz",   "Low end of mid-range"},
    {3, 5,  540.0f,   630.0f,   950.0f,  20.0f,  "540 Hz - 805 Hz",   "Focal low-mid range"},
    {4, 6, 1000.0f,  1250.0f,  1800.0f,  50.0f,  "1.17 kHz - 1.36 kHz", "Center of mid-range"},
    {5, 7, 2000.0f,  2700.0f,  3600.0f, 100.0f,  "2.13 kHz - 3.43 kHz", "High end of mid-range"},
    {6, 8, 4000.0f,  5300.0f,  6000.0f, 100.0f,  "4.40 kHz - 5.35 kHz", "Low end of high-range"},
    {7, 9, 6200.0f,  7500.0f, 11800.0f, 200.0f,  "6.30 kHz - 11.76 kHz", "Core of high-range"},
    {8, 10, 11500.0f, 13000.0f, 16000.0f, 250.0f, "11.77 kHz - 16.00 kHz", "High-end overtones / air"},
}};

std::string trimString(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string sanitizeFileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "Custom_Preset";
    }
    return out;
}

} // namespace

bool EqBandParam::operator==(const EqBandParam& other) const {
    return bandNumber == other.bandNumber &&
           std::fabs(centerFreqHz - other.centerFreqHz) < 0.05f &&
           std::fabs(boostDb - other.boostDb) < 0.01f;
}

const std::array<UiEqBandBounds, 9>& getUiEqBandBounds() {
    return kUiEqBands;
}

std::string formatFrequencyLabel(float hz) {
    std::ostringstream oss;
    if (hz >= 1000.0f) {
        float khz = hz / 1000.0f;
        if (khz >= 10.0f) {
            oss << std::fixed << std::setprecision(1) << khz << " kHz";
        } else {
            oss << std::fixed << std::setprecision(2) << khz << " kHz";
        }
    } else {
        oss << static_cast<int>(std::lround(hz)) << " Hz";
    }
    return oss.str();
}

FacPreset::FacPreset() {
    bands[0] = {1, 62.5f, 0.0f};
    for (size_t i = 0; i < kUiEqBands.size(); ++i) {
        bands[i + 1] = {
            kUiEqBands[i].dspBandNumber,
            kUiEqBands[i].defaultHz,
            0.0f
        };
    }
}

int FacPreset::effectToMainIndex(EffectIndex idx) {
    // Disassembled from DfxDspPreset.cpp (0x1400cd80c - 0x1400cda16)
    switch (idx) {
        case EffectIndex::Clarity:       return 0; // Main 0
        case EffectIndex::Ambience:      return 3; // Main 3
        case EffectIndex::SurroundSound: return 1; // Main 1
        case EffectIndex::DynamicBoost:  return 4; // Main 4
        case EffectIndex::BassBoost:     return 5; // Main 5
        default:                         return 0;
    }
}

int FacPreset::effectToBypassIndex(EffectIndex idx) {
    // Disassembled from DfxDspPreset.cpp (0x1400cd819 - 0x1400cd9c2)
    switch (idx) {
        case EffectIndex::Clarity:       return 0; // Integer[0]
        case EffectIndex::Ambience:      return 2; // Integer[2]
        case EffectIndex::SurroundSound: return 1; // Integer[1]
        case EffectIndex::DynamicBoost:  return 3; // Integer[3]
        case EffectIndex::BassBoost:     return 4; // Integer[4]
        default:                         return 0;
    }
}

int FacPreset::getEffectUiValue(EffectIndex idx) const {
    if (isEffectBypassed(idx)) {
        return 0;
    }
    const int mainVal = std::clamp(mainParams[effectToMainIndex(idx)], 0, 127);
    // Disassembled from DfxDspPreset.cpp (0x1400cd80c) & DfxDspPrivate.cpp (0x1400ccbc0):
    // Qnt maps [0, 127] -> [0.0f, 1.0f], and UI knob multiplies by 10.0f (0x1403a290c)
    return std::clamp(static_cast<int>(std::lround((static_cast<float>(mainVal) / 127.0f) * 10.0f)), 0, 9);
}

float FacPreset::getEffectNormalized(EffectIndex idx) const {
    if (isEffectBypassed(idx)) {
        return 0.0f;
    }
    const int mainVal = std::clamp(mainParams[effectToMainIndex(idx)], 0, 127);
    return static_cast<float>(mainVal) / 127.0f;
}

void FacPreset::setEffectUiValue(EffectIndex idx, int uiVal) {
    const int clamped = std::clamp(uiVal, 0, 9);
    const int mainIdx = effectToMainIndex(idx);
    const int bypassIdx = effectToBypassIndex(idx);
    mainParams[mainIdx] = static_cast<int>(std::lround((static_cast<float>(clamped) / 10.0f) * 127.0f));
    // In Factsoft .fac (DfxDspPreset.cpp @ 0x1400cd819), Integer[k] == 1 means effect is enabled, 0 means bypassed
    appIntegers[bypassIdx] = (clamped > 0) ? 1 : 0;
    modified = true;
}

bool FacPreset::isEffectBypassed(EffectIndex idx) const {
    // Disassembled from DfxDspPreset.cpp @ 0x1400cd819: cmp eax, 1; setne cl (bypass = (Integer[k] != 1))
    return appIntegers[effectToBypassIndex(idx)] == 0;
}

void FacPreset::setEffectBypassed(EffectIndex idx, bool bypassed) {
    appIntegers[effectToBypassIndex(idx)] = bypassed ? 0 : 1;
    modified = true;
}

const EqBandParam& FacPreset::getUiEqBand(int uiBandIdx) const {
    const int clamped = std::clamp(uiBandIdx, 0, 8);
    return bands[clamped + 1];
}

void FacPreset::setUiEqBandBoostDb(int uiBandIdx, float boostDb) {
    const int clamped = std::clamp(uiBandIdx, 0, 8);
    bands[clamped + 1].boostDb = std::clamp(boostDb, -12.0f, 12.0f);
    // Band 1 (62.5 Hz) tracks Bass Boost & Band 2 sub-bass contour subtly if needed
    modified = true;
}

void FacPreset::setUiEqBandCenterFreq(int uiBandIdx, float freqHz) {
    const int clamped = std::clamp(uiBandIdx, 0, 8);
    const auto& bounds = kUiEqBands[clamped];
    bands[clamped + 1].centerFreqHz = std::clamp(freqHz, bounds.minHz, bounds.maxHz);
    modified = true;
}

void FacPreset::stepUiEqBandCenterFreq(int uiBandIdx, int direction) {
    const int clamped = std::clamp(uiBandIdx, 0, 8);
    const auto& bounds = kUiEqBands[clamped];
    const float nextFreq = bands[clamped + 1].centerFreqHz + static_cast<float>(direction) * bounds.stepHz;
    setUiEqBandCenterFreq(clamped, nextFreq);
}

bool FacPreset::hasSameParameters(const FacPreset& other) const {
    return mainParams == other.mainParams &&
           elemParams == other.elemParams &&
           appIntegers == other.appIntegers &&
           eqEnabled == other.eqEnabled &&
           bands == other.bands;
}

std::optional<FacPreset> FacPreset::loadFromFile(const std::filesystem::path& path, bool isFactoryPreset) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return parseFromString(ss.str(), path, isFactoryPreset);
}

std::optional<FacPreset> FacPreset::parseFromString(
    const std::string& content,
    const std::filesystem::path& path,
    bool isFactoryPreset) {
    std::istringstream iss(content);
    std::vector<std::string> lines;
    std::string rawLine;
    while (std::getline(iss, rawLine)) {
        std::string t = trimString(rawLine);
        if (!t.empty()) {
            lines.push_back(t);
        }
    }
    if (lines.size() < 10 ||
        (lines[0].find("CLASS1") == std::string::npos && lines[0].find("Factsoft") == std::string::npos)) {
        return std::nullopt;
    }

    FacPreset preset;
    preset.sourcePath = path;
    preset.isFactory = isFactoryPreset;
    preset.modified = false;
    preset.name = lines[2];

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        auto parseLeadingNumber = [](const std::string& s) -> float {
            const auto colonPos = s.find(':');
            const std::string numPart = trimString(colonPos == std::string::npos ? s : s.substr(0, colonPos));
            try {
                return std::stof(numPart);
            } catch (...) {
                return 0.0f;
            }
        };

        for (int m = 0; m < 6; ++m) {
            const std::string tag = ": Main " + std::to_string(m);
            if (line.find(tag) != std::string::npos) {
                preset.mainParams[m] = static_cast<int>(std::lround(parseLeadingNumber(line)));
            }
        }
        for (int p = 0; p < 7; ++p) {
            const std::string tag = ": Param " + std::to_string(p);
            if (line.find(tag) != std::string::npos) {
                preset.elemParams[p] = static_cast<int>(std::lround(parseLeadingNumber(line)));
            }
        }
        for (int a = 0; a < 7; ++a) {
            const std::string tag = ": Integer[" + std::to_string(a) + "]";
            if (line.find(tag) != std::string::npos) {
                preset.appIntegers[a] = static_cast<int>(std::lround(parseLeadingNumber(line)));
            }
        }
        if (line.find(": On/Off Flag") != std::string::npos) {
            preset.eqEnabled = (static_cast<int>(std::lround(parseLeadingNumber(line))) != 0);
        }
        if (line.rfind("Band ", 0) == 0 && i + 2 < lines.size()) {
            try {
                const int bandNum = std::stoi(line.substr(5));
                if (bandNum >= 1 && bandNum <= 10) {
                    const float cf = parseLeadingNumber(lines[i + 1]);
                    const float boost = parseLeadingNumber(lines[i + 2]);
                    preset.bands[bandNum - 1] = {bandNum, cf, boost};
                }
            } catch (...) {
            }
        }
    }

    return preset;
}

std::string FacPreset::serializeToString() const {
    std::ostringstream oss;
    oss << "CLASS1 : Effect Type\r\n"
        << "9: Version\r\n"
        << name << "\r\n"
        << "0: Double Params Flag\r\n"
        << "1: Total number of elements\r\n";
    for (int m = 0; m < 6; ++m) {
        oss << mainParams[m] << ": Main " << m << "\r\n";
    }
    oss << "0: Element Number\r\n";
    for (int p = 0; p < 7; ++p) {
        oss << "   " << elemParams[p] << ": Param " << p << "\r\n";
    }
    oss << "7: Number of Application Dependent Integers\r\n"
        << "0: Number of Application Dependent Reals\r\n"
        << "0: Number of Application Dependent Strings\r\n";
    for (int a = 0; a < 7; ++a) {
        oss << appIntegers[a] << ": Integer[" << a << "]\r\n";
    }
    oss << "10: Number of EQ Bands\r\n"
        << (eqEnabled ? 1 : 0) << ": On/Off Flag\r\n";
    oss << std::fixed << std::setprecision(3);
    for (int b = 0; b < 10; ++b) {
        oss << "Band " << (b + 1) << "\r\n"
            << "   " << bands[b].centerFreqHz << ": CF\r\n"
            << "   " << bands[b].boostDb << ": Boost/Cut\r\n";
    }
    return oss.str();
}

bool FacPreset::saveToFile(const std::filesystem::path& path) const {
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    const std::string data = serializeToString();
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

FacPresetManager::FacPresetManager(
    const std::filesystem::path& factoryDir,
    const std::filesystem::path& userDir)
    : factoryDir_(factoryDir), userDir_(userDir) {
    reloadAll();
}

void FacPresetManager::setBonusPresetsEnabled(bool enabled) {
    if (bonusEnabled_ == enabled) return;
    const auto previous = currentPreset();
    bonusEnabled_ = enabled;
    reloadAll();
    currentIndex_ = 0;
    for (size_t i = 0; i < presets_.size(); ++i) {
        if (presets_[i].name == previous.name && presets_[i].isFactory == previous.isFactory) {
            currentIndex_ = static_cast<int>(i);
            presets_[i] = previous;
            break;
        }
    }
}

void FacPresetManager::reloadAll() {
    presets_.clear();
    savedSnapshots_.clear();

    std::error_code ec;
    std::filesystem::create_directories(userDir_, ec);

    // 1. Load the 12 factory presets (1.fac .. 12.fac).
    // If user modified and saved a factory preset override in userDir_/factory_overrides/, load that.
    const auto factoryOverridesDir = userDir_ / "factory_overrides";
    std::filesystem::create_directories(factoryOverridesDir, ec);

    for (int i = 1; i <= 12; ++i) {
        const std::string fname = std::to_string(i) + ".fac";
        const auto overridePath = factoryOverridesDir / fname;
        const auto origPath = factoryDir_ / fname;
        std::optional<FacPreset> loaded;
        if (std::filesystem::exists(overridePath, ec)) {
            loaded = FacPreset::loadFromFile(overridePath, true);
        }
        if (!loaded && std::filesystem::exists(origPath, ec)) {
            loaded = FacPreset::loadFromFile(origPath, true);
        }
        if (loaded) {
            presets_.push_back(*loaded);
            savedSnapshots_.push_back(*loaded);
        }
    }

    // Fallback default if factory directory wasn't found
    if (presets_.empty()) {
        FacPreset fallback;
        fallback.isFactory = true;
        presets_.push_back(fallback);
        savedSnapshots_.push_back(fallback);
    }

    // 2. Load any custom user .fac presets from userDir_
    std::vector<std::filesystem::path> userFiles;
    if (std::filesystem::exists(userDir_, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(userDir_, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".fac") {
                userFiles.push_back(entry.path());
            }
        }
    }
    std::sort(userFiles.begin(), userFiles.end());
    for (const auto& uf : userFiles) {
        if (auto loaded = FacPreset::loadFromFile(uf, false)) {
            presets_.push_back(*loaded);
            savedSnapshots_.push_back(*loaded);
        }
    }

    if (bonusEnabled_) {
        const auto bonusDir = factoryDir_.parent_path() / "BonusPresets";
        std::vector<std::filesystem::path> files;
        if (std::filesystem::exists(bonusDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(bonusDir))
                if (entry.is_regular_file() && entry.path().extension() == ".fac") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            auto preset = FacPreset::loadFromFile(factoryOverridesDir / file.filename(), true);
            if (!preset) preset = FacPreset::loadFromFile(file, true);
            if (preset) { presets_.push_back(*preset); savedSnapshots_.push_back(*preset); }
        }
    }

    currentIndex_ = std::clamp(currentIndex_, 0, static_cast<int>(presets_.size()) - 1);
}

bool FacPresetManager::resetToFactoryDefaults() {
    const int prevIdx = currentIndex_;
    std::error_code ec;
    const auto factoryOverridesDir = userDir_ / "factory_overrides";
    if (std::filesystem::exists(factoryOverridesDir, ec)) {
        std::filesystem::remove_all(factoryOverridesDir, ec);
    }
    reloadAll();
    currentIndex_ = std::clamp(prevIdx, 0, static_cast<int>(presets_.size()) - 1);
    return true;
}

bool FacPresetManager::resetCurrentPresetToFactory() {
    if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(presets_.size())) {
        return false;
    }
    auto& cur = presets_[currentIndex_];
    if (cur.isFactory) {
        const std::string fname = cur.sourcePath.filename().string();
        std::error_code ec;
        std::filesystem::remove(userDir_ / "factory_overrides" / fname, ec);
        const auto original = std::filesystem::exists(factoryDir_ / fname) ? factoryDir_ / fname : factoryDir_.parent_path() / "BonusPresets" / fname;
        if (auto orig = FacPreset::loadFromFile(original, true)) {
            presets_[currentIndex_] = *orig;
            savedSnapshots_[currentIndex_] = *orig;
            return true;
        }
    }
    presets_[currentIndex_] = savedSnapshots_[currentIndex_];
    presets_[currentIndex_].modified = false;
    return true;
}

const FacPreset& FacPresetManager::currentPreset() const {
    return presets_[currentIndex_];
}

FacPreset& FacPresetManager::currentPresetMutable() {
    return presets_[currentIndex_];
}

bool FacPresetManager::selectPreset(int index) {
    if (index < 0 || index >= static_cast<int>(presets_.size())) {
        return false;
    }
    currentIndex_ = index;
    return true;
}

bool FacPresetManager::selectNextPreset() {
    if (presets_.empty()) {
        return false;
    }
    currentIndex_ = (currentIndex_ + 1) % static_cast<int>(presets_.size());
    return true;
}

bool FacPresetManager::selectPreviousPreset() {
    if (presets_.empty()) {
        return false;
    }
    currentIndex_ = (currentIndex_ - 1 + static_cast<int>(presets_.size())) % static_cast<int>(presets_.size());
    return true;
}

bool FacPresetManager::undoCurrentPresetChanges() {
    if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(presets_.size())) {
        return false;
    }
    presets_[currentIndex_] = savedSnapshots_[currentIndex_];
    presets_[currentIndex_].modified = false;
    return true;
}

bool FacPresetManager::isCurrentPresetModified() const {
    if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(presets_.size())) {
        return false;
    }
    return !presets_[currentIndex_].hasSameParameters(savedSnapshots_[currentIndex_]);
}

bool FacPresetManager::saveCurrentPresetOverwrite() {
    auto& cur = presets_[currentIndex_];
    std::filesystem::path targetPath = cur.sourcePath;
    if (cur.isFactory) {
        const auto fname = cur.sourcePath.filename().empty()
                               ? (std::to_string(currentIndex_ + 1) + ".fac")
                               : cur.sourcePath.filename().string();
        targetPath = userDir_ / "factory_overrides" / fname;
    } else if (targetPath.empty()) {
        targetPath = userDir_ / (sanitizeFileName(cur.name) + ".fac");
    }
    if (!cur.saveToFile(targetPath)) {
        return false;
    }
    cur.sourcePath = targetPath;
    cur.modified = false;
    savedSnapshots_[currentIndex_] = cur;
    return true;
}

int FacPresetManager::countUserPresets() const {
    int count = 0;
    for (const auto& p : presets_) {
        if (!p.isFactory) {
            ++count;
        }
    }
    return count;
}

std::optional<int> FacPresetManager::saveAsNewPreset(const std::string& newName, std::string* errorOut) {
    const std::string trimmed = trimString(newName);
    if (trimmed.empty()) {
        if (errorOut) *errorOut = "Please enter a valid preset name.";
        return std::nullopt;
    }
    if (countUserPresets() >= kMaxUserPresets) {
        if (errorOut) *errorOut = "You have reached the limit of 10 saved presets.";
        return std::nullopt;
    }
    for (const auto& p : presets_) {
        if (p.name == trimmed) {
            if (errorOut) *errorOut = "Preset with the same name already exists.";
            return std::nullopt;
        }
    }

    FacPreset newPreset = currentPreset();
    newPreset.name = trimmed;
    newPreset.isFactory = false;
    newPreset.modified = false;
    newPreset.sourcePath = userDir_ / (sanitizeFileName(trimmed) + ".fac");
    if (!newPreset.saveToFile(newPreset.sourcePath)) {
        if (errorOut) *errorOut = "Failed to write preset file to disk.";
        return std::nullopt;
    }

    // Restore any unsaved edits on the previous preset back to its snapshot
    presets_[currentIndex_] = savedSnapshots_[currentIndex_];

    presets_.push_back(newPreset);
    savedSnapshots_.push_back(newPreset);
    currentIndex_ = static_cast<int>(presets_.size()) - 1;
    return currentIndex_;
}

bool FacPresetManager::renameCurrentPreset(const std::string& newName, std::string* errorOut) {
    auto& cur = presets_[currentIndex_];
    if (cur.isFactory) {
        if (errorOut) *errorOut = "Default presets cannot be renamed.";
        return false;
    }
    const std::string trimmed = trimString(newName);
    if (trimmed.empty()) {
        if (errorOut) *errorOut = "Please enter a valid preset name.";
        return false;
    }
    for (size_t i = 0; i < presets_.size(); ++i) {
        if (static_cast<int>(i) != currentIndex_ && presets_[i].name == trimmed) {
            if (errorOut) *errorOut = "Preset with the same name already exists.";
            return false;
        }
    }

    const auto oldPath = cur.sourcePath;
    cur.name = trimmed;
    cur.sourcePath = userDir_ / (sanitizeFileName(trimmed) + ".fac");
    if (!cur.saveToFile(cur.sourcePath)) {
        if (errorOut) *errorOut = "Failed to write renamed preset file.";
        return false;
    }
    if (!oldPath.empty() && oldPath != cur.sourcePath) {
        std::error_code ec;
        std::filesystem::remove(oldPath, ec);
    }
    savedSnapshots_[currentIndex_] = cur;
    return true;
}

bool FacPresetManager::deleteCurrentPreset(std::string* errorOut) {
    auto& cur = presets_[currentIndex_];
    if (cur.isFactory) {
        if (errorOut) *errorOut = "Default presets cannot be deleted.";
        return false;
    }
    if (!cur.sourcePath.empty()) {
        std::error_code ec;
        std::filesystem::remove(cur.sourcePath, ec);
    }
    presets_.erase(presets_.begin() + currentIndex_);
    savedSnapshots_.erase(savedSnapshots_.begin() + currentIndex_);
    currentIndex_ = std::clamp(currentIndex_ - 1, 0, static_cast<int>(presets_.size()) - 1);
    return true;
}

std::optional<int> FacPresetManager::importPresetFile(const std::filesystem::path& filePath, std::string* errorOut) {
    auto imported = FacPreset::loadFromFile(filePath, false);
    if (!imported) {
        if (errorOut) *errorOut = "Invalid or corrupted .fac preset file.";
        return std::nullopt;
    }
    if (countUserPresets() >= kMaxUserPresets) {
        if (errorOut) *errorOut = "You have reached the limit of 10 saved presets.";
        return std::nullopt;
    }
    std::string candidateName = imported->name;
    int suffix = 2;
    while (std::any_of(presets_.begin(), presets_.end(), [&](const FacPreset& p) { return p.name == candidateName; })) {
        candidateName = imported->name + " (" + std::to_string(suffix++) + ")";
    }
    imported->name = candidateName;
    imported->isFactory = false;
    imported->modified = false;
    imported->sourcePath = userDir_ / (sanitizeFileName(candidateName) + ".fac");
    if (!imported->saveToFile(imported->sourcePath)) {
        if (errorOut) *errorOut = "Failed to save imported preset to user preset directory.";
        return std::nullopt;
    }
    presets_.push_back(*imported);
    savedSnapshots_.push_back(*imported);
    currentIndex_ = static_cast<int>(presets_.size()) - 1;
    return currentIndex_;
}

bool FacPresetManager::exportCurrentPresetFile(const std::filesystem::path& filePath) const {
    return currentPreset().saveToFile(filePath);
}

} // namespace fxsound
