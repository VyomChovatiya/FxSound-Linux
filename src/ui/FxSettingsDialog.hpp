#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QTabWidget>

namespace fxsound {

struct AppSettingsState {
    bool bonusPresets = false;
    bool launchOnStartup = false;
    bool autoSwitchOutput = true;
    bool hideHelpTips = false;
    bool disableShortcuts = false;
    QString languageCode = "en";
};

class FxSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit FxSettingsDialog(const AppSettingsState& currentSettings, QWidget* parent = nullptr);

    AppSettingsState settingsState() const { return state_; }

    static void applyAutostartFile(bool enabled);

signals:
    void settingsChanged(const AppSettingsState& newState);
    void resetFactoryPresetsRequested();

private:

    AppSettingsState state_;
    QTabWidget* tabWidget_ = nullptr;
    QCheckBox* launchStartupCheck_ = nullptr;
    QCheckBox* autoSwitchCheck_ = nullptr;
    QCheckBox* hideTipsCheck_ = nullptr;
    QCheckBox* disableShortcutsCheck_ = nullptr;
    QComboBox* langCombo_ = nullptr;
    QPushButton* resetPresetsBtn_ = nullptr;
};

} // namespace fxsound
