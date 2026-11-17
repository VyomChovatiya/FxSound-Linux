#pragma once

#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QIcon>
#include <QMap>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <filesystem>
#include <vector>

namespace fxsound {

// Wayland-safe, glitch-free custom Studio Dropdown that renders its popup inside the parent
// window's single backing store instead of spawning a separate Wayland xdg_popup wl_surface.
class FxComboBox : public QComboBox {
    Q_OBJECT

public:
    explicit FxComboBox(QWidget* parent = nullptr);
    ~FxComboBox() override;

    void showPopup() override;
    void hidePopup() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool hovered_ = false;
    bool popupVisible_ = false;
    QPointer<QWidget> popupOverlay_;
};

struct LanguageEntry {
    QString code;        // e.g. "en", "de", "es", "fr", "zh-CN"
    QString displayName; // e.g. "English", "Deutsch", "Español"
};

class FxTheme {
public:
    // Reversed ARGB color constants from FxTheme (0x14014f510)
    static inline const QColor kCrimsonPrimary   = QColor(0xE3, 0x32, 0x50); // #E33250
    static inline const QColor kCrimsonHighlight = QColor(0xEF, 0x4B, 0x65); // #EF4B65
    static inline const QColor kCrimsonGlow      = QColor(0xE3, 0x32, 0x50, 110);
    static inline const QColor kBgWindow         = QColor(0x18, 0x18, 0x18); // #16161D
    static inline const QColor kBgHeader         = QColor(0x18, 0x18, 0x18); // #1B1B24
    static inline const QColor kBgPanel          = QColor(0x0F, 0x0F, 0x0F); // #21212B
    static inline const QColor kBgControl        = QColor(0, 0, 0); // #292935
    static inline const QColor kTrackBackground  = QColor(0x39, 0x16, 0x1C); // #343442
    static inline const QColor kDisabledGrey     = QColor(0x7E, 0x7E, 0x7E); // #7E7E7E
    static inline const QColor kTextPrimary      = QColor(0xB1, 0xB1, 0xB1); // #F5F5F7
    static inline const QColor kTextSecondary    = QColor(0xB1, 0xB1, 0xB1); // #A6A6B5

    // Window dimensions from FxMainComponent / FxProView / FxLiteView
    static constexpr int kProWidth  = 957;
    static constexpr int kProHeight = 711;
    static constexpr int kLiteWidth = 400;
    static constexpr int kLiteHeight = 360;

    static FxTheme& instance();

    void initialize(const std::filesystem::path& projectRoot = {});

    std::filesystem::path rootDir() const { return rootDir_; }
    std::filesystem::path assetsDir() const { return rootDir_ / "assets"; }
    std::filesystem::path factoryPresetsDir() const { return rootDir_ / "FxSound" / "Factsoft"; }

    QPixmap loadSvgPixmap(const QString& svgFileName, const QSize& size) const;
    QIcon loadSvgIcon(const QString& svgFileName, const QSize& size = QSize(24, 24)) const;
    QPixmap loadPngPixmap(const QString& pngFileName) const;

    QFont titleFont(int pointSize = 11, bool bold = true) const;
    QFont uiFont(int pointSize = 10, bool bold = false) const;
    QFont numericFont(int pointSize = 10, bool bold = true) const;

    // 16-language localization system extracted from FxSound.exe
    const std::vector<LanguageEntry>& availableLanguages() const { return languages_; }
    QString currentLanguage() const { return currentLangCode_; }
    void setLanguage(const QString& langCode);
    QString trKey(const QString& englishKey) const;

    QString globalStyleSheet() const;

private:
    FxTheme() = default;
    void loadFonts();
    void loadTranslations();

    std::filesystem::path rootDir_;
    QString primaryFontFamily_ = "Noto Sans";
    QString headingFontFamily_ = "Montserrat Alternates";
    QString gilroyFontFamily_  = "Noto Sans";

    QString currentLangCode_ = "en";
    std::vector<LanguageEntry> languages_;
    QMap<QString, QMap<QString, QString>> translations_;
};

} // namespace fxsound
