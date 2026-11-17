#include "FxMainWindow.hpp"
#include "FxTheme.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeySequence>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

namespace fxsound {

FxMainWindow::FxMainWindow(
    DfxDspEngine& dspEngine,
    FacPresetManager& presetManager,
    LinuxAudioEngine& audioEngine,
    QWidget* parent)
    : QMainWindow(parent),
      dspEngine_(dspEngine),
      presetManager_(presetManager),
      audioEngine_(audioEngine) {
    setWindowTitle("FxSound");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowSystemMenuHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_InputMethodEnabled, false);

    auto& theme = FxTheme::instance();
    setStyleSheet(theme.globalStyleSheet());
    setWindowIcon(QIcon(theme.loadPngPixmap("fxsound.png")));

    loadPersistentSettings();
    theme.setLanguage(appSettings_.languageCode);

    buildUi();
    buildHamburgerMenu();
    buildSystemTray();
    setupKeyboardShortcuts();

    inWindowTooltip_ = new QLabel(this);
    inWindowTooltip_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    inWindowTooltip_->setStyleSheet(
        "QLabel { background-color: #15151E; color: #B1B1B1; border: 1px solid #E33250; "
        "border-radius: 5px; padding: 5px 9px; font-size: 11px; }"
    );
    inWindowTooltip_->hide();
    if (qApp) {
        qApp->installEventFilter(this);
    }

    refreshPresetCombo();
    refreshDeviceCombo();
    syncUiFromCurrentPreset();
    setProView(isProView_);
    updatePowerVisuals();
}

FxMainWindow::~FxMainWindow() {
    savePersistentSettings();
}

void FxMainWindow::loadPersistentSettings() {
    QSettings s("FxSound", "FxSoundLinux");
    appSettings_.bonusPresets = s.value("bonusPresets", false).toBool();
    presetManager_.setBonusPresetsEnabled(appSettings_.bonusPresets);
    appSettings_.launchOnStartup  = s.value("launchOnStartup", false).toBool();
    appSettings_.autoSwitchOutput = s.value("autoSwitchOutput", true).toBool();
    appSettings_.hideHelpTips     = s.value("hideHelpTips", false).toBool();
    appSettings_.disableShortcuts = s.value("disableShortcuts", false).toBool();
    appSettings_.languageCode     = s.value("languageCode", "en").toString();
    isProView_                    = s.value("isProView", true).toBool();
    poweredOn_                    = s.value("poweredOn", true).toBool();
    const int savedPresetIdx      = s.value("selectedPresetIndex", 0).toInt();
    const int savedLoudnessMode   = std::clamp(s.value("loudnessMode", 1).toInt(), 0, 2);
    presetManager_.selectPreset(savedPresetIdx);
    dspEngine_.setPowerEnabled(poweredOn_);
    dspEngine_.setLoudnessMode(static_cast<LoudnessMode>(savedLoudnessMode));
    audioEngine_.setAutoSwitchNewDevice(appSettings_.autoSwitchOutput);
}

void FxMainWindow::savePersistentSettings() {
    QSettings s("FxSound", "FxSoundLinux");
    s.setValue("bonusPresets", appSettings_.bonusPresets);
    s.setValue("launchOnStartup", appSettings_.launchOnStartup);
    s.setValue("autoSwitchOutput", appSettings_.autoSwitchOutput);
    s.setValue("hideHelpTips", appSettings_.hideHelpTips);
    s.setValue("disableShortcuts", appSettings_.disableShortcuts);
    s.setValue("languageCode", appSettings_.languageCode);
    s.setValue("isProView", isProView_);
    s.setValue("poweredOn", poweredOn_);
    s.setValue("selectedPresetIndex", presetManager_.currentIndex());
    s.setValue("loudnessMode", static_cast<int>(dspEngine_.loudnessMode()));
}

void FxMainWindow::buildUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(2, 2, 2, 2);
    rootLayout->setSpacing(0);

    buildTitleBar(rootLayout);

    viewStack_ = new QStackedWidget(central);
    viewStack_->addWidget(buildProViewWidget());  // index 0: FxProView (900x530)
    viewStack_->addWidget(buildLiteViewWidget()); // index 1: FxLiteView (400x360)
    rootLayout->addWidget(viewStack_, 1);
}

void FxMainWindow::buildTitleBar(QVBoxLayout* rootLayout) {
    auto& theme = FxTheme::instance();
    auto* headerWidget = new QWidget(this);
    headerWidget->setFixedHeight(52);
    headerWidget->setStyleSheet("background-color: #181818; border-bottom: 1px solid #242424;");

    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(14, 4, 10, 4);
    headerLayout->setSpacing(8);

    logoLabel_ = new QLabel(headerWidget);
    const qreal logoDpr = std::max(qreal(1), qApp->devicePixelRatio());
    QPixmap logoPix = theme.loadPngPixmap("logo-red.png").scaled(QSize(112, 24) * logoDpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    logoPix.setDevicePixelRatio(logoDpr);
    if (!logoPix.isNull()) {
        QPainter tint(&logoPix);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(logoPix.rect(), QColor("#E61C4C"));
    }
    if (!logoPix.isNull()) {
        logoLabel_->setPixmap(logoPix);
    } else {
        logoLabel_->setText("FXSOUND");
        logoLabel_->setFont(theme.titleFont(12, true));
    }
    logoLabel_->setStyleSheet("border: none; background: transparent;");

    presetModifiedBadge_ = new QLabel("• Modified", headerWidget);
    presetModifiedBadge_->setFont(theme.uiFont(8, true));
    presetModifiedBadge_->setStyleSheet("color: #EF4B65; border: none; background: transparent;");
    presetModifiedBadge_->setVisible(false);

    auto makeHeaderBtn = [&](const QString& svgName, const QString& tip) -> QPushButton* {
        auto* btn = new QPushButton(headerWidget);
        btn->setFixedSize(28, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tip);
        btn->setStyleSheet(
            "QPushButton { background: transparent; border: none; border-radius: 5px; color: #C8C8D6; font-size: 15px; font-weight: bold; }"
            "QPushButton:hover { background: #292938; color: #FFFFFF; }"
        );
        if (!svgName.isEmpty()) {
            btn->setIcon(theme.loadSvgIcon(svgName, QSize(16, 16)));
            btn->setIconSize(QSize(16, 16));
        }
        return btn;
    };

    viewModeBtn_ = makeHeaderBtn("minimize.svg", "Switch between Compact Lite View and Expanded Pro View");
    menuBtn_     = makeHeaderBtn("menu.svg", "Preset & Application Menu");
    minimizeBtn_ = makeHeaderBtn("", "Minimize to System Tray");
    minimizeBtn_->setText("—");
    closeBtn_    = makeHeaderBtn("", "Close to System Tray");
    closeBtn_->setText("✕");
    closeBtn_->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 5px; color: #E61C4C; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background: #E33250; color: #FFFFFF; }"
    );

    connect(viewModeBtn_, &QPushButton::clicked, this, [this]() { setProView(!isProView_); });
    connect(menuBtn_, &QPushButton::clicked, this, [this]() {
        if (hamburgerMenu_) {
            showApplicationMenu(menuBtn_);
        }
    });
    connect(minimizeBtn_, &QPushButton::clicked, this, &QWidget::hide);
    connect(closeBtn_, &QPushButton::clicked, this, &QWidget::close);

    headerLayout->addWidget(logoLabel_);
    headerLayout->addWidget(presetModifiedBadge_);
    headerLayout->addStretch(1);
    headerLayout->addWidget(viewModeBtn_);
    headerLayout->addWidget(menuBtn_);
    headerLayout->addWidget(minimizeBtn_);
    headerLayout->addWidget(closeBtn_);

    rootLayout->addWidget(headerWidget);
}

