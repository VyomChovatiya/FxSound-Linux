#include "FxEffectsPanel.hpp"
#include "FxTheme.hpp"

#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace fxsound {

FxHorizontalEffectSlider::FxHorizontalEffectSlider(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(22);
    setMinimumWidth(140);
    setCursor(Qt::PointingHandCursor);

    auto& theme = FxTheme::instance();
    thumbColor_ = theme.loadSvgPixmap("Slider_Thumb.svg", QSize(18, 18));
    thumbBw_    = theme.loadSvgPixmap("Slider_Thumb_bw.svg", QSize(18, 18));
}

void FxHorizontalEffectSlider::setValue(int val, bool notify) {
    const int clamped = std::clamp(val, 0, 9);
    const bool changed = (value_ != clamped);
    value_ = clamped;
    update();
    if (changed && notify) {
        emit valueChanged(value_);
    }
}

void FxHorizontalEffectSlider::setPowerState(bool poweredOn) {
    if (poweredOn_ != poweredOn) {
        poweredOn_ = poweredOn;
        update();
    }
}

void FxHorizontalEffectSlider::updateFromMouseX(int mouseX) {
    const float pad = 11.0f;
    const float trackW = std::max(10.0f, static_cast<float>(width()) - 2.0f * pad);
    const float norm = std::clamp((static_cast<float>(mouseX) - pad) / trackW, 0.0f, 1.0f);
    const int step = static_cast<int>(std::lround(norm * 9.0f));
    setValue(step, true);
}

void FxHorizontalEffectSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        updateFromMouseX(static_cast<int>(event->position().x()));
    }
}

void FxHorizontalEffectSlider::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        updateFromMouseX(static_cast<int>(event->position().x()));
    }
}

void FxHorizontalEffectSlider::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta > 0) {
        setValue(value_ + 1, true);
    } else if (delta < 0) {
        setValue(value_ - 1, true);
    }
    event->accept();
}

void FxHorizontalEffectSlider::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const float pad = 11.0f;
    const float trackH = 5.5f;
    // Positioned slightly above vertical center so thumb & tick dots have generous bottom clearance
    const float cy = 9.0f;
    const float trackW = std::max(10.0f, static_cast<float>(width()) - 2.0f * pad);
    const float norm = static_cast<float>(value_) / 9.0f;
    const float thumbX = pad + norm * trackW;

    // Background groove
    p.setPen(Qt::NoPen);
    p.setBrush(FxTheme::kTrackBackground);
    p.drawRoundedRect(QRectF(pad, cy - trackH * 0.5f, trackW, trackH), 2.75, 2.75);

    // Active filled groove
    if (norm > 0.001f) {
        QLinearGradient grad(QPointF(pad, cy), QPointF(thumbX, cy));
        if (poweredOn_) {
            grad.setColorAt(0.0, FxTheme::kCrimsonPrimary);
            grad.setColorAt(1.0, FxTheme::kCrimsonHighlight);
        } else {
            grad.setColorAt(0.0, QColor(0x6E, 0x6E, 0x7A));
            grad.setColorAt(1.0, QColor(0x90, 0x90, 0x9E));
        }
        p.setBrush(grad);
        p.drawRoundedRect(QRectF(pad, cy - trackH * 0.5f, thumbX - pad, trackH), 2.75, 2.75);
    }

    // Thumb SVG
    const QPixmap& thumb = poweredOn_ ? thumbColor_ : thumbBw_;
    if (!thumb.isNull()) {
        p.drawPixmap(
            static_cast<int>(std::lround(thumbX - thumb.deviceIndependentSize().width() * 0.5f)),
            static_cast<int>(std::lround(cy - thumb.deviceIndependentSize().height() * 0.5f)),
            thumb
        );
    } else {
        p.setBrush(poweredOn_ ? FxTheme::kCrimsonHighlight : FxTheme::kDisabledGrey);
        p.drawEllipse(QPointF(thumbX, cy), 7.5f, 7.5f);
    }
}

