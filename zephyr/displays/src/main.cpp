// MIT License - see LICENSE
//
// A menu on an SSD1322 OLED (256x64, 16 grays) on SPI, driven by four push buttons on GPIO lines
// (Up, Down, Select, Back), with an HD44780 character LCD (16x2) on a PCF8574 I2C backpack as a
// status line. The menu changes the OLED's brightness and inversion and the LCD's backlight, and
// opens an animation, the panel's sixteen grays and an about page. An LED blinks as a heartbeat.
//
// The same source runs in two places:
//
//   On a Zephyr board, built with west. For the MIMXRT1170-EVKB the parts go on its Arduino header:
//   OLED SPI on D10-D13, D/C on D8, RES on D9; LCD I2C on D14/D15; the buttons on A0-A3 to ground.
//
//     west build -b mimxrt1170_evk/mimxrt1176/cm7 zephyr/displays -- -DNT_ROOT=/path/to/Nt
//     west flash -r linkserver
//
//   Natively on Linux, without hardware or QEMU, against the ntdisplayemu bench of Nt. The top-level
//   CMakeLists.txt of this repository builds it as nt_displays:
//
//     ntdisplayemu --button gpiochip0:17=Up --button gpiochip0:18=Down --button gpiochip0:19=Select
//                  --button gpiochip0:20=Back --led gpiochip0:5=Heartbeat &      (one line)
//     NT_GPIO_EMULATOR=ntdisplayemu ./nt_displays
//
//   There the devices and lines are options (see --help); the defaults are those above. Without
//   NT_GPIO_EMULATOR the same binary drives real displays on a Linux board.
#include "font5x7.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtGpio/QCharacterDisplay>
#include <QtGpio/QGpioLine>
#include <QtGpio/QSsd1322Display>
#ifndef __ZEPHYR__
#include <QtCore/QCommandLineParser>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace {

enum Button { Up, Down, Select, Back, ButtonCount };

struct Wiring {
#ifdef __ZEPHYR__
    // The Arduino header of the MIMXRT1170-EVKB. GPIO9 has no interrupt on the CM7: the buttons are polled.
    QString spi = QStringLiteral("spi@40114000:0"); // LPSPI1, PCS0 on D10
    QString dc = QStringLiteral("gpio9:6");         // D8
    QString reset = QStringLiteral("gpio9:0");      // D9
    QString i2c = QStringLiteral("i2c@40c34000");   // LPI2C5 on D14/D15
    std::array<QString, ButtonCount> buttons = {QStringLiteral("gpio9:9"), QStringLiteral("gpio9:10"),
                                                QStringLiteral("gpio9:11"), QStringLiteral("gpio9:12")}; // A0-A3
    QString led = QStringLiteral("led0");
#else
    QString spi = QStringLiteral("spidev0.0");
    QString dc = QStringLiteral("gpiochip0:25");
    QString reset = QStringLiteral("gpiochip0:24");
    QString i2c = QStringLiteral("i2c-1");
    std::array<QString, ButtonCount> buttons = {QStringLiteral("gpiochip0:17"), QStringLiteral("gpiochip0:18"),
                                                QStringLiteral("gpiochip0:19"), QStringLiteral("gpiochip0:20")};
    QString led = QStringLiteral("gpiochip0:5");
#endif
    int address = 0x27;
    int seconds = 0; // run for ever
};

// A frame of 8-bit gray pixels, drawn by hand: on Zephyr there is no Nt GUI and no QPainter.
class Frame {
public:
    Frame(int width, int height)
        : m_width(width), m_height(height), m_pixels(static_cast<std::size_t>(width * height), 0) {}

    void clear() { std::fill(m_pixels.begin(), m_pixels.end(), 0); }
    void set(int x, int y, int gray) {
        if (x >= 0 && x < m_width && y >= 0 && y < m_height)
            m_pixels[static_cast<std::size_t>(y * m_width + x)] = static_cast<uchar>(gray);
    }
    void fill(int x, int y, int w, int h, int gray) {
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i)
                set(i, j, gray);
    }
    void box(int x, int y, int w, int h, int gray) {
        fill(x, y, w, 1, gray);
        fill(x, y + h - 1, w, 1, gray);
        fill(x, y, 1, h, gray);
        fill(x + w - 1, y, 1, h, gray);
    }
    void disc(int cx, int cy, int r, int gray) {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r)
                    set(cx + x, cy + y, gray);
    }
    // Draws text with the 5x7 font, six pixels a character; returns the x after it.
    int text(int x, int y, const QString &s, int gray) {
        for (const QChar ch : s) {
            const Font5x7::Glyph &g = Font5x7::glyph(static_cast<char>(ch.unicode() < 128 ? ch.unicode() : '?'));
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g.rows[row][col] == '#')
                        set(x + col, y + row, gray);
            x += 6;
        }
        return x;
    }
    const uchar *bits() const { return m_pixels.data(); }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    int m_width;
    int m_height;
    std::vector<uchar> m_pixels;
};

