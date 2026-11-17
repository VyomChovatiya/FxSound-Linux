#include "FxEqualizerPanel.hpp"
#include "FxTheme.hpp"

#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace fxsound {

namespace {

QString formatDbString(float db) {
    const int rounded = static_cast<int>(std::lround(db));
    if (rounded > 0) {
        return QString("+%1").arg(rounded);
    }
    return QString::number(rounded);
}

} // namespace

FxVerticalEqSlider::FxVerticalEqSlider(QWidget* parent)
    : QWidget(parent) {
    setMinimumWidth(44);
    setMinimumHeight(110);
    setCursor(Qt::PointingHandCursor);

    auto& theme = FxTheme::instance();
    thumbColor_ = theme.loadSvgPixmap("Slider_Thumb.svg", QSize(18, 18));
    thumbBw_    = theme.loadSvgPixmap("Slider_Thumb_bw.svg", QSize(18, 18));
}

void FxVerticalEqSlider::setBoostDb(float db, bool notify) {
    const float clamped = std::clamp(std::round(db * 2.0f) * 0.5f, -12.0f, 12.0f);
    const bool changed = (std::fabs(boostDb_ - clamped) > 0.05f);
    boostDb_ = clamped;
    update();
    if (changed && notify) {
        emit boostChanged(boostDb_);
    }
}

void FxVerticalEqSlider::setPowerState(bool poweredOn) {
    if (poweredOn_ != poweredOn) {
        poweredOn_ = poweredOn;
        update();
    }
}

void FxVerticalEqSlider::updateFromMouseY(int mouseY) {
    const float pad = 14.0f;
    const float trackH = std::max(20.0f, static_cast<float>(height()) - 2.0f * pad);
    const float norm = 1.0f - std::clamp((static_cast<float>(mouseY) - pad) / trackH, 0.0f, 1.0f);
    const float db = (norm * 24.0f) - 12.0f;
    setBoostDb(std::round(db), true);
}

void FxVerticalEqSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        updateFromMouseY(static_cast<int>(event->position().y()));
    }
}

void FxVerticalEqSlider::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        updateFromMouseY(static_cast<int>(event->position().y()));
    }
}

void FxVerticalEqSlider::mouseDoubleClickEvent(QMouseEvent* /*event*/) {
    setBoostDb(0.0f, true);
}

void FxVerticalEqSlider::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta > 0) {
        setBoostDb(boostDb_ + 1.0f, true);
    } else if (delta < 0) {
        setBoostDb(boostDb_ - 1.0f, true);
    }
    event->accept();
}

void FxVerticalEqSlider::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const float pad = 14.0f;
    const float cx = static_cast<float>(width()) * 0.5f;

    const float trackH = std::max(20.0f, static_cast<float>(height()) - 2.0f * pad);


    const float norm = (boostDb_ + 12.0f) / 24.0f; // 0.0 (-12 dB) .. 1.0 (+12 dB)
    const float thumbY = pad + (1.0f - norm) * trackH;

    p.setPen(QPen(QColor("#64404a"), 1, Qt::DashLine));
    p.drawLine(QPointF(cx, pad), QPointF(cx, pad + trackH));
    p.setPen(QPen(poweredOn_ ? FxTheme::kCrimsonPrimary : FxTheme::kDisabledGrey, 5));
    p.setBrush(QColor("#090909"));
    p.drawEllipse(QPointF(cx, thumbY), 7, 7);

}

FxFreqWheelWidget::FxFreqWheelWidget(int bandUiIndex, QWidget* parent)
    : QWidget(parent), bandUiIndex_(bandUiIndex) {
    setFixedHeight(70);
    setMinimumWidth(48);
    setCursor(Qt::PointingHandCursor);

    auto& theme = FxTheme::instance();
    prevPix_   = theme.loadSvgPixmap("arrow_prev.svg", QSize(7, 11));
    prevBwPix_ = theme.loadSvgPixmap("arrow_prev_bw.svg", QSize(7, 11));
    nextPix_   = theme.loadSvgPixmap("arrow_next.svg", QSize(7, 11));
    nextBwPix_ = theme.loadSvgPixmap("arrow_next_bw.svg", QSize(7, 11));
}

void FxFreqWheelWidget::setFrequencyHz(float hz) {
    freqText_ = QString::fromStdString(formatFrequencyLabel(hz));
    frequencyHz_ = hz;
    update();
}

void FxFreqWheelWidget::setPowerState(bool poweredOn) {
    poweredOn_ = poweredOn;
    update();
}

void FxFreqWheelWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    p.setFont(FxTheme::instance().numericFont(8, true));
    p.setPen(QColor("#b1b1b1"));
    p.drawText(QRect(0, 0, width(), 22), Qt::AlignCenter, freqText_);
    const QRectF dial(width() / 2.0 - 15, 29, 30, 30);
    const auto& bounds = getUiEqBandBounds()[bandUiIndex_];
    const float fraction = std::clamp((frequencyHz_ - bounds.minHz) / (bounds.maxHz - bounds.minHz), 0.0f, 1.0f);
    p.setPen(QPen(QColor("#39161c"), 5, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(dial, -45 * 16, 270 * 16);
    p.setPen(QPen(poweredOn_ ? FxTheme::kCrimsonPrimary : FxTheme::kDisabledGrey, 5, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(dial, 225 * 16, -int(fraction * 270 * 16));
    const double angle = (225 - fraction * 270) * 3.141592653589793 / 180;
    p.setBrush(QColor("#090909"));
    p.drawEllipse(dial.center() + QPointF(15 * std::cos(angle), -15 * std::sin(angle)), 3.5, 3.5);

}

void FxFreqWheelWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const float mx = static_cast<float>(event->position().x());
        if (mx < static_cast<float>(width()) * 0.4f) {
            emit stepRequested(-1);
        } else if (mx > static_cast<float>(width()) * 0.6f) {
            emit stepRequested(1);
        }
    }
    event->accept();
}

void FxFreqWheelWidget::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta > 0) {
        emit stepRequested(1);
    } else if (delta < 0) {
        emit stepRequested(-1);
    }
    event->accept();
}

FxEqualizerPanel::FxEqualizerPanel(QWidget* parent)
    : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(14, 10, 14, 10);
    outerLayout->setSpacing(4);

    auto& theme = FxTheme::instance();

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addStretch(1);
    resetEqBtn_ = new QPushButton(this);
    resetEqBtn_->setObjectName("resetCurrentPresetButton");
    resetEqBtn_->setAccessibleName("Reset current preset to default");
    resetEqBtn_->setFixedSize(28, 28);
    resetEqBtn_->setIcon(theme.loadSvgIcon("reset.svg", QSize(20, 20)));
    resetEqBtn_->setIconSize(QSize(20, 20));
    resetEqBtn_->setCursor(Qt::PointingHandCursor);
    resetEqBtn_->setToolTip("reset to default");
    resetEqBtn_->setStyleSheet(
        "QPushButton { background: transparent; border: 1px solid transparent; border-radius: 14px; padding: 0; }"
        "QPushButton:hover, QPushButton:focus { background: #39161C; border-color: #E33250; }"
        "QPushButton:pressed { background: #551D29; }"
    );
    connect(resetEqBtn_, &QPushButton::clicked, this, &FxEqualizerPanel::resetRequested);
    headerLayout->addWidget(resetEqBtn_);
    outerLayout->addLayout(headerLayout);

    // 9 vertical band columns + left dB scale column
    auto* bandsLayout = new QHBoxLayout();
    bandsLayout->setContentsMargins(2, 2, 2, 2);
    bandsLayout->setSpacing(2);

    // Left dB axis labels (+12, +6, 0, -6, -12)
    auto* scaleCol = new QVBoxLayout();
    scaleCol->setContentsMargins(0, 22, 4, 26);
    for (const char* mark : {"+12", "+6", "0 dB", "-6", "-12"}) {
        auto* lbl = new QLabel(mark, this);
        lbl->setFont(theme.numericFont(8, false));
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setStyleSheet("color: #7D7D8E; background: transparent;");
        scaleCol->addWidget(lbl);
    }
    while (auto* item = scaleCol->takeAt(0)) { delete item->widget(); delete item; }
    delete scaleCol;

    const auto& bounds = getUiEqBandBounds();

    for (int i = 0; i < 9; ++i) {
        auto& col = columns_[i];
        col.uiIndex = i;

        auto* colWidget = new QWidget(this);
        colWidget->setStyleSheet("background: transparent;");
        auto* colLayout = new QVBoxLayout(colWidget);
        colLayout->setContentsMargins(0, 0, 0, 0);
        colLayout->setSpacing(2);

        col.dbLabel = new QLabel("0", colWidget);
        col.dbLabel->setFont(theme.numericFont(9, true));
        col.dbLabel->setAlignment(Qt::AlignCenter);
        col.dbLabel->setStyleSheet("color: #EF4B65; background: transparent;");

        col.slider = new FxVerticalEqSlider(colWidget);
        col.freqWheel = new FxFreqWheelWidget(i, colWidget);
        col.freqWheel->setFrequencyHz(bounds[i].defaultHz);

        connect(col.slider, &FxVerticalEqSlider::boostChanged, this, [this, i](float newDb) {
            columns_[i].dbLabel->setText(formatDbString(newDb));
            update();
            emit bandBoostChanged(i, newDb);
        });

        connect(col.freqWheel, &FxFreqWheelWidget::stepRequested, this, [this, i](int dir) {
            emit bandFreqStepRequested(i, dir);
        });

        col.dbLabel->hide();
        colLayout->addWidget(col.slider, 1);
        colLayout->addWidget(col.freqWheel);

        bandsLayout->addWidget(colWidget, 1);
    }

    outerLayout->addLayout(bandsLayout, 1);
    setTooltipsEnabled(true);
}

