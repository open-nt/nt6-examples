// MIT License - see LICENSE
// Nt on Zephyr: a timer and a TCP echo server on one event loop.
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

int main() {
    int argc = 1;
    char name[] = "hello";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);

    QTimer heartbeat;
    int beats = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, [&beats] { qInfo() << "heartbeat" << ++beats; });
    heartbeat.start(1000);

    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, [&server] {
        QTcpSocket *client = server.nextPendingConnection();
        QObject::connect(client, &QTcpSocket::readyRead, [client] { client->write(client->readAll()); });
        QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
    });
    if (!server.listen(QHostAddress::Any, 4242))
        qWarning() << "listen failed:" << server.errorString();
    else
        qInfo() << "echo server on port" << server.serverPort();

    return app.exec();
}
