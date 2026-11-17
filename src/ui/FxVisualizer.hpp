#pragma once

#include "../dsp/DfxDspEngine.hpp"

#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <array>

namespace fxsound {

class FxVisualizer : public QWidget {
    Q_OBJECT

public:
    explicit FxVisualizer(DfxDspEngine& dspEngine, QWidget* parent = nullptr);

    void setPowerState(bool poweredOn);
    void setUserVisualizerEnabled(bool enabled);
    bool isUserVisualizerEnabled() const { return userEnabled_; }

    // Called when main window hides to tray or restores so timer & DSP spectrum analyzer sleep with 0% CPU
    void setWindowVisibleState(bool visible);

signals:
    void visualizerToggled(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onFrameTick();

private:
    void updateActiveState();
    void updateEyeButtonIcon();

    DfxDspEngine& dspEngine_;
    QTimer frameTimer_;
    QPushButton* eyeToggleBtn_ = nullptr;

    QPixmap eyeOpenPix_;
    QPixmap eyeClosedPix_;

    bool poweredOn_ = true;
    bool userEnabled_ = true;
    bool windowVisible_ = true;
    float idlePhase_ = 0.0f;

    std::array<float, DfxDspEngine::kSpectrumBandCount> displayBands_{};
    std::array<float, DfxDspEngine::kSpectrumBandCount> peakHolds_{};
};

} // namespace fxsound
