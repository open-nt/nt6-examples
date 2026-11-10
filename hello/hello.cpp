#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QTimer>

#include <iostream>

Q_DECLARE_LOGGING_CATEGORY(lcHello)
Q_LOGGING_CATEGORY(lcHello, "hello")

class Counter : public QObject {
    Q_OBJECT
public:
    explicit Counter(QObject *parent = nullptr) : QObject(parent) {}

    void setValue(int v) {
        if (v == m_value)
            return;
        m_value = v;
        emit valueChanged(v);
    }

signals:
    void valueChanged(int value) { emitSignal(&Counter::valueChanged, value); }

private:
    int m_value = 0;
};

// Parses a positive integer option, reporting a bad value through the log.
static bool positiveValue(const QCommandLineParser &parser, const QString &name, int &out) {
    bool ok = false;
    const int v = parser.value(name).toInt(&ok);
    if (!ok || v <= 0) {
        qCWarning(lcHello, "invalid value \"%s\" for --%s", qUtf8Printable(parser.value(name)), qUtf8Printable(name));
        return false;
    }
    out = v;
    return true;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("hello");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Counts up on a timer and prints each value.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {{"c", "count"}, "Stop after <n> ticks (default 3).", "n", "3"},
        {{"i", "interval"}, "Time between ticks in milliseconds (default 100).", "ms", "100"},
        {{"d", "debug"}, "Enable debug logging."},
    });
    parser.process(app);

    QLoggingCategory::setFilterRules(parser.isSet("debug") ? "hello.debug=true" : "hello.debug=false");

    int count = 0;
    int interval = 0;
    if (!positiveValue(parser, "count", count) || !positiveValue(parser, "interval", interval))
        return 2;
    qCInfo(lcHello) << "starting:" << count << "ticks every" << interval << "ms";

    Counter counter;
    QObject::connect(&counter, &Counter::valueChanged, [](int v) {
        qCDebug(lcHello) << "valueChanged" << v;
        std::cout << "value = " << v << "\n";
    });

    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        counter.setValue(++ticks);
        if (ticks == count) {
            qCInfo(lcHello) << "done";
            QCoreApplication::quit();
        }
    });
    timer.start(interval);

    return app.exec();
}
