// MIT License - see LICENSE
//
// Lists the gamepads and prints what the first one does: buttons, sticks and triggers, and when it
// is plugged in or out. Pressing A rumbles the pad, if it can.
//
//   gamepadinfo                              follow the first pad
//   gamepadinfo --device 1                   follow pad 1
//   gamepadinfo --mappings gamecontrollerdb.txt
//   gamepadinfo --time 10                    quit after ten seconds

#include <QtCore>
#include <QtGamepad>

#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Prints what a gamepad does."));
    parser.addHelpOption();
    const QCommandLineOption deviceOption(QStringLiteral("device"), QStringLiteral("The id of the pad to follow."),
                                          QStringLiteral("id"), QStringLiteral("0"));
    const QCommandLineOption mappingsOption(QStringLiteral("mappings"),
                                            QStringLiteral("An SDL GameControllerDB file of mappings."),
                                            QStringLiteral("file"));
    const QCommandLineOption deadZoneOption(QStringLiteral("dead-zone"),
                                            QStringLiteral("The dead zone of the sticks, 0.0 to 0.9."),
                                            QStringLiteral("radius"), QStringLiteral("0.1"));
    const QCommandLineOption timeOption(QStringLiteral("time"), QStringLiteral("Quit after this many seconds."),
                                        QStringLiteral("seconds"));
    parser.addOption(deviceOption);
    parser.addOption(mappingsOption);
    parser.addOption(deadZoneOption);
    parser.addOption(timeOption);
    parser.process(app);

    QGamepadManager *manager = QGamepadManager::instance();

    if (parser.isSet(mappingsOption)) {
        const int added = manager->addMappingsFromFile(parser.value(mappingsOption));
        if (added < 0) {
            std::fprintf(stderr, "gamepadinfo: cannot read %s\n", qPrintable(parser.value(mappingsOption)));
            return 1;
        }
        std::printf("%d mappings added\n", added);
    }
    manager->setDeadZone(parser.value(deadZoneOption).toDouble());

    const QList<int> ids = manager->connectedGamepads();
    if (ids.isEmpty())
        std::printf("No gamepads yet; plug one in.\n");
    for (const int id : ids)
        std::printf("Pad %d: %s\n  %s\n", id, qPrintable(manager->gamepadName(id)), qPrintable(manager->mapping(id)));
    std::fflush(stdout);

    QGamepad gamepad(parser.value(deviceOption).toInt());

    QObject::connect(&gamepad, &QGamepad::buttonAChanged, [](bool pressed) {
        std::printf("A %s\n", pressed ? "pressed" : "released");
        std::fflush(stdout);
    });
    QObject::connect(&gamepad, &QGamepad::axisLeftXChanged, [&](double x) {
        std::printf("left stick %.2f, %.2f\n", x, gamepad.axisLeftY());
        std::fflush(stdout);
    });

    const QMetaEnum buttons = QMetaEnum::fromType<QGamepadManager::GamepadButton>();
    QObject::connect(&gamepad, &QGamepad::buttonValueChanged, [&](QGamepadManager::GamepadButton button, double value) {
        std::printf("%s %.2f\n", buttons.valueToKey(button), value);
        std::fflush(stdout);
    });
    const QMetaEnum axes = QMetaEnum::fromType<QGamepadManager::GamepadAxis>();
    QObject::connect(&gamepad, &QGamepad::axisValueChanged, [&](QGamepadManager::GamepadAxis axis, double value) {
        std::printf("%s %.2f\n", axes.valueToKey(axis), value);
        std::fflush(stdout);
    });

    QObject::connect(&gamepad, &QGamepad::connectedChanged, [&](bool connected) {
        std::printf("Pad %d %s %s\n", gamepad.deviceId(), connected ? "connected:" : "disconnected",
                    qPrintable(gamepad.name()));
        std::fflush(stdout);
    });

    QObject::connect(&gamepad, &QGamepad::buttonAChanged, [&](bool pressed) {
        if (pressed && gamepad.canRumble())
            gamepad.rumble(0.8, 0.4, 200); // strong motor, weak motor, milliseconds
    });

    if (parser.isSet(timeOption))
        QTimer::singleShot(parser.value(timeOption).toInt() * 1000, &app, &QCoreApplication::quit);
    return app.exec();
}