FxEffectsPanel::FxEffectsPanel(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("effectsCard");
    setStyleSheet("QWidget#effectsCard { background: #0f0f0f; border-radius: 8px; }");
    // Align top and bottom margins with FxEqualizerPanel (4px) so both columns line up flush
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(0);

    const std::array<RowWidgets, 5> defs = {{
        {EffectIndex::Clarity,       "Clarity",        "Enhances and elevates high end\nfidelity and presence", nullptr, nullptr, nullptr},
        {EffectIndex::Ambience,      "Ambience",       "Thickens and smooths audio\nwith controlled reverberation", nullptr, nullptr, nullptr},
        {EffectIndex::SurroundSound, "Surround Sound", "Widens the left-right balance\nfor expansive, wide sound", nullptr, nullptr, nullptr},
        {EffectIndex::DynamicBoost,  "Dynamic Boost",  "Increases overall volume and balance\nwith responsive processing", nullptr, nullptr, nullptr},
        {EffectIndex::BassBoost,     "Bass Boost",     "Boosts low end for full,\nimpactful response", nullptr, nullptr, nullptr},
    }};
    rows_ = defs;

    auto& theme = FxTheme::instance();

    for (size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        auto* rowWidget = new QWidget(this);
        rowWidget->setStyleSheet(
            "background: transparent; border: none;"
        );
        auto* rowLayout = new QVBoxLayout(rowWidget);
        // Compact top margin (5px) and generous bottom margin (8px) so title & slider sit centered/higher
        rowLayout->setContentsMargins(14, 5, 14, 7);
        rowLayout->setSpacing(2);

        auto* topBar = new QHBoxLayout();
        topBar->setContentsMargins(2, 0, 2, 0);

        row.titleLabel = new QLabel(theme.trKey(row.englishTitle), rowWidget);
        row.titleLabel->setFont(theme.uiFont(10, true));
        row.titleLabel->setStyleSheet("border: none; background: transparent; color: #B1B1B1;");

        row.valueLabel = new QLabel("5", rowWidget);
        row.valueLabel->setFont(theme.numericFont(10, true));
        row.valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.valueLabel->setFixedWidth(26);
        row.valueLabel->setStyleSheet("border: none; background: transparent; color: #EF4B65;");

        topBar->addWidget(row.titleLabel);
        topBar->addStretch(1);
        row.valueLabel->hide();

        row.slider = new FxHorizontalEffectSlider(rowWidget);
        row.slider->setStyleSheet("border: none; background: transparent;");

        connect(row.slider, &FxHorizontalEffectSlider::valueChanged, this, [this, i](int newVal) {
            rows_[i].valueLabel->setText(QString::number(newVal));
            emit effectValueChanged(rows_[i].effectIdx, newVal);
        });

        rowLayout->addLayout(topBar);
        rowLayout->addWidget(row.slider);
        mainLayout->addWidget(rowWidget, 1);
    }

    setTooltipsEnabled(true);
}

void FxEffectsPanel::loadFromPreset(const FacPreset& preset) {
    for (auto& row : rows_) {
        const int val = preset.getEffectUiValue(row.effectIdx);
        row.slider->setValue(val, false);
        row.valueLabel->setText(QString::number(val));
    }
}

void FxEffectsPanel::setPowerState(bool poweredOn) {
    poweredOn_ = poweredOn;
    for (auto& row : rows_) {
        row.slider->setPowerState(poweredOn);
        row.valueLabel->setStyleSheet(
            poweredOn
                ? "border: none; background: transparent; color: #EF4B65;"
                : "border: none; background: transparent; color: #8A8A98;"
        );
    }
}

void FxEffectsPanel::setTooltipsEnabled(bool enabled) {
    tooltipsEnabled_ = enabled;
    auto& theme = FxTheme::instance();
    for (auto& row : rows_) {
        const QString tip = enabled ? theme.trKey(row.englishTooltip) : QString();
        row.titleLabel->setToolTip(tip);
        row.slider->setToolTip(tip);
    }
}

void FxEffectsPanel::retranslateUi() {
    auto& theme = FxTheme::instance();
    for (auto& row : rows_) {
        row.titleLabel->setText(theme.trKey(row.englishTitle));
    }
    setTooltipsEnabled(tooltipsEnabled_);
}

} // namespace fxsound
