#include "audio/LinuxAudioEngine.hpp"
#include "dsp/DfxDspEngine.hpp"
#include "dsp/FacPreset.hpp"
#include "ui/FxMainWindow.hpp"
#include "ui/FxSettingsDialog.hpp"
#include "ui/FxTheme.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QSettings>
#include <QKeyEvent>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace {
int g_sigUsrPipe[2] = {-1, -1};

void handleSigUsr1(int /*sig*/) {
    if (g_sigUsrPipe[1] >= 0) {
        const char byte = 1;
        [[maybe_unused]] const auto written = ::write(g_sigUsrPipe[1], &byte, 1);
    }
}
} // namespace

// Some Wayland/compositor combinations leave stale pixels when a raster
// window submits only a small hover/control damage region. Expand each pending
// widget-window update to the complete client area before Qt synchronizes its
// backing store. This is event-driven, not a continuous repaint timer, and covers
// Settings, input/message/file dialogs, and the main window alike.
class FxApplication final : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject* receiver, QEvent* event) override {
        if (event->type() == QEvent::UpdateRequest &&
            qEnvironmentVariable("FXSOUND_PARTIAL_UPDATES") != "1") {
            if (auto* widget = qobject_cast<QWidget*>(receiver);
                widget && widget->isWindow() && widget->isVisible()) {
                widget->update(widget->rect());
            }
        }
        return QApplication::notify(receiver, event);
    }
};