QWidget* FxMainWindow::buildProViewWidget() {
    auto& theme = FxTheme::instance();
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(18, 16, 18, 12);
    pageLayout->setSpacing(12);
    auto* body = new QWidget(page);
    body->setObjectName("processingPanel");
    body->setAttribute(Qt::WA_StyledBackground);
    body->setStyleSheet("QWidget#processingPanel { background: #131313; border-radius: 9px; }");
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(16);
    pageLayout->addWidget(body, 1);

    // Top control strip: Power button + Preset Selector + Output Device Selector
    auto* topBar = new QWidget(page);
    topBar->setFixedHeight(40);
    topBar->setStyleSheet("background-color: #131313; border: none; border-radius: 8px;");
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(38);

    proPowerBtn_ = new QPushButton(topBar);
    proPowerBtn_->setFixedSize(38, 38);
    proPowerBtn_->setObjectName("mainPowerButton");
    proPowerBtn_->setCursor(Qt::PointingHandCursor);
    proPowerBtn_->setStyleSheet("QPushButton { border: none; background: #090909; border-radius: 74px; }");
    proPowerBtn_->setToolTip("Turn FxSound On/Off (Ctrl+Alt+Q)");
    connect(proPowerBtn_, &QPushButton::clicked, this, &FxMainWindow::togglePower);

    auto* presetLbl = new QLabel("PRESET", topBar);
    presetLbl->setFont(theme.uiFont(8, true));
    presetLbl->setStyleSheet("color: #8E8E9E; border: none; background: transparent;");

    proPresetCombo_ = new FxComboBox(topBar);
    proPresetCombo_->setMinimumWidth(160);
    proPresetCombo_->setMinimumHeight(40);
    connect(proPresetCombo_, &QComboBox::currentIndexChanged, this, &FxMainWindow::onPresetComboChanged);

    auto* resetPresetBtn = new QPushButton("↺", topBar);
    resetPresetBtn->setFixedSize(30, 30);
    resetPresetBtn->setFocusPolicy(Qt::NoFocus);
    resetPresetBtn->setCursor(Qt::PointingHandCursor);
    resetPresetBtn->setToolTip("Reset current preset to default settings");
    resetPresetBtn->setStyleSheet(
        "QPushButton { background: #262634; border: 1px solid #38384A; border-radius: 6px; color: #D8D8E6; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { border: 1px solid #E33250; background: #E33250; color: #FFFFFF; }"
    );
    connect(resetPresetBtn, &QPushButton::clicked, this, &FxMainWindow::onUndoPresetChanges);

    auto* volLbl = new QLabel("VOLUME", topBar);
    volLbl->setFont(theme.uiFont(8, true));
    volLbl->setStyleSheet("color: #8E8E9E; border: none; background: transparent;");

    proVolumeCombo_ = new FxComboBox(topBar);
    proVolumeCombo_->setMinimumWidth(122);
    proVolumeCombo_->setToolTip("Normalized Volume Level (keeps loudness consistent across all presets)");
    proVolumeCombo_->addItem("Quiet", static_cast<int>(LoudnessMode::Quiet));
    proVolumeCombo_->addItem("Normal", static_cast<int>(LoudnessMode::Normal));
    proVolumeCombo_->addItem("High Normal", static_cast<int>(LoudnessMode::HighNormal));
    proVolumeCombo_->setCurrentIndex(static_cast<int>(dspEngine_.loudnessMode()));
    connect(proVolumeCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx >= 0 && idx <= 2) {
            setLoudnessMode(static_cast<LoudnessMode>(idx));
        }
    });

    auto* devLbl = new QLabel("OUTPUT", topBar);
    devLbl->setFont(theme.uiFont(8, true));
    devLbl->setStyleSheet("color: #8E8E9E; border: none; background: transparent;");

    proDeviceCombo_ = new FxComboBox(topBar);
    proDeviceCombo_->setMinimumWidth(195);
    proDeviceCombo_->setMinimumHeight(40);
    connect(proDeviceCombo_, &QComboBox::currentIndexChanged, this, &FxMainWindow::onDeviceComboChanged);

    auto* refreshDevBtn = new QPushButton("↻", topBar);
    refreshDevBtn->setFixedSize(30, 30);
    refreshDevBtn->setFocusPolicy(Qt::NoFocus);
    refreshDevBtn->setCursor(Qt::PointingHandCursor);
    refreshDevBtn->setToolTip("Reset preset & refresh audio output devices");
    refreshDevBtn->setStyleSheet(
        "QPushButton { background: #262634; border: 1px solid #38384A; border-radius: 6px; color: #D8D8E6; font-size: 14px; }"
        "QPushButton:hover { border: 1px solid #E33250; background: #E33250; color: #FFFFFF; }"
    );
    connect(refreshDevBtn, &QPushButton::clicked, this, [this]() {
        onUndoPresetChanges();
        refreshDeviceCombo();
    });


    topLayout->addSpacing(2);
    presetLbl->hide();
    topLayout->addWidget(proPresetCombo_, 1);
    resetPresetBtn->hide();
    topLayout->addSpacing(4);
    volLbl->hide();
    proVolumeCombo_->hide();
    topLayout->addSpacing(4);
    devLbl->hide();
    topLayout->addWidget(proDeviceCombo_, 1);
    refreshDevBtn->hide();

    layout->addWidget(topBar);

    // Middle: Real-Time Glowing Spectrum Visualizer
    visualizer_ = new FxVisualizer(dspEngine_, page);
    visualizer_->setFixedHeight(120);
    layout->addWidget(visualizer_);

    // Bottom split pane: Left = 5 Effects Sliders, Right = 9-Band Equalizer
    auto* splitRow = new QHBoxLayout();
    splitRow->setContentsMargins(0, 0, 0, 0);
    splitRow->setSpacing(10);

    effectsPanel_ = new FxEffectsPanel(page);
    effectsPanel_->setFixedWidth(168);

    eqPanel_ = new FxEqualizerPanel(page);

    connect(effectsPanel_, &FxEffectsPanel::effectValueChanged, this, [this](EffectIndex idx, int newVal) {
        presetManager_.currentPresetMutable().setEffectUiValue(idx, newVal);
        dspEngine_.applyPreset(presetManager_.currentPreset());
        presetModifiedBadge_->setVisible(presetManager_.isCurrentPresetModified());
    });

    connect(eqPanel_, &FxEqualizerPanel::bandBoostChanged, this, [this](int uiBandIdx, float newBoostDb) {
        presetManager_.currentPresetMutable().setUiEqBandBoostDb(uiBandIdx, newBoostDb);
        dspEngine_.applyPreset(presetManager_.currentPreset());
        presetModifiedBadge_->setVisible(presetManager_.isCurrentPresetModified());
    });

    connect(eqPanel_, &FxEqualizerPanel::bandFreqStepRequested, this, [this](int uiBandIdx, int dir) {
        presetManager_.currentPresetMutable().stepUiEqBandCenterFreq(uiBandIdx, dir);
        dspEngine_.applyPreset(presetManager_.currentPreset());
        eqPanel_->loadFromPreset(presetManager_.currentPreset());
        presetModifiedBadge_->setVisible(presetManager_.isCurrentPresetModified());
    });

    connect(eqPanel_, &FxEqualizerPanel::resetRequested, this, &FxMainWindow::onUndoPresetChanges);

    splitRow->addWidget(effectsPanel_);
    splitRow->addWidget(eqPanel_, 1);
    layout->addLayout(splitRow, 1);

    auto* powerRow = new QHBoxLayout();
    auto* footerMenu = new QPushButton(page);
    footerMenu->setIcon(theme.loadSvgIcon("menu.svg", QSize(22, 22)));
    footerMenu->setIconSize(QSize(22, 22));
    footerMenu->setFixedSize(48, 48);
    footerMenu->setAccessibleName("Settings and presets menu");
    footerMenu->setStyleSheet("background: #090909; border: none; border-radius: 24px;");
    connect(footerMenu, &QPushButton::clicked, this, [this, footerMenu]() {
        if (hamburgerMenu_) showApplicationMenu(footerMenu);
    });
    powerRow->addWidget(footerMenu, 0, Qt::AlignBottom);
    powerRow->addStretch();
    proPowerBtn_->setParent(page);
    proPowerBtn_->setFixedSize(148, 148);
    proPowerBtn_->setIconSize(QSize(120, 120));
    powerRow->addWidget(proPowerBtn_);
    powerRow->addStretch();
    auto* footerView = new QPushButton(page);
    footerView->setIcon(theme.loadSvgIcon("minimize.svg", QSize(22, 22)));
    footerView->setIconSize(QSize(22, 22));
    footerView->setFixedSize(48, 48);
    footerView->setAccessibleName("Switch to compact view");
    footerView->setStyleSheet("background: #090909; border: none; border-radius: 24px;");
    connect(footerView, &QPushButton::clicked, this, [this]() { setProView(false); });
    powerRow->addWidget(footerView, 0, Qt::AlignBottom);
    pageLayout->addLayout(powerRow);
    return page;
}