// The menu and the pages it opens. All drawing goes through refresh().
class MenuApp {
public:
    MenuApp(QSsd1322Display *oled, QCharacterDisplay *lcd)
        : m_oled(oled), m_lcd(lcd), m_frame(oled ? oled->width() : 256, oled ? oled->height() : 64) {
        if (m_lcd)
            defineBarCharacters();
    }

    void press(Button b) {
        if (m_page == Page::Menu) {
            if (b == Up)
                m_selected = (m_selected + ItemCount - 1) % ItemCount;
            else if (b == Down)
                m_selected = (m_selected + 1) % ItemCount;
            else if (b == Select)
                activate();
        } else if (m_page == Page::Brightness && (b == Up || b == Down)) {
            m_brightness = std::clamp(m_brightness + (b == Up ? 1 : -1), 0, 15);
            if (m_oled)
                m_oled->setContrast(m_brightness * 17);
        } else if (b == Back || b == Select) {
            m_page = Page::Menu;
        }
        refresh();
    }

    // Moves the animation on; called by a timer.
    void tick() {
        ++m_tick;
        if (m_page == Page::Animation)
            refresh();
    }

    void refresh() {
        drawOled();
        updateLcd();
    }

private:
    enum class Page { Menu, Animation, Brightness, Grays, About };
    enum Item { ItemAnimation, ItemBrightness, ItemInvert, ItemBacklight, ItemGrays, ItemAbout, ItemCount };

    QString itemText(int item) const {
        switch (item) {
        case ItemAnimation:
            return QStringLiteral("Animation");
        case ItemBrightness:
            return QStringLiteral("Brightness  %1").arg(m_brightness * 100 / 15).append(QChar('%'));
        case ItemInvert:
            return QStringLiteral("Invert      %1").arg(m_inverted ? QStringLiteral("On") : QStringLiteral("Off"));
        case ItemBacklight:
            return QStringLiteral("Backlight   %1").arg(m_backlight ? QStringLiteral("On") : QStringLiteral("Off"));
        case ItemGrays:
            return QStringLiteral("Gray levels");
        default:
            return QStringLiteral("About");
        }
    }

    void activate() {
        switch (m_selected) {
        case ItemAnimation:
            m_page = Page::Animation;
            break;
        case ItemBrightness:
            m_page = Page::Brightness;
            break;
        case ItemInvert:
            m_inverted = !m_inverted;
            if (m_oled)
                m_oled->setInverted(m_inverted);
            break;
        case ItemBacklight:
            m_backlight = !m_backlight;
            if (m_lcd)
                m_lcd->setBacklight(m_backlight);
            break;
        case ItemGrays:
            m_page = Page::Grays;
            break;
        default:
            m_page = Page::About;
            break;
        }
    }

    void title(const QString &text) {
        m_frame.fill(0, 0, m_frame.width(), 9, 70);
        m_frame.text(3, 1, text, 255);
    }

