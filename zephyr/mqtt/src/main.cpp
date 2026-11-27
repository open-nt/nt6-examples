// MIT License - see LICENSE
// Nt Mqtt on Zephyr: the board gets an address with DHCP, connects to an MQTT
// broker, announces itself on nt/<id>/status (retained, with "offline" as its
// last will), publishes its uptime every five seconds on nt/<id>/uptime and
// prints what arrives on nt/<id>/commands/#. Try it with Mosquitto on the host:
//
//   mosquitto -v -c <(printf 'listener 1883\nallow_anonymous true\n')
//   mosquitto_sub -t 'nt/#' -v
//   mosquitto_pub -t nt/evkb-1/commands/hello -m world
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtMqtt/QMqttClient>

namespace {
// The address of the broker. Change it to that of the machine that runs
// Mosquitto.
const char kBroker[] = "192.168.1.10";
// The client identifier. A board without an entropy driver needs one of its
// own, since the random one that QMqttClient makes otherwise comes from
// QRandomGenerator::system().
const char kClientId[] = "evkb-1";
} // namespace

int main() {
  int argc = 1;
  char name[] = "mqtt";
  char *argv[] = {name, nullptr};
  QCoreApplication app(argc, argv);

  const QString id = QString::fromLatin1(kClientId);
  const QString status = QStringLiteral("nt/%1/status").arg(id);
  QMqttClient client;
  client.setHostname(QString::fromLatin1(kBroker));
  client.setPort(1883);
  client.setClientId(id);
  client.setWillTopic(status);
  client.setWillMessage("offline");
  client.setWillRetain(true);

  QElapsedTimer uptime;
  uptime.start();
  QTimer telemetry;
  QObject::connect(&telemetry, &QTimer::timeout, [&] {
    client.publish(QMqttTopicName(QStringLiteral("nt/%1/uptime").arg(id)),
                   QByteArray::number(uptime.elapsed() / 1000));
  });

  QObject::connect(&client, &QMqttClient::connected, [&] {
    qInfo() << "connected to" << client.hostname() << "as" << client.clientId();
    client.publish(QMqttTopicName(status), "online", 1, true);
    QMqttSubscription *commands = client.subscribe(
        QMqttTopicFilter(QStringLiteral("nt/%1/commands/#").arg(id)), 1);
    QObject::connect(commands, &QMqttSubscription::messageReceived,
                     [](const QMqttMessage &message) {
                       qInfo() << "command" << message.topic().name()
                               << message.payload();
                     });
    telemetry.start(5000);
  });
  // Reconnect after a failure, for example while the network comes up.
  QObject::connect(&client, &QMqttClient::disconnected, [&] {
    telemetry.stop();
    qWarning() << "disconnected, error" << client.error()
               << "- retrying in 5 s";
    QTimer::singleShot(5000, &client, &QMqttClient::connectToHost);
  });
  QTimer::singleShot(3000, &client, &QMqttClient::connectToHost); // after DHCP
  return app.exec();
}
