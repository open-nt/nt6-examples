// MIT License - see LICENSE
//
// A QML document driving a C++ object: a thermostat whose target follows the hour of the day, with a
// Timer, bindings, signal handlers and a type written in C++.
//
//   hello_qml               runs the embedded document for a few seconds
//   hello_qml main.qml      runs a document of your own, with the Thermostat type available
#include <QCoreApplication>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
#include <QtQml>

class Thermostat : public QObject {
    Q_OBJECT
    Q_PROPERTY(double target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(double current READ current NOTIFY currentChanged)
public:
    double target() const { return m_target; }
    void setTarget(double t) {
        if (t == m_target)
            return;
        m_target = t;
        emit targetChanged();
    }
    double current() const { return m_current; }
    // One step of a very simple heater.
    Q_INVOKABLE void step() {
        m_current += (m_target - m_current) * 0.5;
        emit currentChanged();
    }

signals:
    void targetChanged() { emitSignal(&Thermostat::targetChanged); }
    void currentChanged() { emitSignal(&Thermostat::currentChanged); }

private:
    double m_target = 20;
    double m_current = 15;
};

static const char document[] = R"QML(
import QtQml
import Home 1.0

QtObject {
    id: root
    property int hour: 6
    property bool night: hour < 7 || hour > 21

    property Thermostat thermostat: Thermostat {
        target: root.night ? 17 : 21
        onCurrentChanged: console.log("hour", root.hour, "current", current.toFixed(1), "target", target)
    }

    property Timer clock: Timer {
        interval: 100; repeat: true; running: true
        onTriggered: {
            thermostat.step()
            if (++root.hour > 23)
                Qt.quit()
        }
    }

    Component.onCompleted: console.log("started at", hour, "with", greeting)
}
)QML";

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qmlRegisterType<Thermostat>("Home", 1, 0, "Thermostat");

    if (argc > 1) {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("greeting", QStringLiteral("hello"));
        engine.load(QString::fromLocal8Bit(argv[1]));
        if (engine.rootObjects().isEmpty())
            return 1;
        return app.exec();
    }

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("greeting", QStringLiteral("hello"));
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit);

    QQmlComponent component(&engine);
    component.setData(document, QUrl(QStringLiteral("file:///home.qml")));
    QObject *home = component.create();
    if (!home) {
        qWarning().noquote() << component.errorString();
        return 1;
    }

    const int result = app.exec();
    delete home;
    return result;
}

#include "hello_qml.moc"