    void drawOled() {
        if (!m_oled)
            return;
        Frame &f = m_frame;
        f.clear();
        switch (m_page) {
        case Page::Menu: {
            title(QStringLiteral("Nt displays - menu"));
            // Six items fit under the title: 9 pixels a line.
            for (int i = 0; i < ItemCount; ++i) {
                const int y = 10 + i * 9;
                if (i == m_selected) {
                    f.fill(0, y - 1, f.width(), 9, 40);
                    f.text(3, y, QStringLiteral(">"), 255);
                }
                f.text(12, y, itemText(i), i == m_selected ? 255 : 150);
            }
            break;
        }
        case Page::Brightness: {
            title(QStringLiteral("Brightness"));
            f.text(8, 20, QStringLiteral("Up/Down to change, Back to return"), 160);
            f.box(8, 36, f.width() - 16, 14, 120);
            f.fill(10, 38, (f.width() - 20) * m_brightness / 15, 10, 230);
            f.text(8, 54, QStringLiteral("Contrast %1 of 255").arg(m_brightness * 17), 160);
            break;
        }
        case Page::Grays: {
            title(QStringLiteral("Gray levels"));
            const int w = (f.width() - 16) / 16;
            for (int g = 0; g < 16; ++g) {
                f.fill(8 + g * w, 14, w - 2, 36, g * 17);
                f.text(8 + g * w + (g < 10 ? 3 : 0), 54, QString::number(g), 150);
            }
            break;
        }
        case Page::About: {
            title(QStringLiteral("About"));
            f.text(8, 14, QStringLiteral("Nt GPIO: QSsd1322Display + QCharacterDisplay"), 200);
            f.text(8, 24, QStringLiteral("Buttons on QGpioLine, polled or on IRQs"), 160);
            f.text(8, 34, QStringLiteral("Runs on Zephyr and natively on Linux"), 160);
            f.text(8, 44, QStringLiteral("(NT_GPIO_EMULATOR=ntdisplayemu)"), 120);
            f.text(8, 55, QStringLiteral("Back to return"), 100);
            break;
        }
        case Page::Animation: {
            const int w = f.width();
            const int h = f.height();
            const double level = 0.5 + 0.5 * std::sin(m_tick * 0.05);
            f.box(0, 0, w, h, 90);
            const double phase = m_tick * 0.15;
            for (int x = 3; x < w - 3; ++x) {
                const double y = h / 2.0 - 4 + std::sin(x * 0.06 + phase) * (h / 2.0 - 12) * (0.4 + 0.6 * level);
                f.set(x, static_cast<int>(y), 200);
                f.set(x, static_cast<int>(y) + 1, 80);
            }
            const int span = w - 24;
            const int travel = m_tick * 3 % (2 * span);
            f.disc(12 + (travel < span ? travel : 2 * span - travel),
                   12 + static_cast<int>(std::fabs(std::sin(m_tick * 0.12)) * (h - 32)), 5, 255);
            f.box(3, h - 9, w - 6, 6, 120);
            f.fill(4, h - 8, static_cast<int>((w - 8) * level), 4, 230);
            break;
        }
        }
        m_oled->writePixels(f.bits(), f.width());
    }

    void updateLcd() {
        if (!m_lcd)
            return;
        QString top;
        QString bottom;
        switch (m_page) {
        case Page::Menu:
            top = QStringLiteral("Menu %1/%2").arg(m_selected + 1).arg(int(ItemCount));
            bottom = itemText(m_selected).simplified();
            break;
        case Page::Brightness:
            top = QStringLiteral("Brightness");
            bottom = bar(m_brightness / 15.0, 16);
            break;
        case Page::Animation:
            // The LCD changes every frame only here; elsewhere it changes with the buttons.
            top = QStringLiteral("Animation #%1").arg(m_tick);
            bottom = bar(0.5 + 0.5 * std::sin(m_tick * 0.05), 16);
            break;
        case Page::Grays:
            top = QStringLiteral("Gray levels");
            bottom = QStringLiteral("16 levels, 4 bit");
            break;
        case Page::About:
            top = QStringLiteral("Nt displays");
            bottom = QStringLiteral("Zephyr + Linux");
            break;
        }
        const QString text = top + QChar('\n') + bottom;
        if (text != m_lcdText) { // only what changed goes over I2C
            m_lcd->setText(text);
            m_lcdText = text;
        }
    }

    // Custom characters 1 to 5: a cell filled from the left by one to five columns, for a smooth bar.
    void defineBarCharacters() {
        for (int filled = 1; filled <= 5; ++filled)
            m_lcd->setCustomCharacter(filled, QByteArray(8, static_cast<char>((0x1f << (5 - filled)) & 0x1f)));
    }

    static QString bar(double level, int cells) {
        int fifths = static_cast<int>(level * cells * 5 + 0.5);
        QString out;
        for (int i = 0; i < cells; ++i) {
            const int here = std::min(5, std::max(0, fifths));
            out.append(here == 0 ? QChar(' ') : QChar(static_cast<char16_t>(here)));
            fifths -= 5;
        }
        return out;
    }

    QSsd1322Display *m_oled;
    QCharacterDisplay *m_lcd;
    Frame m_frame;
    Page m_page = Page::Menu;
    int m_selected = 0;
    int m_brightness = 9; // of 15; 9 * 17 = 153, near the controller's usual 159
    bool m_inverted = false;
    bool m_backlight = true;
    int m_tick = 0;
    QString m_lcdText;
};

} // namespace