int main(int argc, char* argv[]) {
    // Isolate FxSound from third-party Qt platform theme/style plugins (e.g. qtengine / caelestia.colors / Kvantum)
    // that inject broken hover shaders into QComboBox popups and cause Wayland surface glitches.
    qunsetenv("QT_QPA_PLATFORMTHEME");
    qunsetenv("QT_STYLE_OVERRIDE");
    qunsetenv("QT_IM_MODULE");
    QLoggingCategory::setFilterRules(
        "qt.qpa.wayland.textinput=false\n"
        "kf.config.core=false"
    );

    // Keep all dialogs in Qt Widgets so the same backing-store policy applies.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    FxApplication app(argc, argv);
    if (QStyle* fusion = QStyleFactory::create("Fusion")) {
        QApplication::setStyle(fusion);
    }
    QApplication::setApplicationName("FxSound");
    QApplication::setApplicationVersion("1.1.16.0");
    QApplication::setOrganizationName("FxSound");

    QCommandLineParser parser;
    parser.setApplicationDescription("FxSound Pro Audio Enhancer (Linux Native Reversed Build)");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption minimizedOpt("minimized", "Start minimized to the system tray.");
    QCommandLineOption noSinkOpt("no-virtual-sink", "Do not create or switch the system virtual sink.");
    QCommandLineOption dumpScreenshotsOpt("dump-screenshots", "Render UI screenshots into <dir> and exit.", "dir");
    parser.addOption(minimizedOpt);
    parser.addOption(noSinkOpt);
    parser.addOption(dumpScreenshotsOpt);
    parser.process(app);

    auto& theme = fxsound::FxTheme::instance();
    theme.initialize();

    const bool dumpMode = parser.isSet(dumpScreenshotsOpt);
    const std::filesystem::path pidFilePath = "/tmp/fxsound_linux.pid";

    // Single-instance check: if FxSound is already running in the background (e.g. from startup),
    // wake up its window via SIGUSR1 instead of starting a duplicate audio engine.
    if (!dumpMode) {
        std::ifstream pidIn(pidFilePath);
        pid_t existingPid = 0;
        if ((pidIn >> existingPid) && existingPid > 0 && existingPid != ::getpid()) {
            if (::kill(existingPid, 0) == 0) {
                if (!parser.isSet(minimizedOpt)) {
                    ::kill(existingPid, SIGUSR1);
                }
                return 0;
            }
        }
        std::ofstream pidOut(pidFilePath, std::ios::trunc);
        pidOut << ::getpid() << "\n";
    }

    const std::filesystem::path factoryDir = theme.factoryPresetsDir();
    std::filesystem::path userDir;
    const QString cfgPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!cfgPath.isEmpty()) {
        userDir = std::filesystem::path(cfgPath.toStdString()) / "Presets";
    } else {
        userDir = theme.rootDir() / "build" / "user_presets";
    }

    fxsound::FacPresetManager presetManager(factoryDir, userDir);
    if (dumpMode) {
        if (presetManager.presets().size() < 12) qFatal("Factory preset resources missing");
        for (const auto* icon : {"Button_ON.svg", "Button_OFF.svg", "Logo.svg", "menu.svg", "settings.svg", "reset.svg"}) {
            if (theme.loadSvgPixmap(icon, QSize(32, 32)).isNull())
                qFatal("Required UI icon missing: %s", icon);
        }
    }
    fxsound::DfxDspEngine dspEngine;
    dspEngine.applyPreset(presetManager.currentPreset());

    fxsound::LinuxAudioEngine audioEngine(dspEngine);
    if (!parser.isSet(noSinkOpt) && !dumpMode) {
        audioEngine.start(true);
    }

    fxsound::FxMainWindow mainWindow(dspEngine, presetManager, audioEngine);

    QSocketNotifier* sigNotifier = nullptr;
    if (!dumpMode && ::pipe(g_sigUsrPipe) == 0) {
        std::signal(SIGUSR1, handleSigUsr1);
        sigNotifier = new QSocketNotifier(g_sigUsrPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(sigNotifier, &QSocketNotifier::activated, &mainWindow, [&]() {
            char buf[16];
            [[maybe_unused]] const auto n = ::read(g_sigUsrPipe[0], buf, sizeof(buf));
            mainWindow.showNormal();
            mainWindow.raise();
            mainWindow.activateWindow();
        });
    }

    if (dumpMode) {
        const QString outDirStr = parser.value(dumpScreenshotsOpt);
        QDir outDir(outDirStr);
        outDir.mkpath(".");

        // Feed a synthetic musical chord block through DfxDspEngine so the visualizer bars are lively
        std::vector<float> testAudio(2048 * 2, 0.0f);
        for (size_t i = 0; i < 2048; ++i) {
            const float t = static_cast<float>(i) / 48000.0f;
            const float s = 0.25f * std::sin(2.0f * 3.14159265f * 85.0f * t)
                          + 0.22f * std::sin(2.0f * 3.14159265f * 240.0f * t)
                          + 0.18f * std::sin(2.0f * 3.14159265f * 880.0f * t)
                          + 0.15f * std::sin(2.0f * 3.14159265f * 3200.0f * t)
                          + 0.12f * std::sin(2.0f * 3.14159265f * 9500.0f * t);
            testAudio[2 * i]     = s;
            testAudio[2 * i + 1] = s * 0.9f;
        }
        dspEngine.processStereoInterleaved(testAudio.data(), 2048);

        mainWindow.setPower(true);
        mainWindow.setProView(true);
        mainWindow.show();

        QTimer::singleShot(120, [&]() {
            auto* power = mainWindow.findChild<QPushButton*>("mainPowerButton");
            if (!power) qFatal("Main power button missing");
            power->click();
            if (mainWindow.isPoweredOn()) qFatal("Power button failed to switch off");
            power->click();
            if (!mainWindow.isPoweredOn()) qFatal("Power button failed to switch on");
            const auto sliders = mainWindow.findChildren<fxsound::FxVerticalEqSlider*>();
            if (sliders.size() != 9) qFatal("EQ controls missing");
            const float savedGain = sliders.front()->boostDb();
            sliders.front()->setBoostDb(3.0f, true);
            if (presetManager.currentPreset().getUiEqBand(0).boostDb != 3.0f) qFatal("EQ control not connected");
            sliders.front()->setBoostDb(savedGain, true);
            auto* reset = mainWindow.findChild<QPushButton*>("resetCurrentPresetButton");
            if (!reset || !reset->isVisible() || reset->toolTip() != "reset to default")
                qFatal("Reset button or tooltip missing");
            const auto presetNameBeforeReset = presetManager.currentPreset().name;
            sliders.front()->setBoostDb(savedGain == 10.0f ? -10.0f : 10.0f, true);
            reset->click();
            if (presetManager.isCurrentPresetModified() || presetManager.currentPreset().name != presetNameBeforeReset)
                qFatal("Reset button failed to restore the current preset");
            if (sliders.front()->boostDb() != presetManager.currentPreset().getUiEqBand(0).boostDb)
                qFatal("Reset button did not refresh EQ controls");
            mainWindow.grab().save(outDir.filePath("fxsound_pro_view_on.png"));
            for (auto* button : mainWindow.findChildren<QPushButton*>()) {
                if (button->accessibleName() == "Settings and presets menu") {
                    button->click();
                    QApplication::processEvents();
                    auto* overlay = mainWindow.findChild<QWidget*>("applicationMenuOverlay");
                    if (!overlay || !overlay->isVisible() || overlay->isWindow()) qFatal("Menu must be a visible child overlay");
                    mainWindow.grab().save(outDir.filePath("fxsound_application_menu.png"));
                    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QApplication::sendEvent(QApplication::focusWidget(), &escape);
                    if (overlay->isVisible()) qFatal("Menu Escape dismissal failed");
                    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    break;
                }
            }


            mainWindow.setPower(false);
            QApplication::processEvents();
            mainWindow.grab().save(outDir.filePath("fxsound_pro_view_off.png"));

            mainWindow.setPower(true);
            mainWindow.setProView(false);
            QApplication::processEvents();
            mainWindow.grab().save(outDir.filePath("fxsound_lite_view.png"));

            // Exercise the actual settings-to-preset-manager connection, then reopen
            // the dialog to verify its state survives dialog recreation.
            const size_t originalCount = presetManager.presets().size();
            bool initialBonus = false;
            QTimer::singleShot(0, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog) qFatal("Settings dialog did not open");
                for (auto* checkbox : dialog->findChildren<QCheckBox*>()) {
                    if (checkbox->text().startsWith("Enable bonus presets")) {
                        initialBonus = checkbox->isChecked();
                        checkbox->click();
                        const size_t expected = initialBonus ? originalCount - 12 : originalCount + 12;
                        if (presetManager.presets().size() != expected) qFatal("Bonus checkbox did not update presets");
                        QSettings persisted("FxSound", "FxSoundLinux");
                        persisted.sync();
                        if (persisted.value("bonusPresets").toBool() != checkbox->isChecked()) qFatal("Bonus preference not persisted");
                        dialog->grab().save(outDir.filePath("fxsound_settings_dialog.png"));
                        dialog->accept();
                        return;
                    }
                }
                qFatal("Bonus preset setting missing");
            });
            mainWindow.openSettingsDialog();
            QTimer::singleShot(0, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog) qFatal("Settings dialog did not reopen");
                for (auto* checkbox : dialog->findChildren<QCheckBox*>()) {
                    if (checkbox->text().startsWith("Enable bonus presets")) {
                        if (checkbox->isChecked() == initialBonus) qFatal("Bonus setting was not retained");
                        checkbox->click();
                        if (presetManager.presets().size() != originalCount) qFatal("Bonus disable did not restore list");
                        dialog->accept();
                        return;
                    }
                }
                qFatal("Bonus preset setting missing on reopen");
            });
            mainWindow.openSettingsDialog();

            app.quit();
        });

        return app.exec();
    }

    if (!parser.isSet(minimizedOpt)) {
        mainWindow.show();
    }

    const int rc = app.exec();
    audioEngine.stop();
    if (!dumpMode) {
        std::error_code ec;
        std::filesystem::remove(pidFilePath, ec);
    }
    return rc;
}