QWidget* FxMainWindow::buildLiteViewWidget() {
    auto& theme = FxTheme::instance();
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 18);
    layout->setSpacing(12);

    // Big centered Power Button
    auto* powerRow = new QHBoxLayout();
    litePowerBtn_ = new QPushButton(page);
    litePowerBtn_->setFixedSize(76, 76);
    litePowerBtn_->setCursor(Qt::PointingHandCursor);
    litePowerBtn_->setStyleSheet("QPushButton { border: none; background: transparent; }");
    litePowerBtn_->setToolTip("Turn FxSound On/Off (Ctrl+Alt+Q)");
    connect(litePowerBtn_, &QPushButton::clicked, this, &FxMainWindow::togglePower);
    powerRow->addStretch(1);
    powerRow->addWidget(litePowerBtn_);
    powerRow->addStretch(1);

    liteStatusLabel_ = new QLabel("FXSOUND ON • ACTIVE", page);
    liteStatusLabel_->setFont(theme.titleFont(10, true));
    liteStatusLabel_->setAlignment(Qt::AlignCenter);
    liteStatusLabel_->setStyleSheet("color: #EF4B65; background: transparent;");

    // Preset row with prev/next buttons
    auto* presetBox = new QWidget(page);
    presetBox->setStyleSheet("background-color: #0f0f0f; border: 1px solid #2A2A38; border-radius: 8px;");
    auto* presetLayout = new QVBoxLayout(presetBox);
    presetLayout->setContentsMargins(12, 8, 12, 10);
    presetLayout->setSpacing(5);

    auto* pTitle = new QLabel("PRESET", presetBox);
    pTitle->setFont(theme.uiFont(8, true));
    pTitle->setStyleSheet("color: #8E8E9E; border: none;");

    auto* pControls = new QHBoxLayout();
    auto* prevBtn = new QPushButton(presetBox);
    prevBtn->setFixedSize(28, 28);
    prevBtn->setIcon(theme.loadSvgIcon("arrow_prev.svg", QSize(10, 14)));
    prevBtn->setCursor(Qt::PointingHandCursor);
    prevBtn->setStyleSheet("QPushButton { background: #262634; border: none; border-radius: 5px; } QPushButton:hover { background: #E33250; }");
    connect(prevBtn, &QPushButton::clicked, this, [this]() {
        presetManager_.selectPreviousPreset();
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    });

    litePresetCombo_ = new FxComboBox(presetBox);
    connect(litePresetCombo_, &QComboBox::currentIndexChanged, this, &FxMainWindow::onPresetComboChanged);

    auto* nextBtn = new QPushButton(presetBox);
    nextBtn->setFixedSize(28, 28);
    nextBtn->setFocusPolicy(Qt::NoFocus);
    nextBtn->setIcon(theme.loadSvgIcon("arrow_next.svg", QSize(10, 14)));
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setStyleSheet("QPushButton { background: #262634; border: none; border-radius: 5px; } QPushButton:hover { background: #E33250; }");
    connect(nextBtn, &QPushButton::clicked, this, [this]() {
        presetManager_.selectNextPreset();
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    });

    pControls->addWidget(prevBtn);
    pControls->addWidget(litePresetCombo_, 1);
    pControls->addWidget(nextBtn);
    presetLayout->addWidget(pTitle);
    presetLayout->addLayout(pControls);

    // Output Device + Volume Level row in Lite View
    auto* devBox = new QWidget(page);
    devBox->setStyleSheet("background-color: #0f0f0f; border: 1px solid #2A2A38; border-radius: 8px;");
    auto* devLayout = new QVBoxLayout(devBox);
    devLayout->setContentsMargins(12, 8, 12, 10);
    devLayout->setSpacing(5);

    auto* dHeaderRow = new QHBoxLayout();
    auto* dTitle = new QLabel("PLAYBACK DEVICE & VOLUME", devBox);
    dTitle->setFont(theme.uiFont(8, true));
    dTitle->setStyleSheet("color: #8E8E9E; border: none;");
    dHeaderRow->addWidget(dTitle);
    dHeaderRow->addStretch(1);

    auto* dControlsRow = new QHBoxLayout();
    liteDeviceCombo_ = new FxComboBox(devBox);
    connect(liteDeviceCombo_, &QComboBox::currentIndexChanged, this, &FxMainWindow::onDeviceComboChanged);

    liteVolumeCombo_ = new FxComboBox(devBox);
    liteVolumeCombo_->setFixedWidth(118);
    liteVolumeCombo_->setToolTip("Normalized Volume Level (Quiet / Normal / High Normal)");
    liteVolumeCombo_->addItem("Quiet", static_cast<int>(LoudnessMode::Quiet));
    liteVolumeCombo_->addItem("Normal", static_cast<int>(LoudnessMode::Normal));
    liteVolumeCombo_->addItem("High Normal", static_cast<int>(LoudnessMode::HighNormal));
    liteVolumeCombo_->setCurrentIndex(static_cast<int>(dspEngine_.loudnessMode()));
    connect(liteVolumeCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx >= 0 && idx <= 2) {
            setLoudnessMode(static_cast<LoudnessMode>(idx));
        }
    });

    dControlsRow->addWidget(liteDeviceCombo_, 1);
    dControlsRow->addWidget(liteVolumeCombo_);
    devLayout->addLayout(dHeaderRow);
    devLayout->addLayout(dControlsRow);

    // Expand to Pro View button
    liteExpandBtn_ = new QPushButton("  Expand Pro Equalizer && Effects", page);
    liteExpandBtn_->setCursor(Qt::PointingHandCursor);
    liteExpandBtn_->setIcon(theme.loadSvgIcon("maximize.svg", QSize(14, 14)));
    liteExpandBtn_->setStyleSheet(
        "QPushButton { background-color: #262634; border: 1px solid #3B3B4F; border-radius: 6px; padding: 8px; color: #B1B1B1; font-weight: 600; }"
        "QPushButton:hover { border: 1px solid #E33250; background-color: #2F2F40; }"
    );
    connect(liteExpandBtn_, &QPushButton::clicked, this, [this]() { setProView(true); });

    layout->addLayout(powerRow);
    layout->addWidget(liteStatusLabel_);
    layout->addWidget(presetBox);
    layout->addWidget(devBox);
    layout->addStretch(1);
    layout->addWidget(liteExpandBtn_);

    return page;
}