int main(int argc, char **argv) {
    Wiring wiring;
#ifdef __ZEPHYR__
    (void)argc;
    (void)argv;
    int zargc = 1;
    char name[] = "displays";
    char *zargv[] = {name, nullptr};
    QCoreApplication app(zargc, zargv);
#else
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "A menu on an SSD1322 OLED driven by GPIO buttons, with an HD44780 LCD; run it with "
        "NT_GPIO_EMULATOR=ntdisplayemu to use the emulator instead of the hardware."));
    parser.addHelpOption();
    const QCommandLineOption spi(QStringLiteral("spi"), QStringLiteral("The SPI device of the OLED."),
                                 QStringLiteral("device"), wiring.spi);
    const QCommandLineOption dc(QStringLiteral("dc"), QStringLiteral("The D/C line of the OLED."),
                                QStringLiteral("line"), wiring.dc);
    const QCommandLineOption reset(QStringLiteral("reset"), QStringLiteral("The RES line of the OLED."),
                                   QStringLiteral("line"), wiring.reset);
    const QCommandLineOption i2c(QStringLiteral("i2c"), QStringLiteral("The I2C bus of the LCD."),
                                 QStringLiteral("bus"), wiring.i2c);
    const QCommandLineOption address(QStringLiteral("address"), QStringLiteral("The I2C address of the LCD."),
                                     QStringLiteral("address"), QStringLiteral("0x27"));
    const QCommandLineOption up(QStringLiteral("up"), QStringLiteral("The Up button."), QStringLiteral("line"),
                                wiring.buttons[Up]);
    const QCommandLineOption down(QStringLiteral("down"), QStringLiteral("The Down button."),
                                  QStringLiteral("line"), wiring.buttons[Down]);
    const QCommandLineOption select(QStringLiteral("select"), QStringLiteral("The Select button."),
                                    QStringLiteral("line"), wiring.buttons[Select]);
    const QCommandLineOption back(QStringLiteral("back"), QStringLiteral("The Back button."),
                                  QStringLiteral("line"), wiring.buttons[Back]);
    const QCommandLineOption led(QStringLiteral("led"), QStringLiteral("The heartbeat LED."),
                                 QStringLiteral("line"), wiring.led);
    const QCommandLineOption time(QStringLiteral("time"), QStringLiteral("Quit after this many seconds."),
                                  QStringLiteral("seconds"));
    parser.addOptions({spi, dc, reset, i2c, address, up, down, select, back, led, time});
    parser.process(app);
    wiring.spi = parser.value(spi);
    wiring.dc = parser.value(dc);
    wiring.reset = parser.value(reset);
    wiring.i2c = parser.value(i2c);
    wiring.address = parser.value(address).toInt(nullptr, 0);
    wiring.buttons = {parser.value(up), parser.value(down), parser.value(select), parser.value(back)};
    wiring.led = parser.value(led);
    wiring.seconds = parser.value(time).toInt();
#endif

    QSsd1322Display oled;
    oled.setSpiDevice(wiring.spi);
    oled.setDataCommandLine(wiring.dc);
    oled.setResetLine(wiring.reset);
    const bool haveOled = oled.open();
    if (!haveOled)
        qWarning() << "OLED:" << oled.errorString();

    QCharacterDisplay lcd;
    lcd.setI2cDevice(wiring.i2c, wiring.address);
    const bool haveLcd = lcd.open();
    if (!haveLcd)
        qWarning() << "LCD:" << lcd.errorString();
    if (!haveOled && !haveLcd)
        return 1;

    MenuApp menu(haveOled ? &oled : nullptr, haveLcd ? &lcd : nullptr);

    // The buttons pull their lines to ground: active low, with the pull-up on.
    static const char *const names[] = {"Up", "Down", "Select", "Back"};
    std::vector<std::unique_ptr<QGpioLine>> buttons;
    for (int b = 0; b < ButtonCount; ++b) {
        auto line = std::make_unique<QGpioLine>(wiring.buttons[static_cast<std::size_t>(b)]);
        line->setDirection(QGpioLine::Input);
        line->setActiveLow(true);
        line->setBias(QGpioLine::PullUp);
        line->setDebounceInterval(20);
        if (!line->open()) {
            qWarning() << names[b] << "button" << line->lineName() << ":" << line->errorString();
            continue;
        }
        QObject::connect(line.get(), &QGpioLine::valueChanged, [&menu, b](bool pressed) {
            if (!pressed)
                return;
            qInfo() << names[b];
            menu.press(static_cast<Button>(b));
        });
        buttons.push_back(std::move(line));
    }

    QGpioLine heartbeat(wiring.led);
    heartbeat.setDirection(QGpioLine::Output);
    const bool haveLed = heartbeat.open();

    menu.refresh();
    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        menu.tick();
        if (haveLed)
            heartbeat.setValue(++ticks / 10 % 2 == 0);
    });
    timer.start(50);

    qInfo() << "menu ready:" << buttons.size() << "buttons";
    if (wiring.seconds > 0)
        QTimer::singleShot(wiring.seconds * 1000, &app, [&app] { app.quit(); });
    return app.exec();
}
