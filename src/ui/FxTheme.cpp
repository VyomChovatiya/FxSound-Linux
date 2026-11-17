#include "FxTheme.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QEnterEvent>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace fxsound {

namespace {

class FxComboPopupOverlay : public QWidget {
public:
    static constexpr int kRowHeight = 28;
    static constexpr int kPadY = 4;

    FxComboPopupOverlay(FxComboBox* combo, QWidget* topWindow)
        : QWidget(topWindow), combo_(combo) {
        setGeometry(topWindow->rect());
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_InputMethodEnabled, false);

        hoveredRow_ = combo_->currentIndex();
        recomputeCardGeometry();
    }

    void recomputeCardGeometry() {
        QWidget* topWin = parentWidget();
        if (!topWin || !combo_) return;

        setGeometry(topWin->rect());
        const QPoint anchor = combo_->mapTo(topWin, QPoint(0, combo_->height() + 3));
        const int count = combo_->count();
        const int totalContentH = count * kRowHeight + 2 * kPadY;

        const int cardW = std::max(combo_->width(), 168);
        const int x = std::clamp(anchor.x(), 6, std::max(6, topWin->width() - cardW - 6));

        const int availBelow = topWin->height() - anchor.y() - 8;
        const int cardH = std::min(totalContentH, std::max(110, availBelow));
        cardRect_ = QRect(x, anchor.y(), cardW, cardH);

        maxScrollY_ = std::max(0, totalContentH - cardH);
        if (hoveredRow_ >= 0 && maxScrollY_ > 0) {
            const int rowTop = hoveredRow_ * kRowHeight;
            const int visibleH = cardH - 2 * kPadY;
            if (rowTop + kRowHeight > visibleH) {
                scrollY_ = std::clamp(rowTop - visibleH / 2, 0, maxScrollY_);
            }
        }
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        if (!combo_) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Soft drop shadow around the dropdown card
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 115));
        p.drawRoundedRect(QRectF(cardRect_).adjusted(-1.5, 1.5, 2.5, 4.0), 7.5, 7.5);

        // Opaque dark studio card with crimson border
        p.setPen(QPen(FxTheme::kCrimsonPrimary, 1.25));
        p.setBrush(QColor(0x1D, 0x1D, 0x28));
        p.drawRoundedRect(QRectF(cardRect_), 6.0, 6.0);

        const QRect innerRect = cardRect_.adjusted(2, kPadY, -2, -kPadY);
        p.save();
        p.setClipRect(innerRect);

        const int count = combo_->count();
        const bool hasScroll = (maxScrollY_ > 0);
        const int rowRightTrim = hasScroll ? 10 : 4;

        p.setFont(FxTheme::instance().uiFont(10, true));
        const QFontMetrics fm(p.font());

        for (int i = 0; i < count; ++i) {
            const int ry = cardRect_.top() + kPadY + i * kRowHeight - scrollY_;
            const QRect rowRect(cardRect_.left() + 4, ry, cardRect_.width() - 4 - rowRightTrim, kRowHeight);
            if (rowRect.bottom() < innerRect.top() || rowRect.top() > innerRect.bottom()) {
                continue;
            }

            if (i == hoveredRow_) {
                p.setPen(Qt::NoPen);
                p.setBrush(FxTheme::kCrimsonPrimary);
                p.drawRoundedRect(QRectF(rowRect), 4.5, 4.5);
                p.setPen( QColor(0xFF, 0xFF, 0xFF));
            } else if (i == combo_->currentIndex()) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x2A, 0x2A, 0x3A));
                p.drawRoundedRect(QRectF(rowRect), 4.5, 4.5);
                p.setPen(FxTheme::kCrimsonHighlight);
            } else {
                p.setPen(QColor(0xF0, 0xF0, 0xF6));
            }

            const QRect textRect = rowRect.adjusted(10, 0, -8, 0);
            const QString elided = fm.elidedText(combo_->itemText(i), Qt::ElideRight, textRect.width());
            p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
        }

        if (hasScroll) {
            const int trackH = innerRect.height();
            const int totalH = count * kRowHeight;
            const int thumbH = std::max(20, (trackH * trackH) / std::max(1, totalH));
            const int thumbY = innerRect.top() + ((trackH - thumbH) * scrollY_) / std::max(1, maxScrollY_);
            const QRectF thumbRect(cardRect_.right() - 6.0, thumbY, 3.5, thumbH);
            p.setPen(Qt::NoPen);
            p.setBrush(FxTheme::kCrimsonHighlight);
            p.drawRoundedRect(thumbRect, 1.75, 1.75);
        }

        p.restore();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint pos = event->position().toPoint();
        if (cardRect_.contains(pos)) {
            setCursor(Qt::PointingHandCursor);
            const int relY = pos.y() - (cardRect_.top() + kPadY) + scrollY_;
            const int row = relY / kRowHeight;
            const int validRow = (row >= 0 && row < combo_->count()) ? row : -1;
            if (hoveredRow_ != validRow) {
                hoveredRow_ = validRow;
                update();
            }
        } else {
            setCursor(Qt::ArrowCursor);
            if (hoveredRow_ != -1) {
                hoveredRow_ = -1;
                update();
            }
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        const QPoint pos = event->position().toPoint();
        if (event->button() == Qt::LeftButton && cardRect_.contains(pos)) {
            const int relY = pos.y() - (cardRect_.top() + kPadY) + scrollY_;
            const int row = relY / kRowHeight;
            if (row >= 0 && row < combo_->count()) {
                combo_->setCurrentIndex(row);
                emit combo_->activated(row);
                emit combo_->textActivated(combo_->itemText(row));
            }
        }
        if (combo_) {
            combo_->hidePopup();
        }
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override {
        if (maxScrollY_ > 0) {
            const int step = (event->angleDelta().y() > 0) ? -kRowHeight : kRowHeight;
            scrollY_ = std::clamp(scrollY_ + step, 0, maxScrollY_);
            update();
        }
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (!combo_) return;
        if (event->key() == Qt::Key_Escape) {
            combo_->hidePopup();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Down) {
            hoveredRow_ = std::clamp(hoveredRow_ + 1, 0, std::max(0, combo_->count() - 1));
            update();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Up) {
            hoveredRow_ = std::clamp(hoveredRow_ - 1, 0, std::max(0, combo_->count() - 1));
            update();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (hoveredRow_ >= 0 && hoveredRow_ < combo_->count()) {
                combo_->setCurrentIndex(hoveredRow_);
                emit combo_->activated(hoveredRow_);
                emit combo_->textActivated(combo_->itemText(hoveredRow_));
            }
            combo_->hidePopup();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QPointer<FxComboBox> combo_;
    QRect cardRect_;
    int hoveredRow_ = -1;
    int scrollY_ = 0;
    int maxScrollY_ = 0;
};

} // namespace

FxComboBox::FxComboBox(QWidget* parent)
    : QComboBox(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setMinimumHeight(30);
}

FxComboBox::~FxComboBox() {
    if (popupOverlay_) {
        popupOverlay_->hide();
        popupOverlay_->deleteLater();
    }
}

void FxComboBox::showPopup() {
    if (count() <= 0) {
        return;
    }
    hidePopup();
    QWidget* topWin = window();
    if (!topWin) {
        return;
    }
    popupVisible_ = true;
    auto* overlay = new FxComboPopupOverlay(this, topWin);
    popupOverlay_ = overlay;
    overlay->show();
    overlay->raise();
    overlay->setFocus();
    update();
}

void FxComboBox::hidePopup() {
    popupVisible_ = false;
    if (popupOverlay_) {
        popupOverlay_->hide();
        popupOverlay_->deleteLater();
        popupOverlay_ = nullptr;
    }
    update();
}

void FxComboBox::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QComboBox::enterEvent(event);
}

