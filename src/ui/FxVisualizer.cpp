#include "FxVisualizer.hpp"
#include "FxTheme.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <cmath>

namespace fxsound {

FxVisualizer::FxVisualizer(DfxDspEngine& dspEngine, QWidget* parent)
    : QWidget(parent), dspEngine_(dspEngine) {
    setMinimumHeight(88);
    setMaximumHeight(96);

    auto& theme = FxTheme::instance();
    eyeOpenPix_   = theme.loadPngPixmap("icons8-eye-48.png");
    eyeClosedPix_ = theme.loadPngPixmap("icons8-closed-eye-48.png");

    displayBands_.fill(0.0f);
    peakHolds_.fill(0.0f);

    eyeToggleBtn_ = new QPushButton(this);
    eyeToggleBtn_->setFixedSize(28, 28);
    eyeToggleBtn_->setCursor(Qt::PointingHandCursor);
    eyeToggleBtn_->setStyleSheet(
        "QPushButton { background: rgba(28,28,38,180); border: 1px solid #363648; border-radius: 14px; }"
        "QPushButton:hover { border: 1px solid #E33250; background: rgba(45,45,60,220); }"
    );
    eyeToggleBtn_->setToolTip("Toggle real-time spectrum visualizer");
    updateEyeButtonIcon();

    connect(eyeToggleBtn_, &QPushButton::clicked, this, [this]() {
        setUserVisualizerEnabled(!userEnabled_);
        emit visualizerToggled(userEnabled_);
    });

    frameTimer_.setInterval(33); // ~30 FPS
    connect(&frameTimer_, &QTimer::timeout, this, &FxVisualizer::onFrameTick);
    updateActiveState();
}

void FxVisualizer::setPowerState(bool poweredOn) {
    poweredOn_ = poweredOn;
    updateActiveState();
    update();
}

void FxVisualizer::setUserVisualizerEnabled(bool enabled) {
    userEnabled_ = enabled;
    updateEyeButtonIcon();
    updateActiveState();
    update();
}

void FxVisualizer::setWindowVisibleState(bool visible) {
    windowVisible_ = visible;
    updateActiveState();
}

void FxVisualizer::updateEyeButtonIcon() {
    const QPixmap& pix = userEnabled_ ? eyeOpenPix_ : eyeClosedPix_;
    if (!pix.isNull()) {
        eyeToggleBtn_->setIcon(QIcon(pix));
        eyeToggleBtn_->setIconSize(QSize(16, 16));
    }
}

void FxVisualizer::updateActiveState() {
    const bool active = poweredOn_ && userEnabled_ && windowVisible_;
    dspEngine_.setVisualizerActive(active);
    if (active) {
        if (!frameTimer_.isActive()) {
            frameTimer_.start();
        }
    } else {
        frameTimer_.stop();
    }
}

void FxVisualizer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (eyeToggleBtn_) {
        eyeToggleBtn_->move(width() - eyeToggleBtn_->width() - 12, 8);
    }
}

void FxVisualizer::onFrameTick() {
    const auto liveBands = dspEngine_.getSpectrumBands();
    for (size_t i = 0; i < DfxDspEngine::kSpectrumBandCount; ++i) {
        const float target = std::pow(std::clamp((liveBands[i] - 0.15f) / 0.7f, 0.0f, 1.0f), 1.25f);
        const float speed = target > displayBands_[i] ? 0.72f : 0.16f;
        displayBands_[i] += speed * (target - displayBands_[i]);
        for (size_t age = history_[i].size() - 1; age > 0; --age)
            history_[i][age] = history_[i][age - 1];
        history_[i][0] = displayBands_[i];
        if (displayBands_[i] > peakHolds_[i]) {
            peakHolds_[i] = displayBands_[i];
        } else {
            peakHolds_[i] = std::max(0.05f, peakHolds_[i] - 0.015f);
        }
    }
    update();
}

void FxVisualizer::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(4, 2, -4, -2);

    // 1. Sleek deep obsidian studio backdrop with subtle border
    QLinearGradient bgGrad(r.topLeft(), r.bottomLeft());
    bgGrad.setColorAt(0.0, QColor("#0f0f0f"));
    bgGrad.setColorAt(0.65, QColor("#0f0f0f"));
    bgGrad.setColorAt(1.0, QColor("#0f0f0f"));
    p.setPen(QPen(QColor("#0f0f0f"), 1.0));
    p.setBrush(bgGrad);
    p.drawRoundedRect(r, 8.0, 8.0);

    // Clip inside rounded rect
    QPainterPath clipPath;
    clipPath.addRoundedRect(r.adjusted(1, 1, -1, -1), 7.0, 7.0);
    p.setClipPath(clipPath);

    if (!userEnabled_) return;

    // Each band emits a short mirrored history: new transients appear in the
    // middle and travel outward, following FxSound's original visual rhythm.
    constexpr int barsPerBand = 7;
    constexpr int columns = DfxDspEngine::kSpectrumBandCount * barsPerBand;
    const float inset = width() * 0.045f;
    const float step = (width() - inset * 2) / columns;
    const float center = height() * 0.55f;
    const float maximum = height() * 0.80f;
    p.setPen(Qt::NoPen);
    for (int col = 0; col < columns; ++col) {
        const size_t band = col / barsPerBand;
        const int age = std::abs(col % barsPerBand - barsPerBand / 2);
        const float level = poweredOn_ ? history_[band][age] : 0.0f;
        const float barHeight = std::max(2.0f, level * maximum);
        const float x = inset + col * step;
        const float barWidth = std::max(1.5f, step * 0.55f);
        const QRectF bar(x, center - barHeight / 2, barWidth, barHeight);
        if (level > 0.1f) {
            QColor halo = FxTheme::kCrimsonPrimary;
            halo.setAlphaF(level * 0.13f);
            p.setBrush(halo);
            p.drawRoundedRect(bar.adjusted(-2, -2, 2, 2), 3, 3);
        }
        QLinearGradient gradient(0, center - maximum / 2, 0, center + maximum / 2);
        gradient.setColorAt(0, QColor("#ffb0c1"));
        gradient.setColorAt(0.30, QColor("#ff567c"));
        gradient.setColorAt(0.55, FxTheme::kCrimsonPrimary);
        gradient.setColorAt(1, QColor("#811d39"));
        p.setBrush(poweredOn_ ? QBrush(gradient) : QBrush(FxTheme::kDisabledGrey));
        p.drawRoundedRect(bar, 0.8, 0.8);
    }
}

} // namespace fxsound
