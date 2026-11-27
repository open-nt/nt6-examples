// MIT License - see LICENSE
// Nt GPIO on Zephyr: the board's first LED blinks, its second LED is lit while the user button is held,
// and each press is printed. Lines are named by the devicetree: the aliases led0, led1 and sw0 carry
// their pins and flags, and a pin can also be named by its controller, as gpio9:3.
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtGpio/QGpioLine>

namespace {

bool openLine(QGpioLine &line, QGpioLine::Direction direction) {
    line.setDirection(direction);
    if (!line.open()) {
        qWarning() << line.lineName() << ":" << line.errorString();
        return false;
    }
    qInfo() << line.lineName() << (direction == QGpioLine::Input ? "input" : "output")
            << (direction == QGpioLine::Input ? (line.isEventDriven() ? "on interrupts" : "polled") : "");
    return true;
}

} // namespace

int main() {
    int argc = 1;
    char name[] = "gpio";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);

    QGpioLine blink(QStringLiteral("led0"));
    QGpioLine follow(QStringLiteral("led1"));
    QGpioLine button(QStringLiteral("sw0"));
    button.setDebounceInterval(20);
    const bool haveBlink = openLine(blink, QGpioLine::Output);
    const bool haveFollow = openLine(follow, QGpioLine::Output);
    if (!openLine(button, QGpioLine::Input))
        return 1;

    // The same pin by its controller's node label and number: GPIO9 pin 3 is the green LED of the EVKB.
    {
        QGpioLine byController(QStringLiteral("gpio9:3"));
        byController.setDirection(QGpioLine::Output);
        qInfo() << "gpio9:3" << (byController.open() ? "opens" : qPrintable(byController.errorString()));
    }

    int presses = 0;
    QObject::connect(&button, &QGpioLine::valueChanged, [&](bool pressed) {
        if (haveFollow)
            follow.setValue(pressed);
        if (pressed)
            ++presses;
        qInfo() << (pressed ? "button pressed" : "button released") << "presses:" << presses;
    });

    QTimer timer;
    bool on = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        on = !on;
        if (haveBlink)
            blink.setValue(on);
    });
    timer.start(500);

    return app.exec();
}