void FxComboBox::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QComboBox::leaveEvent(event);
}

void FxComboBox::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (popupVisible_) {
            hidePopup();
        } else {
            showPopup();
        }
        event->accept();
        return;
    }
    QComboBox::mousePressEvent(event);
}

void FxComboBox::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool active = hovered_ || popupVisible_;
    const QRectF boxRect = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);

    p.setPen(active ? QPen(FxTheme::kCrimsonPrimary, 1.35) : QPen(QColor("#000000"), 1.0));
    p.setBrush(active ? QColor("#0f0f0f") : QColor("#000000"));
    p.drawRoundedRect(boxRect, 6.0, 6.0);

    // Selected item text
    p.setFont(FxTheme::instance().uiFont(10, true));
    p.setPen(QColor(0xB1, 0xB1, 0xB1));
    const QRect textRect = rect().adjusted(12, 0, -28, 0);
    const QFontMetrics fm(p.font());
    const QString elided = fm.elidedText(currentText(), Qt::ElideRight, textRect.width());
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

    // Crisp vector chevron indicator on right
    const float cx = static_cast<float>(width()) - 15.0f;
    const float cy = static_cast<float>(height()) * 0.5f;
    QPainterPath chevron;
    if (popupVisible_) {
        chevron.moveTo(cx - 4.2f, cy + 2.0f);
        chevron.lineTo(cx,        cy - 2.2f);
        chevron.lineTo(cx + 4.2f, cy + 2.0f);
    } else {
        chevron.moveTo(cx - 4.2f, cy - 1.8f);
        chevron.lineTo(cx,        cy + 2.4f);
        chevron.lineTo(cx + 4.2f, cy - 1.8f);
    }
    p.setPen(QPen(active ? FxTheme::kCrimsonHighlight : FxTheme::kCrimsonPrimary, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(chevron);
}

FxTheme& FxTheme::instance() {
    static FxTheme inst;
    return inst;
}

void FxTheme::initialize(const std::filesystem::path& projectRoot) {
    if (!projectRoot.empty() && std::filesystem::exists(projectRoot / "assets")) {
        rootDir_ = projectRoot;
    } else {
        std::filesystem::path cwd = std::filesystem::current_path();
        if (std::filesystem::exists(cwd / "assets")) {
            rootDir_ = cwd;
        } else if (std::filesystem::exists(cwd.parent_path() / "assets")) {
            rootDir_ = cwd.parent_path();
        } else if (QCoreApplication::instance()) {
            std::filesystem::path appDir = QCoreApplication::applicationDirPath().toStdString();
            if (std::filesystem::exists(appDir / "assets")) {
                rootDir_ = appDir;
            } else if (std::filesystem::exists(appDir.parent_path() / "assets")) {
                rootDir_ = appDir.parent_path();
            } else {
                rootDir_ = cwd;
            }
        } else {
            rootDir_ = cwd;
        }
    }

    loadFonts();
    loadTranslations();
}

void FxTheme::loadFonts() {
    const std::vector<std::filesystem::path> fontPaths = {
        rootDir_ / "FxSound" / "NotoSans-Regular.ttf",
        rootDir_ / "FxSound" / "NotoSans-Bold.ttf",
        rootDir_ / "FxSound" / "NotoSans-Italic.ttf",
        rootDir_ / "FxSound" / "MontserratAlternates-Regular.otf",
        rootDir_ / "FxSound" / "MontserratAlternates-Medium.otf",
        rootDir_ / "FxSound" / "MontserratAlternates-Bold.otf",
        rootDir_ / "assets" / "fonts" / "Gilroy-Semibold-1.ttf",
        rootDir_ / "assets" / "fonts" / "Gilroy-Semibold-2.ttf",
    };

    for (const auto& fp : fontPaths) {
        std::error_code ec;
        if (std::filesystem::exists(fp, ec)) {
            const int id = QFontDatabase::addApplicationFont(QString::fromStdString(fp.string()));
            if (id >= 0) {
                const QStringList families = QFontDatabase::applicationFontFamilies(id);
                for (const QString& fam : families) {
                    if (fam.contains("Noto Sans", Qt::CaseInsensitive)) {
                        primaryFontFamily_ = fam;
                    } else if (fam.contains("Montserrat", Qt::CaseInsensitive)) {
                        headingFontFamily_ = fam;
                    } else if (fam.contains("Gilroy", Qt::CaseInsensitive)) {
                        gilroyFontFamily_ = fam;
                    }
                }
            }
        }
    }
}

void FxTheme::loadTranslations() {
    languages_ = {
        {"en",    "English"},
        {"de",    "Deutsch"},
        {"es",    "Español"},
        {"fr",    "Français"},
        {"id",    "Bahasa Indonesia"},
        {"it",    "Italiano"},
        {"ko",    "한국어"},
        {"pl",    "Polski"},
        {"pt",    "Português"},
        {"pt-BR", "Português (Brasil)"},
        {"ro",    "Română"},
        {"ru",    "Русский"},
        {"sv",    "Svenska"},
        {"tr",    "Türkçe"},
        {"vi",    "Tiếng Việt"},
        {"zh-CN", "简体中文"},
        {"zh-TW", "繁體中文"},
    };

    const auto jsonPath = rootDir_ / "assets" / "i18n" / "translations.json";
    QFile file(QString::fromStdString(jsonPath.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject rootObj = doc.object();
    for (auto it = rootObj.begin(); it != rootObj.end(); ++it) {
        const QString langCode = it.key();
        if (!it.value().isObject()) continue;
        const QJsonObject mapObj = it.value().toObject();
        QMap<QString, QString> table;
        for (auto mit = mapObj.begin(); mit != mapObj.end(); ++mit) {
            table.insert(mit.key(), mit.value().toString());
        }
        translations_.insert(langCode, table);
    }
}

void FxTheme::setLanguage(const QString& langCode) {
    currentLangCode_ = langCode;
}

QString FxTheme::trKey(const QString& englishKey) const {
    if (currentLangCode_ == "en" || !translations_.contains(currentLangCode_)) {
        return englishKey;
    }
    const auto& map = translations_[currentLangCode_];
    const QString val = map.value(englishKey);
    return val.isEmpty() ? englishKey : val;
}

QPixmap FxTheme::loadSvgPixmap(const QString& svgFileName, const QSize& size) const {
    const auto path = rootDir_ / "assets" / "svg" / svgFileName.toStdString();
    const qreal dpr = std::max(qreal(1), qGuiApp->devicePixelRatio());
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(QString::fromStdString(path.string()));
    if (renderer.isValid()) {
        if (svgFileName == "Slider_Thumb.svg") {
            // Crop 64x64 outer drop-shadow canvas to the exact 16x16 thumb ring at (24, 20)
            renderer.setViewBox(QRectF(24.0, 20.0, 16.0, 16.0));
        }
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, QRectF(0, 0, size.width(), size.height()));
    }
    return pixmap;
}

QIcon FxTheme::loadSvgIcon(const QString& svgFileName, const QSize& size) const {
    return QIcon(loadSvgPixmap(svgFileName, size));
}

QPixmap FxTheme::loadPngPixmap(const QString& pngFileName) const {
    const auto path = rootDir_ / "assets" / "png" / pngFileName.toStdString();
    QPixmap pix(QString::fromStdString(path.string()));
    return pix;
}

QFont FxTheme::titleFont(int pointSize, bool bold) const {
    QFont f(headingFontFamily_, pointSize);
    f.setBold(bold);
    return f;
}

QFont FxTheme::uiFont(int pointSize, bool bold) const {
    QFont f(gilroyFontFamily_, pointSize);
    f.setBold(bold);
    return f;
}

QFont FxTheme::numericFont(int pointSize, bool bold) const {
    QFont f(gilroyFontFamily_, pointSize);
    f.setBold(bold);
    return f;
}

QString FxTheme::globalStyleSheet() const {
    return R"(
        QWidget {
            color: #B1B1B1;
            font-family: 'Noto Sans', sans-serif;
        }
        QComboBox {
            background-color: #0f0f0f;
            border: 1px solid #38384A;
            border-radius: 6px;
            padding: 4px 12px;
            min-height: 22px;
            color: #B1B1B1;
            font-size: 13px;
            font-weight: 600;
        }
        QComboBox:hover {
            border: 1px solid #E33250;
            background-color: #2A2A38;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background-color: #131313;
            border: 1px solid #E33250;
            selection-background-color: #E33250;
            selection-color: #FFFFFF;
            color: #B1B1B1;
            padding: 4px;
            outline: 0;
        }
        QMenu {
            background-color: #1E1E28;
            border: 1px solid #38384A;
            border-radius: 6px;
            padding: 6px 0px;
            color: #B1B1B1;
            font-size: 13px;
        }
        QMenu::item {
            padding: 7px 22px;
        }
        QMenu::item:selected {
            background-color: #E33250;
            color: #FFFFFF;
        }
        QMenu::item:disabled {
            color: #6C6C7A;
        }
        QMenu::separator {
            height: 1px;
            background: #323240;
            margin: 4px 10px;
        }
        QToolTip {
            background-color: #15151C;
            color: #B1B1B1;
            border: 1px solid #E33250;
            border-radius: 4px;
            padding: 6px 8px;
            font-size: 12px;
        }
    )";
}

} // namespace fxsound
