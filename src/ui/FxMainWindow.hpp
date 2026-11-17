#pragma once

#include "../audio/LinuxAudioEngine.hpp"
#include "../dsp/DfxDspEngine.hpp"
#include "../dsp/FacPreset.hpp"
#include "FxEffectsPanel.hpp"
#include "FxEqualizerPanel.hpp"
#include "FxSettingsDialog.hpp"
#include "FxVisualizer.hpp"

#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPoint>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <vector>

namespace fxsound {

class FxMainWindow : public QMainWindow {
    Q_OBJECT

public:
    FxMainWindow(
        DfxDspEngine& dspEngine,
        FacPresetManager& presetManager,
        LinuxAudioEngine& audioEngine,
        QWidget* parent = nullptr
    );
    ~FxMainWindow() override;

    bool isProView() const { return isProView_; }
    void setProView(bool proView);

    void togglePower();
    void setPower(bool enabled);
    bool isPoweredOn() const { return poweredOn_; }

    void setLoudnessMode(LoudnessMode mode);
    LoudnessMode loudnessMode() const { return dspEngine_.loudnessMode(); }

    void openSettingsDialog(int initialTab = 0);
    void refreshPresetCombo();
    void refreshDeviceCombo();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onPresetComboChanged(int index);
    void onDeviceComboChanged(int index);
    void onSavePresetOverwrite();
    void onSaveNewPreset();
    void onUndoPresetChanges();
    void onRenamePreset();
    void onDeletePreset();
    void onImportPreset();
    void onExportPreset();

private:
    void buildUi();
    void buildTitleBar(QVBoxLayout* rootLayout);
    QWidget* buildProViewWidget();
    QWidget* buildLiteViewWidget();
    void buildHamburgerMenu();
    void showApplicationMenu(QWidget* anchor);
    void buildSystemTray();
    void setupKeyboardShortcuts();
    void syncUiFromCurrentPreset();
    void updatePowerVisuals();
    void updateTrayMenu();
    void loadPersistentSettings();
    void savePersistentSettings();

    DfxDspEngine& dspEngine_;
    FacPresetManager& presetManager_;
    LinuxAudioEngine& audioEngine_;

    bool isProView_ = true;
    bool poweredOn_ = true;
    bool forceQuit_ = false;
    bool draggingWindow_ = false;
    QPoint dragOffset_;

    AppSettingsState appSettings_;
    std::vector<AudioOutputDevice> outputDevices_;

    // Title bar widgets
    QLabel* logoLabel_ = nullptr;
    QLabel* presetModifiedBadge_ = nullptr;
    QPushButton* viewModeBtn_ = nullptr;
    QPushButton* menuBtn_ = nullptr;
    QPushButton* minimizeBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;

    // Top control bar (Pro view)
    QPushButton* proPowerBtn_ = nullptr;
    QComboBox* proPresetCombo_ = nullptr;
    QComboBox* proVolumeCombo_ = nullptr;
    QComboBox* proDeviceCombo_ = nullptr;

    // Lite view widgets
    QPushButton* litePowerBtn_ = nullptr;
    QComboBox* litePresetCombo_ = nullptr;
    QComboBox* liteVolumeCombo_ = nullptr;
    QComboBox* liteDeviceCombo_ = nullptr;
    QLabel* liteStatusLabel_ = nullptr;
    QPushButton* liteExpandBtn_ = nullptr;

    // Stacked container & panels
    QStackedWidget* viewStack_ = nullptr;
    FxVisualizer* visualizer_ = nullptr;
    FxEffectsPanel* effectsPanel_ = nullptr;
    FxEqualizerPanel* eqPanel_ = nullptr;

    // Hamburger menu & actions
    QMenu* hamburgerMenu_ = nullptr;
    QAction* actSavePreset_ = nullptr;
    QAction* actSaveNewPreset_ = nullptr;
    QAction* actUndoPreset_ = nullptr;
    QAction* actRenamePreset_ = nullptr;
    QAction* actDeletePreset_ = nullptr;
    QAction* actImportPreset_ = nullptr;
    QAction* actExportPreset_ = nullptr;
    QAction* actSettings_ = nullptr;
    QAction* actHelp_ = nullptr;
    QAction* actDonate_ = nullptr;

    // System tray
    QSystemTrayIcon* trayIcon_ = nullptr;
    QMenu* trayMenu_ = nullptr;

    // In-window Wayland-safe tooltip overlay (prevents native QToolTip wl_surface hover flicker)
    QLabel* inWindowTooltip_ = nullptr;

    // Keyboard shortcuts
    std::vector<QShortcut*> shortcuts_;
};

} // namespace fxsound
