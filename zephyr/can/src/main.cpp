// MIT License - see LICENSE
// Nt SerialBus on Zephyr: the CAN controller that zephyr,canbus chooses (FlexCAN3 on the MIMXRT1170-EVKB) in
// loopback mode, so that it needs neither a transceiver nor a bus. A frame goes out every second and comes back
// through the controller; set LoopbackKey to false to send onto a real bus at 500 kbit/s.
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtSerialBus/QCanBus>
#include <QtSerialBus/QCanBusDevice>

#include <memory>

int main() {
    int argc = 1;
    char name[] = "can";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);

    for (const QCanBusDeviceInfo &info : QCanBus::instance()->availableDevices(QStringLiteral("zephyrcan")))
        qInfo() << "controller" << info.name() << info.description() << (info.hasFlexibleDataRate() ? "CAN FD" : "");

    QString error;
    std::unique_ptr<QCanBusDevice> device(
        QCanBus::instance()->createDevice(QStringLiteral("zephyrcan"), QStringLiteral("canbus"), &error));
    if (!device) {
        qWarning() << error;
        return 1;
    }
    device->setConfigurationParameter(QCanBusDevice::LoopbackKey, true);
    device->setConfigurationParameter(QCanBusDevice::BitRateKey, 500000);
    QObject::connect(device.get(), &QCanBusDevice::framesReceived, [&device] {
        for (const QCanBusFrame &frame : device->readAllFrames()) {
            if (frame.frameType() == QCanBusFrame::ErrorFrame)
                qInfo() << "error frame:" << device->interpretErrorFrame(frame);
            else
                qInfo() << "received" << frame.toString();
        }
    });
    QObject::connect(device.get(), &QCanBusDevice::errorOccurred,
                     [&device](QCanBusDevice::CanBusError) { qWarning() << device->errorString(); });
    if (!device->connectDevice()) {
        qWarning() << "cannot connect:" << device->errorString();
        return 1;
    }
    qInfo() << "connected; bus status" << int(device->busStatus());

    quint8 counter = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QCanBusFrame frame(0x123, QByteArray(1, char(counter++)));
        if (device->writeFrame(frame))
            qInfo() << "sent    " << frame.toString();
    });
    timer.start(1000);
    return app.exec();
}
