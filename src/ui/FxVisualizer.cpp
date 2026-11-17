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

    displayBands_.fill(0.18f);
    peakHolds_.fill(0.22f);

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
    idlePhase_ += 0.09f;
    const auto liveBands = dspEngine_.getSpectrumBands();

    bool hasSignal = false;
    for (float v : liveBands) {
        if (v > 0.04f) {
            hasSignal = true;
            break;
        }
    }

    for (size_t i = 0; i < DfxDspEngine::kSpectrumBandCount; ++i) {
        float target = liveBands[i];
        if (!hasSignal && poweredOn_) {
            // Subtle ambient EQ breathing curve when idle & powered on
            const float eqDb = dspEngine_.evaluateEqResponseDb(60.0f * std::pow(1.32f, static_cast<float>(i)));
            const float eqNorm = std::clamp((eqDb + 12.0f) / 24.0f, 0.12f, 0.85f);
            const float wave = 0.06f * std::sin(idlePhase_ + static_cast<float>(i) * 0.42f)
                             + 0.04f * std::cos(idlePhase_ * 0.7f - static_cast<float>(i) * 0.25f);
            target = std::clamp(0.22f + 0.36f * eqNorm + wave, 0.10f, 0.78f);
        }

        displayBands_[i] = 0.35f * displayBands_[i] + 0.65f * target;
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

    constexpr int columns = 110;
    const float inset = width() * 0.06f;
    const float step = (width() - inset * 2) / columns;
    const float center = height() * 0.5f;
    const float maximum = height() * 0.78f;
    p.setPen(Qt::NoPen);
    for (int col = 0; col < columns; ++col) {
        const float pos = float(col) / (columns - 1) * (DfxDspEngine::kSpectrumBandCount - 1);
        const auto a = static_cast<size_t>(pos);
        const auto b = std::min(a + 1, DfxDspEngine::kSpectrumBandCount - 1);
        const float fraction = pos - a;
        const float level = poweredOn_ ? displayBands_[a] * (1 - fraction) + displayBands_[b] * fraction : 0;
        const float barHeight = std::clamp(level * maximum, 2.0f, maximum);
        p.setBrush(poweredOn_ ? FxTheme::kCrimsonPrimary : FxTheme::kDisabledGrey);
        p.drawRect(QRectF(inset + col * step, center - barHeight / 2, step * 0.58f, barHeight));
    }
}

} // namespace fxsound
