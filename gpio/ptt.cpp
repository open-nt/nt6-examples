// MIT License - see LICENSE
//
// A push-to-talk switch on a GPIO line, with an optional transmit LED on another. It prints
// "TX on" while the switch is held and "TX off" when it is let go:
//
//   ptt PTT                          a line named PTT in the devicetree
//   ptt gpiochip0:17 --led gpiochip0:18
//   ptt gpiochip0:17 --time 10       quit after ten seconds
//
// The switch is taken to pull the line to ground when pressed, so the line is active low with
// the pull-up turned on.

#include <QtCore>
#include <QtGpio>

#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Reports a push-to-talk switch on a GPIO line."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("line"), QStringLiteral("The switch: a line name or chip:offset."));
    const QCommandLineOption ledOption(QStringLiteral("led"), QStringLiteral("A line that lights while transmitting."),
                                       QStringLiteral("line"));
    const QCommandLineOption timeOption(QStringLiteral("time"), QStringLiteral("Quit after this many seconds."),
                                        QStringLiteral("seconds"));
    parser.addOption(ledOption);
    parser.addOption(timeOption);
    parser.process(app);
    if (parser.positionalArguments().size() != 1)
        parser.showHelp(1);

    QGpioLine ptt(parser.positionalArguments().constFirst());
    ptt.setActiveLow(true);         // pressed pulls the line low
    ptt.setBias(QGpioLine::PullUp); // released floats high
    ptt.setDebounceInterval(20);    // ignore contact bounce
    if (!ptt.open()) {
        std::fprintf(stderr, "ptt: %s\n", qPrintable(ptt.errorString()));
        return 1;
    }

    QGpioLine led(parser.value(ledOption));
    led.setDirection(QGpioLine::Output);
    if (parser.isSet(ledOption) && !led.open()) {
        std::fprintf(stderr, "ptt: %s\n", qPrintable(led.errorString()));
        return 1;
    }

    QObject::connect(&ptt, &QGpioLine::valueChanged, [&](bool pressed) {
        std::printf("TX %s\n", pressed ? "on" : "off");
        std::fflush(stdout);
        if (led.isOpen())
            led.setValue(pressed);
    });

    QObject::connect(&ptt, &QGpioLine::errorOccurred, [&](QGpioLine::GpioError error) {
        if (error == QGpioLine::ResourceError) { // the chip went away
            std::fprintf(stderr, "ptt: %s\n", qPrintable(ptt.errorString()));
            app.exit(1);
        }
    });

    std::printf("PTT %s, %s\n", ptt.value() ? "pressed" : "released",
                ptt.isEventDriven() ? "edge driven" : "polled");
    if (parser.isSet(timeOption))
        QTimer::singleShot(parser.value(timeOption).toInt() * 1000, &app, &QCoreApplication::quit);
    return app.exec();
}