namespace {
// A child overlay shares the main window backing store: no native popup surface.
class ActionMenuOverlay final : public QWidget {
public:
    explicit ActionMenuOverlay(QWidget* parent, QWidget* anchor) : QWidget(parent), anchor_(anchor) {
        setObjectName("applicationMenuOverlay");
        setGeometry(parent->rect());
        setFocusPolicy(Qt::StrongFocus);
    }
    void dismiss() { hide(); if (anchor_) anchor_->setFocus(); deleteLater(); }
protected:
    void mousePressEvent(QMouseEvent*) override { dismiss(); }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) dismiss();
        else QWidget::keyPressEvent(event);
    }
private:
    QPointer<QWidget> anchor_;
};
}

void FxMainWindow::showApplicationMenu(QWidget* anchor) {
    if (auto* existing = findChild<QWidget*>("applicationMenuOverlay")) {
        existing->hide();
        existing->deleteLater();
        return;
    }
    QMetaObject::invokeMethod(hamburgerMenu_, "aboutToShow", Qt::DirectConnection);
    auto* overlay = new ActionMenuOverlay(this, anchor);
    auto* card = new QWidget(overlay);
    card->setObjectName("applicationMenuCard");
    card->setAttribute(Qt::WA_StyledBackground);
    card->setStyleSheet(
        "QWidget#applicationMenuCard { background: #131313; border: 1px solid #393939; border-radius: 6px; }"
        "QPushButton { text-align: left; padding: 7px 14px; border: none; background: #131313; color: #B1B1B1; }"
        "QPushButton:hover, QPushButton:focus { background: #E33250; color: white; }"
        "QPushButton:disabled { color: #626262; }");
    auto* content = new QWidget(card);
    content->setStyleSheet("background-color: #131313;");
    auto* rows = new QVBoxLayout(content);
    rows->setContentsMargins(4, 6, 4, 6);
    rows->setSpacing(0);
    QPushButton* first = nullptr;
    for (auto* action : hamburgerMenu_->actions()) {
        if (!action->isVisible()) continue;
        if (action->isSeparator()) {
            auto* line = new QWidget(card);
            line->setFixedHeight(1);
            line->setStyleSheet("background: #393939;");
            rows->addWidget(line);
            continue;
        }
        auto* button = new QPushButton(action->text().replace('\t', "    "), card);
        button->setFont(FxTheme::instance().uiFont(11));
        button->setEnabled(action->isEnabled());
        button->setFocusPolicy(Qt::StrongFocus);
        rows->addWidget(button);
        if (!first && button->isEnabled()) first = button;
        connect(button, &QPushButton::clicked, this, [overlay, action]() {
            overlay->dismiss();
            action->trigger();
        });
    }
    auto* scroll = new QScrollArea(card);
    scroll->setStyleSheet("QScrollArea { background: #131313; border: none; } QScrollArea > QWidget { background: #131313; }");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 1, 1);
    cardLayout->addWidget(scroll);
    card->resize(std::max(280, content->sizeHint().width() + 24),
                 std::min(height() - 16, content->sizeHint().height() + 4));
    const QPoint point = anchor->mapTo(this, QPoint(0, 0));
    const int x = std::clamp(point.x(), 8, std::max(8, width() - card->width() - 8));
    const int y = point.y() >= card->height() + 8 ? point.y() - card->height() - 4 : 8;
    card->move(x, y);
    overlay->show();
    overlay->raise();
    if (first) first->setFocus();
}

