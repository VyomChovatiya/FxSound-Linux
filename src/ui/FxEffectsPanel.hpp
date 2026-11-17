#pragma once

#include "../dsp/FacPreset.hpp"

#include <QLabel>
#include <QPixmap>
#include <QWidget>
#include <array>

namespace fxsound {

class FxHorizontalEffectSlider : public QWidget {
    Q_OBJECT

public:
    explicit FxHorizontalEffectSlider(QWidget* parent = nullptr);

    int value() const { return value_; }
    void setValue(int val, bool notify = false);

    void setPowerState(bool poweredOn);

signals:
    void valueChanged(int newValue);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateFromMouseX(int mouseX);

    int value_ = 5; // 0..9
    bool poweredOn_ = true;
    QPixmap thumbColor_;
    QPixmap thumbBw_;
};

class FxEffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit FxEffectsPanel(QWidget* parent = nullptr);

    void loadFromPreset(const FacPreset& preset);
    void setPowerState(bool poweredOn);
    void setTooltipsEnabled(bool enabled);
    void retranslateUi();

signals:
    void effectValueChanged(EffectIndex effectIdx, int newValue);

private:
    struct RowWidgets {
        EffectIndex effectIdx;
        QString englishTitle;
        QString englishTooltip;
        QLabel* titleLabel = nullptr;
        FxHorizontalEffectSlider* slider = nullptr;
        QLabel* valueLabel = nullptr;
    };

    std::array<RowWidgets, 5> rows_;
    bool poweredOn_ = true;
    bool tooltipsEnabled_ = true;
};

} // namespace fxsound