void FxEqualizerPanel::loadFromPreset(const FacPreset& preset) {
    for (int i = 0; i < 9; ++i) {
        const auto& band = preset.getUiEqBand(i);
        columns_[i].slider->setBoostDb(band.boostDb, false);
        columns_[i].dbLabel->setText(formatDbString(band.boostDb));
        columns_[i].freqWheel->setFrequencyHz(band.centerFreqHz);
    }
    update();
}

void FxEqualizerPanel::setPowerState(bool poweredOn) {
    poweredOn_ = poweredOn;
    for (auto& col : columns_) {
        col.slider->setPowerState(poweredOn);
        col.freqWheel->setPowerState(poweredOn);
        col.dbLabel->setStyleSheet(
            poweredOn
                ? "color: #EF4B65; background: transparent;"
                : "color: #8A8A98; background: transparent;"
        );
    }
    update();
}

void FxEqualizerPanel::setTooltipsEnabled(bool enabled) {
    tooltipsEnabled_ = enabled;
    const auto& bounds = getUiEqBandBounds();
    for (int i = 0; i < 9; ++i) {
        const QString sliderTip = enabled
            ? QString("%1\n%2").arg(bounds[i].tooltipRange, bounds[i].tooltipDescription)
            : QString();
        const QString wheelTip = enabled
            ? QString("Adjust center frequency (%1)\nScroll mouse wheel or click arrows").arg(bounds[i].tooltipRange)
            : QString();
        columns_[i].slider->setToolTip(sliderTip);
        columns_[i].freqWheel->setToolTip(wheelTip);
    }
}

void FxEqualizerPanel::retranslateUi() {
    setTooltipsEnabled(tooltipsEnabled_);
}

void FxEqualizerPanel::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Card background
    const QRectF cardRect = rect().adjusted(4, 4, -4, -4);
    p.setPen(QPen(QColor("#0f0f0f"), 1.0));
    p.setBrush(QColor("#0f0f0f"));
    p.drawRoundedRect(cardRect, 8.0, 8.0);

    // Draw subtle spline curve connecting the 9 EQ thumbs
    QVector<QPointF> pts;
    pts.reserve(9);
    for (int i = 0; i < 9; ++i) {
        auto* sl = columns_[i].slider;
        if (!sl) continue;
        const QPoint topLeft = sl->mapTo(this, QPoint(0, 0));
        const float pad = 14.0f;
        const float trackH = std::max(20.0f, static_cast<float>(sl->height()) - 2.0f * pad);
        const float norm = (sl->boostDb() + 12.0f) / 24.0f;
        const float x = static_cast<float>(topLeft.x()) + static_cast<float>(sl->width()) * 0.5f;
        const float y = static_cast<float>(topLeft.y()) + pad + (1.0f - norm) * trackH;
        pts.push_back(QPointF(x, y));
    }

    if (pts.size() == 9) {
        QPainterPath path;
        path.moveTo(pts[0]);
        for (int i = 0; i < pts.size() - 1; ++i) {
            path.lineTo(pts[i + 1]);
        }
        QColor curveColor = poweredOn_ ? QColor(0xE3, 0x32, 0x50) : QColor(0x80, 0x80, 0x90, 70);
        p.setPen(QPen(curveColor, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        QPainterPath fill = path;
        const auto* slider = columns_[0].slider;
        const float bottom = slider->mapTo(this, QPoint(0, slider->height() - 14)).y();
        fill.lineTo(pts.back().x(), bottom);
        fill.lineTo(pts.front().x(), bottom);
        fill.closeSubpath();
        QLinearGradient shade(0, pts.front().y(), 0, bottom);
        shade.setColorAt(0, QColor(227, 50, 80, 40));
        shade.setColorAt(1, QColor(227, 50, 80, 0));
        p.fillPath(fill, shade);
    }
}

} // namespace fxsound