void FxMainWindow::buildHamburgerMenu() {
    auto& theme = FxTheme::instance();
    hamburgerMenu_ = new QMenu(this);

    actSavePreset_    = hamburgerMenu_->addAction(theme.trKey("Save Preset"), this, &FxMainWindow::onSavePresetOverwrite);
    actSaveNewPreset_ = hamburgerMenu_->addAction(theme.trKey("Save New Preset") + "...", this, &FxMainWindow::onSaveNewPreset);
    actUndoPreset_    = hamburgerMenu_->addAction(theme.trKey("Undo Preset Changes"), this, &FxMainWindow::onUndoPresetChanges);
    actRenamePreset_  = hamburgerMenu_->addAction(theme.trKey("Rename Preset") + "...", this, &FxMainWindow::onRenamePreset);
    actDeletePreset_  = hamburgerMenu_->addAction(theme.trKey("Delete Preset"), this, &FxMainWindow::onDeletePreset);

    hamburgerMenu_->addSeparator();
    actImportPreset_  = hamburgerMenu_->addAction(theme.trKey("Import Presets") + "...\tCtrl+I", this, &FxMainWindow::onImportPreset);
    actExportPreset_  = hamburgerMenu_->addAction(theme.trKey("Export Presets") + "...\tCtrl+E", this, &FxMainWindow::onExportPreset);

    hamburgerMenu_->addSeparator();
    actSettings_      = hamburgerMenu_->addAction(theme.trKey("Settings") + "...", this, [this]() { openSettingsDialog(0); });
    actHelp_          = hamburgerMenu_->addAction(theme.trKey("Help"), this, [this]() { openSettingsDialog(1); });
    actDonate_        = hamburgerMenu_->addAction(theme.trKey("Donate"), this, []() {
        QDesktopServices::openUrl(QUrl("https://www.paypal.com/donate/?hosted_button_id=J2674VZX2JF3A"));
    });

    connect(hamburgerMenu_, &QMenu::aboutToShow, this, [this]() {
        const auto& cur = presetManager_.currentPreset();
        const bool isMod = presetManager_.isCurrentPresetModified();
        actSavePreset_->setEnabled(isMod);
        actUndoPreset_->setEnabled(true);
        actRenamePreset_->setEnabled(!cur.isFactory);
        actDeletePreset_->setEnabled(!cur.isFactory);
    });
}

void FxMainWindow::buildSystemTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    auto& theme = FxTheme::instance();
    trayIcon_ = new QSystemTrayIcon(this);
    trayIcon_->setIcon(QIcon(theme.loadPngPixmap(poweredOn_ ? "logo-red.png" : "logo-white.png")));
    trayIcon_->setToolTip("FxSound Audio Enhancer");

    trayMenu_ = new QMenu(this);
    trayIcon_->setContextMenu(trayMenu_);
    updateTrayMenu();

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (isVisible()) {
                hide();
            } else {
                showNormal();
                raise();
                activateWindow();
            }
        }
    });

    trayIcon_->show();
}

