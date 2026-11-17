#pragma once

#include "../dsp/FacPreset.hpp"

#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>
#include <array>

namespace fxsound {

class FxVerticalEqSlider : public QWidget {
    Q_OBJECT

public:
    explicit FxVerticalEqSlider(QWidget* parent = nullptr);

    float boostDb() const { return boostDb_; }
    void setBoostDb(float db, bool notify = false);
    void setPowerState(bool poweredOn);

signals:
    void boostChanged(float newBoostDb);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateFromMouseY(int mouseY);

    float boostDb_ = 0.0f; // -12.0f .. +12.0f
    bool poweredOn_ = true;
    QPixmap thumbColor_;
    QPixmap thumbBw_;
};

class FxFreqWheelWidget : public QWidget {
    Q_OBJECT

public:
    explicit FxFreqWheelWidget(int bandUiIndex, QWidget* parent = nullptr);

    void setFrequencyHz(float hz);
    void setPowerState(bool poweredOn);

signals:
    void stepRequested(int direction); // -1 or +1

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    int bandUiIndex_ = 0;
    float frequencyHz_ = 115;
    QString freqText_ = "115";
    bool poweredOn_ = true;
    QPixmap prevPix_;
    QPixmap prevBwPix_;
    QPixmap nextPix_;
    QPixmap nextBwPix_;
};

class FxEqualizerPanel : public QWidget {
    Q_OBJECT

public:
    explicit FxEqualizerPanel(QWidget* parent = nullptr);

    void loadFromPreset(const FacPreset& preset);
    void setPowerState(bool poweredOn);
    void setTooltipsEnabled(bool enabled);
    void retranslateUi();

signals:
    void bandBoostChanged(int uiBandIdx, float newBoostDb);
    void bandFreqStepRequested(int uiBandIdx, int direction);
    void resetRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct BandColumn {
        int uiIndex = 0;
        QLabel* dbLabel = nullptr;
        FxVerticalEqSlider* slider = nullptr;
        FxFreqWheelWidget* freqWheel = nullptr;
    };

    std::array<BandColumn, 9> columns_{};
    QPushButton* resetEqBtn_ = nullptr;
    bool poweredOn_ = true;
    bool tooltipsEnabled_ = true;
};

} // namespace fxsound
