// MIT License - see LICENSE
// Nt SerialPort on Zephyr: lists the UARTs of the board, opens one (lpuart2 on
// the MIMXRT1170-EVKB, which the overlay puts in loopback mode) and writes a
// line every second, which comes back through the UART and is printed. On a
// board without lpuart2, change the name to another port that availablePorts()
// lists; not the one of the console.
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

int main() {
  int argc = 1;
  char name[] = "serial";
  char *argv[] = {name, nullptr};
  QCoreApplication app(argc, argv);

  for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
    qInfo() << "port" << info.portName() << info.systemLocation();

  QSerialPort port;
  port.setPortName(QStringLiteral("lpuart2"));
  port.setBaudRate(QSerialPort::Baud115200);
  QObject::connect(&port, &QSerialPort::readyRead, [&port] {
    while (port.canReadLine())
      qInfo() << "received" << port.readLine().trimmed();
  });
  QObject::connect(&port, &QSerialPort::errorOccurred,
                   [&port](QSerialPort::SerialPortError error) {
                     if (error != QSerialPort::NoError)
                       qWarning() << "error:" << port.errorString();
                   });
  if (!port.open(QIODevice::ReadWrite)) {
    qWarning() << "cannot open" << port.portName() << port.errorString();
    return 1;
  }
  qInfo() << "opened" << port.portName() << "at" << port.baudRate() << "baud";

  int counter = 0;
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    const QByteArray line = "line " + QByteArray::number(counter++) + "\n";
    port.write(line);
    qInfo() << "sent    " << line.trimmed();
  });
  timer.start(1000);
  return app.exec();
}
