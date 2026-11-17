#include "FxSettingsDialog.hpp"
#include "FxTheme.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace fxsound {

FxSettingsDialog::FxSettingsDialog(const AppSettingsState& currentSettings, QWidget* parent)
    : QDialog(parent), state_(currentSettings) {
    auto& theme = FxTheme::instance();
    setWindowTitle(theme.trKey("Settings"));
    setFixedSize(560, 470);
    setStyleSheet(R"(
        QDialog, QWidget {
            background-color: #181818;
            color: #B1B1B1;
        }
        QLabel {
            color: #B1B1B1;
            background: transparent;
        }
        QComboBox {
            background-color: #0f0f0f;
            border: 1px solid #38384A;
            border-radius: 6px;
            padding: 5px 12px;
            min-height: 22px;
            color: #B1B1B1;
            font-size: 13px;
            font-weight: 600;
        }
        QComboBox:hover {
            border: 1px solid #E33250;
        }
        QComboBox QAbstractItemView {
            background-color: #131313;
            border: 1px solid #E33250;
            selection-background-color: #E33250;
            selection-color: #FFFFFF;
            color: #B1B1B1;
        }
        QTabWidget::pane {
            border: 1px solid #2F2F3E;
            background-color: #131313;
            border-radius: 6px;
        }
        QTabBar::tab {
            background-color: #181818;
            color: #A6A6B5;
            padding: 8px 22px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            font-weight: 600;
        }
        QTabBar::tab:selected {
            background-color: #E33250;
            color: #FFFFFF;
        }
        QCheckBox {
            spacing: 10px;
            color: #B1B1B1;
            font-size: 13px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 4px;
            border: 1px solid #48485A;
            background: #0f0f0f;
        }
        QCheckBox::indicator:checked {
            background: #E33250;
            border: 1px solid #EF4B65;
        }
        QPushButton {
            background-color: #2A2A38;
            border: 1px solid #3E3E52;
            border-radius: 6px;
            padding: 7px 14px;
            color: #B1B1B1;
            font-weight: 600;
        }
        QPushButton:hover {
            border: 1px solid #E33250;
            background-color: #343446;
        }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(12);

    tabWidget_ = new QTabWidget(this);

    // --- Tab 1: General ---
    auto* generalPage = new QWidget(this);
    auto* genLayout = new QVBoxLayout(generalPage);
    genLayout->setContentsMargins(18, 16, 18, 16);
    genLayout->setSpacing(12);

    launchStartupCheck_ = new QCheckBox(theme.trKey("Launch on system startup"), generalPage);
    launchStartupCheck_->setChecked(state_.launchOnStartup);

    autoSwitchCheck_ = new QCheckBox(theme.trKey("Automatically switch to newly connected output device"), generalPage);
    autoSwitchCheck_->setChecked(state_.autoSwitchOutput);

    hideTipsCheck_ = new QCheckBox(theme.trKey("Hide help tips"), generalPage);
    hideTipsCheck_->setChecked(state_.hideHelpTips);

    disableShortcutsCheck_ = new QCheckBox(theme.trKey("Disable keyboard shortcuts"), generalPage);
    disableShortcutsCheck_->setChecked(state_.disableShortcuts);

    auto* langRow = new QHBoxLayout();
    auto* langLabel = new QLabel(theme.trKey("Language") + ":", generalPage);
    langLabel->setFont(theme.uiFont(10, true));
    langCombo_ = new FxComboBox(generalPage);
    const auto& langs = theme.availableLanguages();
    for (size_t i = 0; i < langs.size(); ++i) {
        langCombo_->addItem(langs[i].displayName, langs[i].code);
        if (langs[i].code == state_.languageCode) {
            langCombo_->setCurrentIndex(static_cast<int>(i));
        }
    }
    langRow->addWidget(langLabel);
    langRow->addWidget(langCombo_, 1);

    auto* shortcutsBox = new QLabel(
        "<b>Keyboard Shortcuts:</b><br>"
        "• Turn FxSound On/Off: <code>Ctrl+Alt+Q</code> &nbsp;&nbsp; • Open/Close: <code>Ctrl+Alt+E</code><br>"
        "• Next/Prev Preset: <code>Ctrl+Alt+A / D</code> &nbsp;&nbsp; • Change Output: <code>Ctrl+Alt+W / S</code>",
        generalPage
    );
    shortcutsBox->setStyleSheet("color: #B4B4C4; font-size: 11px; background: #171720; padding: 8px; border-radius: 5px;");

    resetPresetsBtn_ = new QPushButton(theme.trKey("Reset presets to factory defaults"), generalPage);

    auto* bonusCheck = new QCheckBox("Enable bonus presets (12 music presets)", generalPage);
    bonusCheck->setChecked(state_.bonusPresets);
    bonusCheck->setToolTip("70’s, 80’s, Alternative Rock, Classic Rock, Classical, Jazz, Metal, Modern Country, Modern Rock, Pop, R&B, Trap");
    connect(bonusCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        state_.bonusPresets = enabled;
        emit settingsChanged(state_);
    });
    genLayout->addWidget(bonusCheck);
    genLayout->addWidget(launchStartupCheck_);
    genLayout->addWidget(autoSwitchCheck_);
    genLayout->addWidget(hideTipsCheck_);
    genLayout->addWidget(disableShortcutsCheck_);
    genLayout->addLayout(langRow);
    genLayout->addWidget(shortcutsBox);
    genLayout->addStretch(1);
    genLayout->addWidget(resetPresetsBtn_);

    // --- Tab 2: Help ---
    auto* helpPage = new QWidget(this);
    auto* helpLayout = new QVBoxLayout(helpPage);
    helpLayout->setContentsMargins(18, 16, 18, 16);
    helpLayout->setSpacing(12);

    auto* logoLbl = new QLabel(helpPage);
    const QPixmap logoPix = theme.loadSvgPixmap("Logo.svg", QSize(145, 32));
    if (!logoPix.isNull()) {
        logoLbl->setPixmap(logoPix);
    }

    auto* verLbl = new QLabel(
        "<b>FxSound Pro Audio Enhancer (Linux Native)</b><br>"
        "Version: 1.1.16.0 (Reversed from Windows x64 Binary &amp; Factsoft DSP)<br>"
        "Engine: 64-bit DfxDsp / 10-Band SOS Biquad EQ / PipeWire &amp; PulseAudio Virtual Sink",
        helpPage
    );
    verLbl->setStyleSheet("color: #D2D2E0; font-size: 12px;");
    verLbl->setWordWrap(true);

    auto* helpCenterBtn = new QPushButton(theme.trKey("Help Center") + " (fxsound.com/learning-center)", helpPage);
    auto* changelogBtn = new QPushButton(theme.trKey("Change Logs") + " (fxsound.com/changelog)", helpPage);
    auto* feedbackBtn = new QPushButton(theme.trKey("Submit Feedback"), helpPage);

    connect(helpCenterBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://www.fxsound.com/learning-center"));
    });
    connect(changelogBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://www.fxsound.com/changelog"));
    });
    connect(feedbackBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "FxSound Help", "FxSound Linux Native Reversed Build v1.1.16.0\nAll 12 Factsoft presets and 5 DSP modulation stages active.");
    });

    helpLayout->addWidget(logoLbl);
    helpLayout->addWidget(verLbl);
    helpLayout->addSpacing(6);
    helpLayout->addWidget(helpCenterBtn);
    helpLayout->addWidget(changelogBtn);
    helpLayout->addWidget(feedbackBtn);
    helpLayout->addStretch(1);

    tabWidget_->addTab(generalPage, theme.trKey("General"));
    tabWidget_->addTab(helpPage, theme.trKey("Help"));
    mainLayout->addWidget(tabWidget_, 1);

    // Bottom Close button
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch(1);
    auto* closeBtn = new QPushButton("Close", this);
    closeBtn->setStyleSheet("QPushButton { background-color: #E33250; color: white; padding: 7px 24px; border: none; } QPushButton:hover { background-color: #EF4B65; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    mainLayout->addLayout(bottomRow);

    auto emitUpdate = [this]() {
        state_.launchOnStartup = launchStartupCheck_->isChecked();
        state_.autoSwitchOutput = autoSwitchCheck_->isChecked();
        state_.hideHelpTips = hideTipsCheck_->isChecked();
        state_.disableShortcuts = disableShortcutsCheck_->isChecked();
        state_.languageCode = langCombo_->currentData().toString();
        applyAutostartFile(state_.launchOnStartup);
        emit settingsChanged(state_);
    };

    connect(launchStartupCheck_, &QCheckBox::toggled, this, emitUpdate);
    connect(autoSwitchCheck_, &QCheckBox::toggled, this, emitUpdate);
    connect(hideTipsCheck_, &QCheckBox::toggled, this, emitUpdate);
    connect(disableShortcutsCheck_, &QCheckBox::toggled, this, emitUpdate);
    connect(langCombo_, &QComboBox::currentIndexChanged, this, emitUpdate);

    connect(resetPresetsBtn_, &QPushButton::clicked, this, [this]() {
        emit resetFactoryPresetsRequested();
        resetPresetsBtn_->setText("✓ Presets Reset to Factory Defaults");
        resetPresetsBtn_->setStyleSheet(
            "QPushButton { background-color: #1F4E3B; border: 1px solid #2EA872; color: #FFFFFF; }"
        );
    });
}

void FxSettingsDialog::applyAutostartFile(bool enabled) {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty()) return;

    QDir autostartDir(configDir + "/autostart");
    const QString desktopFilePath = autostartDir.filePath("fxsound.desktop");
    if (enabled) {
        autostartDir.mkpath(".");
        QFile f(desktopFilePath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString exePath = QCoreApplication::applicationFilePath();
            const QString content = QString(
                "[Desktop Entry]\n"
                "Type=Application\n"
                "Name=FxSound\n"
                "Comment=FxSound Pro Audio Enhancer\n"
                "Exec=%1 --minimized\n"
                "Terminal=false\n"
                "Categories=AudioVideo;Audio;\n"
            ).arg(exePath);
            f.write(content.toUtf8());
        }
    } else {
        QFile::remove(desktopFilePath);
    }
}

} // namespace fxsound