void FxMainWindow::updateTrayMenu() {
    if (!trayMenu_) return;
    auto& theme = FxTheme::instance();
    trayMenu_->clear();

    trayMenu_->addAction(isVisible() ? theme.trKey("Minimize") : theme.trKey("Open"), this, [this]() {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            raise();
            activateWindow();
        }
    });

    trayMenu_->addAction(
        poweredOn_ ? theme.trKey("Turn FxSound Off") : theme.trKey("Turn FxSound On"),
        this,
        &FxMainWindow::togglePower
    );

    trayMenu_->addSeparator();

    // PresetSelect submenu
    auto* presetSub = trayMenu_->addMenu(theme.trKey("PresetSelect"));
    const auto& presets = presetManager_.presets();
    for (size_t i = 0; i < presets.size(); ++i) {
        QAction* act = presetSub->addAction(QString::fromStdString(presets[i].name));
        act->setCheckable(true);
        act->setChecked(static_cast<int>(i) == presetManager_.currentIndex());
        connect(act, &QAction::triggered, this, [this, i]() {
            presetManager_.selectPreset(static_cast<int>(i));
            refreshPresetCombo();
            syncUiFromCurrentPreset();
        });
    }

    // Volume Level submenu in System Tray
    auto* volSub = trayMenu_->addMenu("Volume Level");
    const std::array<std::pair<const char*, LoudnessMode>, 3> volModes = {{
        {"Quiet",       LoudnessMode::Quiet},
        {"Normal",      LoudnessMode::Normal},
        {"High Normal", LoudnessMode::HighNormal},
    }};
    for (const auto& [label, m] : volModes) {
        QAction* act = volSub->addAction(label);
        act->setCheckable(true);
        act->setChecked(dspEngine_.loudnessMode() == m);
        connect(act, &QAction::triggered, this, [this, m]() {
            setLoudnessMode(m);
        });
    }

    // DeviceSelect submenu
    auto* devSub = trayMenu_->addMenu(theme.trKey("DeviceSelect"));
    const std::string curDev = audioEngine_.targetOutputDevice();
    for (size_t i = 0; i < outputDevices_.size(); ++i) {
        QAction* act = devSub->addAction(QString::fromStdString(outputDevices_[i].description));
        act->setCheckable(true);
        act->setChecked(outputDevices_[i].id == curDev);
        connect(act, &QAction::triggered, this, [this, i]() {
            if (proDeviceCombo_) proDeviceCombo_->setCurrentIndex(static_cast<int>(i));
        });
    }

    trayMenu_->addSeparator();
    trayMenu_->addAction(theme.trKey("Settings"), this, [this]() {
        showNormal();
        raise();
        openSettingsDialog(0);
    });
    trayMenu_->addAction(theme.trKey("Exit"), this, [this]() {
        forceQuit_ = true;
        close();
        QApplication::quit();
    });
}

void FxMainWindow::setupKeyboardShortcuts() {
    auto addSc = [this](const QString& seq, auto slotFn) {
        auto* sc = new QShortcut(QKeySequence(seq), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, slotFn]() {
            if (!appSettings_.disableShortcuts) {
                slotFn();
            }
        });
        shortcuts_.push_back(sc);
    };

    addSc("Ctrl+Alt+Q", [this]() { togglePower(); });
    addSc("Ctrl+Alt+E", [this]() {
        if (isVisible()) hide();
        else { showNormal(); raise(); activateWindow(); }
    });
    addSc("Ctrl+Alt+A", [this]() {
        presetManager_.selectNextPreset();
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    });
    addSc("Ctrl+Alt+D", [this]() {
        presetManager_.selectPreviousPreset();
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    });
    addSc("Ctrl+Alt+W", [this]() {
        if (proDeviceCombo_ && proDeviceCombo_->count() > 0) {
            proDeviceCombo_->setCurrentIndex((proDeviceCombo_->currentIndex() + 1) % proDeviceCombo_->count());
        }
    });
    addSc("Ctrl+Alt+S", [this]() {
        if (proDeviceCombo_ && proDeviceCombo_->count() > 0) {
            const int n = proDeviceCombo_->count();
            proDeviceCombo_->setCurrentIndex((proDeviceCombo_->currentIndex() - 1 + n) % n);
        }
    });
    addSc("Ctrl+I", [this]() { onImportPreset(); });
    addSc("Ctrl+E", [this]() { onExportPreset(); });
}

void FxMainWindow::setProView(bool proView) {
    isProView_ = proView;
    menuBtn_->setVisible(!proView);
    viewModeBtn_->setVisible(!proView);
    minimizeBtn_->setVisible(!proView);
    auto& theme = FxTheme::instance();
    if (isProView_) {
        viewStack_->setCurrentIndex(0);
        setFixedSize(FxTheme::kProWidth, FxTheme::kProHeight);
        if (viewModeBtn_) {
            viewModeBtn_->setIcon(theme.loadSvgIcon("minimize.svg", QSize(16, 16)));
            viewModeBtn_->setToolTip("Switch to Compact View (400x360)");
        }
    } else {
        viewStack_->setCurrentIndex(1);
        setFixedSize(FxTheme::kLiteWidth, FxTheme::kLiteHeight);
        if (viewModeBtn_) {
            viewModeBtn_->setIcon(theme.loadSvgIcon("maximize.svg", QSize(16, 16)));
            viewModeBtn_->setToolTip("Switch to Pro View (900x530)");
        }
    }
    update();
}

void FxMainWindow::togglePower() {
    setPower(!poweredOn_);
}

void FxMainWindow::setPower(bool enabled) {
    poweredOn_ = enabled;
    dspEngine_.setPowerEnabled(poweredOn_);
    updatePowerVisuals();
    updateTrayMenu();
}

void FxMainWindow::setLoudnessMode(LoudnessMode mode) {
    dspEngine_.setLoudnessMode(mode);
    const int idx = static_cast<int>(mode);
    if (proVolumeCombo_ && proVolumeCombo_->currentIndex() != idx) {
        proVolumeCombo_->blockSignals(true);
        proVolumeCombo_->setCurrentIndex(idx);
        proVolumeCombo_->blockSignals(false);
    }
    if (liteVolumeCombo_ && liteVolumeCombo_->currentIndex() != idx) {
        liteVolumeCombo_->blockSignals(true);
        liteVolumeCombo_->setCurrentIndex(idx);
        liteVolumeCombo_->blockSignals(false);
    }
    updateTrayMenu();
    savePersistentSettings();
}

void FxMainWindow::updatePowerVisuals() {
    auto& theme = FxTheme::instance();
    const QString svgName = poweredOn_ ? "Button_ON.svg" : "Button_OFF.svg";

    if (proPowerBtn_) {
        proPowerBtn_->setIcon(theme.loadSvgIcon(svgName, QSize(120, 120)));
        proPowerBtn_->setIconSize(QSize(120, 120));
    }
    if (litePowerBtn_) {
        litePowerBtn_->setIcon(theme.loadSvgIcon(svgName, QSize(72, 72)));
        litePowerBtn_->setIconSize(QSize(72, 72));
    }
    if (liteStatusLabel_) {
        liteStatusLabel_->setText(poweredOn_ ? "FXSOUND ON • PROCESSING ACTIVE" : "FXSOUND OFF • BYPASSED");
        liteStatusLabel_->setStyleSheet(
            poweredOn_ ? "color: #EF4B65; background: transparent;" : "color: #7E7E8C; background: transparent;"
        );
    }
    if (visualizer_) {
        visualizer_->setPowerState(poweredOn_);
    }
    if (effectsPanel_) {
        effectsPanel_->setPowerState(poweredOn_);
    }
    if (eqPanel_) {
        eqPanel_->setPowerState(poweredOn_);
    }
    if (trayIcon_) {
        trayIcon_->setIcon(QIcon(theme.loadPngPixmap(poweredOn_ ? "logo-red.png" : "logo-white.png")));
    }
}

