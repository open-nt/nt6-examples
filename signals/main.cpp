// MIT License - see LICENSE
//
// The patterns of the "Signals and Slots" guide, in one program. It checks each step and exits
// with 0 if all of them behaved as described.
#include "thermostat.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>
#include <memory>

static int g_failures = 0;
static void expect(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    //! [connect]
    Thermostat thermostat;
    Display display;
    // Sender, &Class::signal, receiver, &Class::slot.
    QObject::connect(&thermostat, &Thermostat::temperatureChanged, &display, &Display::show);
    thermostat.setTemperature(21.5);
    // display.text is now "21.5 C": the slot ran inside setTemperature().
    //! [connect]
    expect(display.text == "21.5 C", "a member function slot runs when the signal is emitted");

    //! [lambda]
    int alarms = 0;
    // A lambda needs no receiver. Give it a context object anyway (here &display) when it uses
    // something that can be destroyed: the connection is removed together with the context.
    QObject::connect(&thermostat, &Thermostat::overheated, &display, [&alarms] { ++alarms; });
    thermostat.setTemperature(35.0);
    //! [lambda]
    expect(alarms == 1, "a lambda runs when the signal is emitted");

    //! [fewer-arguments]
    // A slot may ignore the trailing arguments of the signal.
    int changes = 0;
    QObject::connect(&thermostat, &Thermostat::temperatureChanged, [&changes] { ++changes; });
    //! [fewer-arguments]
    thermostat.setTemperature(35.0); // no change: no signal
    thermostat.setTemperature(18.0);
    expect(changes == 1, "a slot without arguments connects to a signal with one");

    //! [context]
    {
        auto window = std::make_unique<Display>();
        QObject::connect(&thermostat, &Thermostat::temperatureChanged, window.get(), &Display::show);
    } // the Display is destroyed here, and so is its connection
    thermostat.setTemperature(19.0); // safe: nothing calls into the destroyed object
    //! [context]
    expect(true, "a connection to a destroyed receiver is removed");

    //! [disconnect]
    const QMetaObject::Connection logging =
        QObject::connect(&thermostat, &Thermostat::temperatureChanged, [](double c) { std::printf("%.1f\n", c); });
    QObject::disconnect(logging); // remove just this connection
    // ...or every connection from a signal to one receiver:
    QObject::disconnect(&thermostat, &Thermostat::temperatureChanged, &display, nullptr);
    //! [disconnect]
    thermostat.setTemperature(25.0);
    expect(display.text == "19 C", "disconnected slots no longer run");

    //! [flags]
    // Called once, then disconnected automatically.
    int firstReading = 0;
    QObject::connect(
        &thermostat, &Thermostat::temperatureChanged, &display, [&firstReading] { ++firstReading; },
        Qt::SingleShotConnection);
    // Not connected twice, however often this line runs.
    QObject::connect(&thermostat, &Thermostat::temperatureChanged, &display, &Display::show, Qt::UniqueConnection);
    QObject::connect(&thermostat, &Thermostat::temperatureChanged, &display, &Display::show, Qt::UniqueConnection);
    //! [flags]
    thermostat.setTemperature(26.0);
    thermostat.setTemperature(27.0);
    expect(firstReading == 1, "a single-shot connection runs once");
    expect(display.text == "27 C", "a unique connection is made once");

    //! [queued]
    // A queued connection runs the slot later, from the event loop, as connections to objects in
    // other threads do. Nothing happens until the loop runs.
    double queued = 0;
    QObject::connect(
        &thermostat, &Thermostat::temperatureChanged, &display, [&queued](double c) { queued = c; },
        Qt::QueuedConnection);
    thermostat.setTemperature(22.0);
    // queued is still 0 here.
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    app.exec(); // the slot runs now: queued becomes 22
    //! [queued]
    expect(queued == 22.0, "a queued slot runs from the event loop");

    return g_failures == 0 ? 0 : 1;
}
