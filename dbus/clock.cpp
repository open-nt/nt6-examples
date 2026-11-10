// MIT License - see LICENSE
//
// A clock exported on the session bus, and a client for it.
//
//   clock server        exports /clock as org.example.Clock and waits
//   clock client        calls the clock, reads a property and waits for one signal

#include <QtCore>
#include <QtDBus>

#include <cstdio>

//! [adaptor]
class Clock : public QObject {
    Q_OBJECT
public:
    QString now() const { return QDateTime::currentDateTime().toString(Qt::ISODate); }
signals:
    void alarm(const QString &message);
};

class ClockAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.example.Clock")
    Q_PROPERTY(int volume READ volume WRITE setVolume)
public:
    explicit ClockAdaptor(Clock *clock) : QDBusAbstractAdaptor(clock), m_clock(clock) {
        setAutoRelaySignals(true); // Clock::alarm() is emitted as ClockAdaptor::alarm()
    }
    int volume() const { return m_volume; }
    void setVolume(int volume) { m_volume = volume; }

public slots:
    QString Now() { return m_clock->now(); }
    void SetAlarm(int seconds, const QString &message) {
        QTimer::singleShot(seconds * 1000, m_clock, [this, message] { emit m_clock->alarm(message); });
    }

signals:
    void alarm(const QString &message);

private:
    Clock *m_clock;
    int m_volume = 5;
};
//! [adaptor]

//! [proxy]
class ClockInterface : public QDBusAbstractInterface {
    Q_OBJECT
public:
    explicit ClockInterface(const QDBusConnection &connection, QObject *parent = nullptr)
        : QDBusAbstractInterface("org.example.Clock", "/clock", "org.example.Clock", connection, parent) {}

    QDBusPendingReply<QString> Now() { return asyncCall("Now"); }
    QDBusPendingReply<> SetAlarm(int seconds, const QString &message) {
        return asyncCall("SetAlarm", seconds, message);
    }

signals:
    void alarm(const QString &message); // emitted when the remote clock emits it
};
//! [proxy]

static int server() {
    //! [server]
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "No session bus: %s\n", qPrintable(bus.lastError().message()));
        return 1;
    }
    auto *clock = new Clock;
    new ClockAdaptor(clock);
    bus.registerObject("/clock", clock);
    if (!bus.registerService("org.example.Clock")) {
        std::fprintf(stderr, "org.example.Clock is taken: %s\n", qPrintable(bus.lastError().message()));
        return 1;
    }
    //! [server]
    std::printf("Serving org.example.Clock as %s\n", qPrintable(bus.baseService()));
    return QCoreApplication::exec();
}

static int client() {
    //! [client]
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface clock("org.example.Clock", "/clock", "org.example.Clock", bus);
    QDBusReply<QString> now = clock.call("Now");
    if (!now.isValid()) {
        std::fprintf(stderr, "Cannot reach the clock: %s\n", qPrintable(now.error().message()));
        return 1;
    }
    std::printf("It is %s, volume %d\n", qPrintable(now.value()), clock.property("volume").toInt());
    //! [client]

    ClockInterface proxy(bus);
    QObject::connect(&proxy, &ClockInterface::alarm, [](const QString &message) {
        std::printf("Alarm: %s\n", qPrintable(message));
        QCoreApplication::quit();
    });
    proxy.SetAlarm(1, "tea is ready");
    return QCoreApplication::exec();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.value(1) == "server")
        return server();
    if (args.value(1) == "client")
        return client();
    std::fprintf(stderr, "usage: %s server|client\n", argv[0]);
    return 2;
}

#include "clock.moc"