void FxMainWindow::refreshPresetCombo() {
    const auto& presets = presetManager_.presets();
    const int curIdx = presetManager_.currentIndex();

    if (proPresetCombo_) {
        proPresetCombo_->blockSignals(true);
        proPresetCombo_->clear();
        for (const auto& p : presets) {
            proPresetCombo_->addItem(QString::fromStdString(p.name));
        }
        proPresetCombo_->setCurrentIndex(curIdx);
        proPresetCombo_->blockSignals(false);
    }
    if (litePresetCombo_) {
        litePresetCombo_->blockSignals(true);
        litePresetCombo_->clear();
        for (const auto& p : presets) {
            litePresetCombo_->addItem(QString::fromStdString(p.name));
        }
        litePresetCombo_->setCurrentIndex(curIdx);
        litePresetCombo_->blockSignals(false);
    }
    updateTrayMenu();
}

void FxMainWindow::refreshDeviceCombo() {
    outputDevices_ = audioEngine_.enumerateOutputDevices();
    if (outputDevices_.empty()) {
        outputDevices_.push_back({"default", "System Default Hardware Output", true});
    }

    const std::string activeId = audioEngine_.targetOutputDevice();
    int selectedIdx = 0;
    for (size_t i = 0; i < outputDevices_.size(); ++i) {
        if (outputDevices_[i].id == activeId || (activeId.empty() && outputDevices_[i].isDefault)) {
            selectedIdx = static_cast<int>(i);
            break;
        }
    }

    if (proDeviceCombo_) {
        proDeviceCombo_->blockSignals(true);
        proDeviceCombo_->clear();
        for (const auto& d : outputDevices_) {
            proDeviceCombo_->addItem(QString::fromStdString(d.description), QString::fromStdString(d.id));
        }
        proDeviceCombo_->setCurrentIndex(selectedIdx);
        proDeviceCombo_->blockSignals(false);
    }
    if (liteDeviceCombo_) {
        liteDeviceCombo_->blockSignals(true);
        liteDeviceCombo_->clear();
        for (const auto& d : outputDevices_) {
            liteDeviceCombo_->addItem(QString::fromStdString(d.description), QString::fromStdString(d.id));
        }
        liteDeviceCombo_->setCurrentIndex(selectedIdx);
        liteDeviceCombo_->blockSignals(false);
    }
    updateTrayMenu();
}

void FxMainWindow::syncUiFromCurrentPreset() {
    const auto& preset = presetManager_.currentPreset();
    dspEngine_.applyPreset(preset);

    if (effectsPanel_) {
        effectsPanel_->loadFromPreset(preset);
    }
    if (eqPanel_) {
        eqPanel_->loadFromPreset(preset);
    }
    if (presetModifiedBadge_) {
        presetModifiedBadge_->setVisible(presetManager_.isCurrentPresetModified());
    }
    if (proPresetCombo_ && proPresetCombo_->currentIndex() != presetManager_.currentIndex()) {
        proPresetCombo_->blockSignals(true);
        proPresetCombo_->setCurrentIndex(presetManager_.currentIndex());
        proPresetCombo_->blockSignals(false);
    }
    if (litePresetCombo_ && litePresetCombo_->currentIndex() != presetManager_.currentIndex()) {
        litePresetCombo_->blockSignals(true);
        litePresetCombo_->setCurrentIndex(presetManager_.currentIndex());
        litePresetCombo_->blockSignals(false);
    }
    updateTrayMenu();
}

void FxMainWindow::onPresetComboChanged(int index) {
    if (presetManager_.selectPreset(index)) {
        syncUiFromCurrentPreset();
    }
}

void FxMainWindow::onDeviceComboChanged(int index) {
    if (index < 0 || index >= static_cast<int>(outputDevices_.size())) {
        return;
    }
    audioEngine_.setTargetOutputDevice(outputDevices_[index].id);

    if (proDeviceCombo_ && proDeviceCombo_->currentIndex() != index) {
        proDeviceCombo_->blockSignals(true);
        proDeviceCombo_->setCurrentIndex(index);
        proDeviceCombo_->blockSignals(false);
    }
    if (liteDeviceCombo_ && liteDeviceCombo_->currentIndex() != index) {
        liteDeviceCombo_->blockSignals(true);
        liteDeviceCombo_->setCurrentIndex(index);
        liteDeviceCombo_->blockSignals(false);
    }
    updateTrayMenu();
}

void FxMainWindow::onSavePresetOverwrite() {
    if (presetManager_.saveCurrentPresetOverwrite()) {
        presetModifiedBadge_->setVisible(false);
    }
}

void FxMainWindow::onSaveNewPreset() {
    auto& theme = FxTheme::instance();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this,
        theme.trKey("Save New Preset"),
        theme.trKey("Enter a name for the new preset"),
        QLineEdit::Normal,
        QString::fromStdString(presetManager_.currentPreset().name) + " Custom",
        &ok
    );
    if (!ok || newName.trimmed().isEmpty()) {
        return;
    }
    std::string err;
    if (presetManager_.saveAsNewPreset(newName.toStdString(), &err)) {
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    } else {
        QMessageBox::warning(this, "FxSound", QString::fromStdString(err));
    }
}

void FxMainWindow::onUndoPresetChanges() {
    if (presetManager_.resetCurrentPresetToFactory()) {
        syncUiFromCurrentPreset();
    }
}

