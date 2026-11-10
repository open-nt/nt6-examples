// MIT License - see LICENSE
//
// A small serial terminal. With --list it prints the serial ports of the
// machine; otherwise it connects standard input and output to a port:
//
//   serialterm --list
//   serialterm ttyUSB0 --baud 115200
//   printf 'AT\r\n' | serialterm ttyUSB0 --time 2

#include <QtCore>
#include <QtSerialPort>

#include <cstdio>
#include <unistd.h>

namespace {

int listPorts() {
    //! [list]
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString line = info.portName() + "\t" + info.systemLocation() + "\t" + info.description();
        if (info.hasVendorIdentifier() && info.hasProductIdentifier())
            line += "\t" + QString::number(info.vendorIdentifier(), 16).rightJustified(4, QChar(u'0')) + ":" +
                    QString::number(info.productIdentifier(), 16).rightJustified(4, QChar(u'0'));
        std::printf("%s\n", qPrintable(line));
    }
    //! [list]
    return 0;
}

// Maps the text of a command line option, such as "even" or "2", to a setting.
// Reports the choices and returns false when the text is not one of them.
template <typename T>
bool choose(const QString &option, const QString &text, const QList<std::pair<const char *, T>> &choices, T *out) {
    for (const auto &choice : choices) {
        if (text == QLatin1String(choice.first)) {
            *out = choice.second;
            return true;
        }
    }
    QStringList names;
    for (const auto &choice : choices)
        names << QString::fromLatin1(choice.first);
    std::fprintf(stderr, "serialterm: --%s must be one of: %s\n", qPrintable(option), qPrintable(names.join(", ")));
    return false;
}

}  // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("Connects standard input and output to a serial port.");
    parser.addHelpOption();
    parser.addPositionalArgument("port", "Port name, such as ttyUSB0 or /dev/ttyS0.", "[port]");
    parser.addOption(QCommandLineOption({"l", "list"}, "List the serial ports and exit."));
    parser.addOption(QCommandLineOption({"b", "baud"}, "Baud rate (default 9600).", "rate", "9600"));
    parser.addOption(QCommandLineOption({"d", "data-bits"}, "Data bits: 5, 6, 7 or 8 (default 8).", "bits", "8"));
    parser.addOption(QCommandLineOption({"p", "parity"}, "Parity: none, even, odd, space or mark.", "parity", "none"));
    parser.addOption(QCommandLineOption({"s", "stop-bits"}, "Stop bits: 1 or 2 (default 1).", "bits", "1"));
    parser.addOption(
        QCommandLineOption({"f", "flow"}, "Flow control: none, hardware or software.", "control", "none"));
    parser.addOption(QCommandLineOption({"t", "time"}, "Quit after this many seconds (default: run until the input "
                                                       "ends, then wait for the last reply).",
                                        "seconds", "0"));
    parser.process(app);

    if (parser.isSet("list"))
        return listPorts();

    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1) {
        std::fprintf(stderr, "serialterm: give one port name, or --list\n%s", qPrintable(parser.helpText()));
        return 2;
    }

    //! [settings]
    QSerialPort port(positional.first());

    bool ok = false;
    const qint32 baud = parser.value("baud").toInt(&ok);
    if (!ok || baud <= 0) {
        std::fprintf(stderr, "serialterm: --baud must be a positive number\n");
        return 2;
    }
    QSerialPort::DataBits dataBits{};
    QSerialPort::Parity parity{};
    QSerialPort::StopBits stopBits{};
    QSerialPort::FlowControl flow{};
    if (!choose<QSerialPort::DataBits>("data-bits", parser.value("data-bits"),
                                       {{"5", QSerialPort::Data5},
                                        {"6", QSerialPort::Data6},
                                        {"7", QSerialPort::Data7},
                                        {"8", QSerialPort::Data8}},
                                       &dataBits) ||
        !choose<QSerialPort::Parity>("parity", parser.value("parity"),
                                     {{"none", QSerialPort::NoParity},
                                      {"even", QSerialPort::EvenParity},
                                      {"odd", QSerialPort::OddParity},
                                      {"space", QSerialPort::SpaceParity},
                                      {"mark", QSerialPort::MarkParity}},
                                     &parity) ||
        !choose<QSerialPort::StopBits>("stop-bits", parser.value("stop-bits"),
                                       {{"1", QSerialPort::OneStop}, {"2", QSerialPort::TwoStop}}, &stopBits) ||
        !choose<QSerialPort::FlowControl>("flow", parser.value("flow"),
                                          {{"none", QSerialPort::NoFlowControl},
                                           {"hardware", QSerialPort::HardwareControl},
                                           {"software", QSerialPort::SoftwareControl}},
                                          &flow))
        return 2;

    port.setBaudRate(baud);
    port.setDataBits(dataBits);
    port.setParity(parity);
    port.setStopBits(stopBits);
    port.setFlowControl(flow);
    //! [settings]

    //! [open]
    if (!port.open(QIODevice::ReadWrite)) {
        std::fprintf(stderr, "serialterm: cannot open %s: %s\n", qPrintable(port.portName()),
                     qPrintable(port.errorString()));
        return 1;
    }
    //! [open]

    int exitCode = 0;

    //! [read]
    QObject::connect(&port, &QIODevice::readyRead, [&port] {
        const QByteArray data = port.readAll();
        std::fwrite(data.constData(), 1, static_cast<std::size_t>(data.size()), stdout);
        std::fflush(stdout);
    });
    //! [read]

    //! [errors]
    QObject::connect(&port, &QSerialPort::errorOccurred, [&](QSerialPort::SerialPortError error) {
        std::fprintf(stderr, "serialterm: %s\n", qPrintable(port.errorString()));
        // A device that disappears, for example an unplugged USB adapter, cannot recover.
        if (error == QSerialPort::ResourceError) {
            exitCode = 1;
            QCoreApplication::quit();
        }
    });
    //! [errors]

    // When the input ends, give the device a moment to send its last reply.
    const int seconds = parser.value("time").toInt();
    QTimer quitTimer;
    quitTimer.setSingleShot(true);
    QObject::connect(&quitTimer, &QTimer::timeout, &app, &QCoreApplication::quit);
    if (seconds > 0)
        quitTimer.start(seconds * 1000);

    //! [write]
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, [&] {
        char buffer[512];
        const ssize_t n = ::read(STDIN_FILENO, buffer, sizeof buffer);
        if (n > 0) {
            port.write(buffer, n);
            return;
        }
        input.setEnabled(false);
        if (!quitTimer.isActive())
            quitTimer.start(500);
    });
    //! [write]

    const int result = app.exec();
    port.close();
    return exitCode != 0 ? exitCode : result;
}
