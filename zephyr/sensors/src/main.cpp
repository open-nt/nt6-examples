// MIT License - see LICENSE
// Nt Sensors on Zephyr: lists the sensors that the devicetree names and prints a reading of each every second.
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtSensors/QSensor>

#include <memory>
#include <vector>

int main() {
    int argc = 1;
    char name[] = "sensors";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);

    std::vector<std::unique_ptr<QSensor>> sensors;
    for (const QByteArray &type : QSensor::sensorTypes()) {
        for (const QByteArray &id : QSensor::sensorsForType(type)) {
            auto sensor = std::make_unique<QSensor>(type);
            sensor->setIdentifier(id);
            sensor->setDataRate(1);
            QSensor *s = sensor.get();
            QObject::connect(s, &QSensor::readingChanged, [s] {
                QSensorReading *r = s->reading();
                QDebug line = qInfo().nospace();
                line << s->type() << " " << s->identifier() << ":";
                for (int i = 0; i < r->valueCount(); ++i)
                    line << " " << r->value(i).toDouble();
            });
            QObject::connect(s, &QSensor::sensorError, [s](int error) { qWarning() << s->identifier() << "error" << error; });
            if (s->start())
                qInfo() << "reading" << type << id;
            sensors.push_back(std::move(sensor));
        }
    }
    if (sensors.empty())
        qInfo() << "no sensors: give one a devicetree alias such as accel0 or ambient-temp0";
    return app.exec();
}