void FxMainWindow::onRenamePreset() {
    auto& theme = FxTheme::instance();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this,
        theme.trKey("Rename Preset"),
        theme.trKey("Enter a new name for the preset"),
        QLineEdit::Normal,
        QString::fromStdString(presetManager_.currentPreset().name),
        &ok
    );
    if (!ok || newName.trimmed().isEmpty()) {
        return;
    }
    std::string err;
    if (presetManager_.renameCurrentPreset(newName.toStdString(), &err)) {
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    } else {
        QMessageBox::warning(this, "FxSound", QString::fromStdString(err));
    }
}

void FxMainWindow::onDeletePreset() {
    auto& theme = FxTheme::instance();
    const auto ans = QMessageBox::question(
        this,
        theme.trKey("Delete Preset"),
        theme.trKey("Are you sure you want to delete the preset?"),
        QMessageBox::Yes | QMessageBox::No
    );
    if (ans != QMessageBox::Yes) {
        return;
    }
    std::string err;
    if (presetManager_.deleteCurrentPreset(&err)) {
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    } else {
        QMessageBox::warning(this, "FxSound", QString::fromStdString(err));
    }
}

void FxMainWindow::onImportPreset() {
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Import Factsoft Preset (.fac)",
        QString(),
        "FxSound Preset Files (*.fac);;All Files (*)"
    );
    if (file.isEmpty()) {
        return;
    }
    std::string err;
    if (presetManager_.importPresetFile(file.toStdString(), &err)) {
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    } else {
        QMessageBox::warning(this, "FxSound Import Error", QString::fromStdString(err));
    }
}

void FxMainWindow::onExportPreset() {
    const QString defaultName = QString::fromStdString(presetManager_.currentPreset().name) + ".fac";
    const QString file = QFileDialog::getSaveFileName(
        this,
        "Export Factsoft Preset (.fac)",
        defaultName,
        "FxSound Preset Files (*.fac)"
    );
    if (file.isEmpty()) {
        return;
    }
    if (presetManager_.exportCurrentPresetFile(file.toStdString())) {
        QMessageBox::information(this, "FxSound", FxTheme::instance().trKey("Presets exported successfully."));
    } else {
        QMessageBox::warning(this, "FxSound", "Failed to export .fac preset file.");
    }
}

void FxMainWindow::openSettingsDialog(int initialTab) {
    FxSettingsDialog dlg(appSettings_, this);
    if (auto* tabs = dlg.findChild<QTabWidget*>()) {
        tabs->setCurrentIndex(initialTab);
    }

    connect(&dlg, &FxSettingsDialog::settingsChanged, this, [this](const AppSettingsState& st) {
        if (appSettings_.bonusPresets != st.bonusPresets) {
            presetManager_.setBonusPresetsEnabled(st.bonusPresets);
            refreshPresetCombo();
            syncUiFromCurrentPreset();
            updateTrayMenu();
        }
        appSettings_ = st;
        FxTheme::instance().setLanguage(appSettings_.languageCode);
        audioEngine_.setAutoSwitchNewDevice(appSettings_.autoSwitchOutput);
        if (effectsPanel_) {
            effectsPanel_->setTooltipsEnabled(!appSettings_.hideHelpTips);
            effectsPanel_->retranslateUi();
        }
        if (eqPanel_) {
            eqPanel_->setTooltipsEnabled(!appSettings_.hideHelpTips);
            eqPanel_->retranslateUi();
        }
        savePersistentSettings();
    });

    connect(&dlg, &FxSettingsDialog::resetFactoryPresetsRequested, this, [this]() {
        presetManager_.resetToFactoryDefaults();
        refreshPresetCombo();
        syncUiFromCurrentPreset();
    });

    dlg.exec();
}

bool FxMainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        // Intercept all QEvent::ToolTip events so Qt never creates a separate Wayland QToolTip wl_surface
        // (which steals pointer focus on Wayland/Hyprland and causes hover flicker).
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!appSettings_.hideHelpTips && widget && isAncestorOf(widget) && !widget->toolTip().isEmpty() && inWindowTooltip_) {
            inWindowTooltip_->setText(widget->toolTip());
            inWindowTooltip_->adjustSize();
            const QPoint widgetBottomLeft = widget->mapTo(this, QPoint(0, widget->height() + 6));
            const int x = std::clamp(widgetBottomLeft.x(), 8, std::max(8, width() - inWindowTooltip_->width() - 8));
            int y = widgetBottomLeft.y();
            if (y + inWindowTooltip_->height() > height() - 8) {
                y = std::max(8, widget->mapTo(this, QPoint(0, 0)).y() - inWindowTooltip_->height() - 6);
            }
            inWindowTooltip_->move(x, y);
            inWindowTooltip_->show();
            inWindowTooltip_->raise();
        } else if (inWindowTooltip_) {
            inWindowTooltip_->hide();
        }
        return true;
    }
    if (event->type() == QEvent::Leave ||
        event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::Wheel) {
        if (inWindowTooltip_ && inWindowTooltip_->isVisible()) {
            inWindowTooltip_->hide();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void FxMainWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setPen(Qt::NoPen);
    p.setBrush(FxTheme::kBgWindow);
    p.drawRect(rect().adjusted(1, 1, -1, -1));
}

void FxMainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().y() <= 44.0) {
        if (windowHandle() && windowHandle()->startSystemMove()) {
            event->accept();
            return;
        }
        draggingWindow_ = true;
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void FxMainWindow::mouseMoveEvent(QMouseEvent* event) {
    if (draggingWindow_ && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - dragOffset_);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void FxMainWindow::mouseReleaseEvent(QMouseEvent* event) {
    draggingWindow_ = false;
    QMainWindow::mouseReleaseEvent(event);
}

void FxMainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (visualizer_) {
        visualizer_->setWindowVisibleState(true);
    }
    updateTrayMenu();
}

void FxMainWindow::hideEvent(QHideEvent* event) {
    QMainWindow::hideEvent(event);
    // Zero-CPU background sleep for spectrum visualizer when minimized/hidden to tray!
    if (visualizer_) {
        visualizer_->setWindowVisibleState(false);
    }
    updateTrayMenu();
}

void FxMainWindow::closeEvent(QCloseEvent* event) {
    if (!forceQuit_ && trayIcon_ && trayIcon_->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    savePersistentSettings();
    QMainWindow::closeEvent(event);
}

} // namespace fxsound
