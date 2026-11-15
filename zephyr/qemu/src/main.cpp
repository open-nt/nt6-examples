// MIT License - see LICENSE
// Nt on Zephyr under QEMU: exercises the event loop, timers, JSON, a TCP and a TLS connection over the
// loopback interface and a worker thread, prints PASS or FAIL, and serves a web page with the results from
// port 8080, which QEMU can forward to the host (see doc/zephyr.h).
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#ifndef QT_NO_SSL
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslServer>
#include <QtNetwork/QSslSocket>
#endif

#include "board.h"
#include "webpage.h"

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char *what, const QString &detail = QString()) {
    std::printf("  %-34s %s\n", what, ok ? "ok" : "FAILED");
    recordResult(QString::fromUtf8(what), ok, detail);
    if (!ok)
        ++g_failures;
}

#ifndef QT_NO_SSL
// A self-signed certificate for "localhost" and its key, made for this sample only. A key that is published
// like this one protects nothing: a product provisions its own.
const char kCertificate[] = "-----BEGIN CERTIFICATE-----\n"
                            "MIIBwzCCAWmgAwIBAgIUEYiBkzEWT3q28i3qL7vmV9bPVHQwCgYIKoZIzj0EAwIw\n"
                            "KDESMBAGA1UEAwwJbG9jYWxob3N0MRIwEAYDVQQKDAlOdCBzYW1wbGUwIBcNMjYx\n"
                            "MTExMTkwMTI0WhgPMjEyNjEwMTgxOTAxMjRaMCgxEjAQBgNVBAMMCWxvY2FsaG9z\n"
                            "dDESMBAGA1UECgwJTnQgc2FtcGxlMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE\n"
                            "3YSYJZRxhsaL1Hykv32HYqXmq3u1ceDIG+W1itvXrsrLS7We6H028d7ney3DfQ97\n"
                            "nC/KGxbJuEg+BbJFNsuPp6NvMG0wHQYDVR0OBBYEFH4J0fpFAzvsIhYxewr5x1V9\n"
                            "qOjWMB8GA1UdIwQYMBaAFH4J0fpFAzvsIhYxewr5x1V9qOjWMA8GA1UdEwEB/wQF\n"
                            "MAMBAf8wGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMAoGCCqGSM49BAMCA0gA\n"
                            "MEUCIEFUK3+MUDAtJbxOATnWeZry7X83NyeERjUkwmFy4S3QAiEAuSozXhei3DgP\n"
                            "1HgYW3X965EcSrgajueRSOEFEEWTKrE=\n"
                            "-----END CERTIFICATE-----\n";
const char kPrivateKey[] = "-----BEGIN PRIVATE KEY-----\n"
                           "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgKdEgDazo+ur/ciNQ\n"
                           "lDuc2MmBi8VtDhGPMTUtz4viDkChRANCAATdhJgllHGGxovUfKS/fYdipeare7Vx\n"
                           "4Mgb5bWK29euystLtZ7ofTbx3ud7LcN9D3ucL8obFsm4SD4FskU2y4+n\n"
                           "-----END PRIVATE KEY-----\n";
#endif

} // namespace

int main() {
    int argc = 1;
    char name[] = "nt_qemu";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);
    std::printf("Nt %s on Zephyr\n", qVersion());

    limitEthernetFrames(); // see board.h

    // The web page comes first, so that it can be opened while the checks run, and shows them as they finish.
    QString webError;
    if (startWebServer(8080, &webError))
        std::printf("  web page on port 8080\n");
    else
        std::printf("  no web page: %s\n", qPrintable(webError));

    // Strings and JSON, with no event loop involved.
    QJsonObject object;
    object.insert(QString("board"), QString("qemu"));
    object.insert(QString("answer"), 42);
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const QJsonObject parsed = QJsonDocument::fromJson(json).object();
    check(parsed.value(QString("answer")).toInt() == 42 && parsed.value(QString("board")).toString() == QString("qemu"),
          "JSON round trip");

    // A repeating timer.
    int ticks = 0;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&ticks] { ++ticks; });
    ticker.start(50);

    // A TCP echo server and a client that talks to it over the loopback interface.
    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, [&server] {
        QTcpSocket *peer = server.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, [peer] { peer->write(peer->readAll()); });
        QObject::connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
    });
    const bool listening = server.listen(QHostAddress(QString("127.0.0.1")), 4242);
    check(listening, "QTcpServer::listen");

    const QByteArray message("hello from Nt");
    QByteArray echoed;
    QTcpSocket client;
    QObject::connect(&client, &QTcpSocket::connected, [&] { client.write(message); });
    QObject::connect(&client, &QTcpSocket::readyRead, [&] { echoed += client.readAll(); });
    client.connectToHost(QHostAddress(QString("127.0.0.1")), 4242);

#ifndef QT_NO_SSL
    // The same over TLS: the server presents the certificate, and the client trusts exactly that one and
    // checks that it was issued to "localhost".
    const QSslCertificate certificate(QByteArray(kCertificate), QSsl::Pem);
    const QSslKey privateKey(QByteArray(kPrivateKey), QSsl::Ec, QSsl::Pem);
    check(!certificate.isNull() &&
              certificate.subjectInfo(QSslCertificate::CommonName).value(0) == QString("localhost"),
          "QSslCertificate from PEM");
    check(!privateKey.isNull() && privateKey.length() == 256, "QSslKey from PEM");

    QSslConfiguration serverConfiguration;
    serverConfiguration.setLocalCertificate(certificate);
    serverConfiguration.setPrivateKey(privateKey);
    QSslServer secureServer;
    secureServer.setSslConfiguration(serverConfiguration);
    secureServer.setHandshakeTimeout(
        120000); // a processor with no help for the cryptography may need longer than the default
    QObject::connect(&secureServer, &QSslServer::errorOccurred, [](QSslSocket *socket, QAbstractSocket::SocketError) {
        std::printf("  (server refused a client: %s)\n", socket->errorString().toUtf8().constData());
    });
    QObject::connect(&secureServer, &QTcpServer::pendingConnectionAvailable, [&secureServer] {
        QTcpSocket *peer = secureServer.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, [peer] { peer->write(peer->readAll()); });
        QObject::connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
    });
    check(secureServer.listen(QHostAddress(QString("127.0.0.1")), 4443), "QSslServer::listen");

    const QByteArray secret("hello over TLS");
    QByteArray secureEcho;
    bool encrypted = false;
    QSslConfiguration clientConfiguration;
    clientConfiguration.setCaCertificates({certificate});
    QSslSocket secureClient;
    secureClient.setSslConfiguration(clientConfiguration);
    QObject::connect(&secureClient, &QSslSocket::encrypted, [&] {
        encrypted = true;
        secureClient.write(secret);
    });
    QObject::connect(&secureClient, &QSslSocket::readyRead, [&] { secureEcho += secureClient.readAll(); });
    secureClient.connectToHostEncrypted(QString("127.0.0.1"), 4443, QString("localhost"));

    // A client that trusts no certificate must refuse the same server.
    bool refused = false;
    QSslConfiguration strangerConfiguration;
    strangerConfiguration.setCaCertificates({});
    QSslSocket stranger;
    stranger.setSslConfiguration(strangerConfiguration);
    QObject::connect(&stranger, &QSslSocket::sslErrors, [&](const QList<QSslError> &) { refused = true; });
    QObject::connect(&stranger, &QSslSocket::encrypted, [&] { refused = false; });
    stranger.connectToHostEncrypted(QString("127.0.0.1"), 4443, QString("localhost"));
#endif

    // A worker thread that reports back with a queued call into the main thread's event loop.
    bool workerRan = false;
    bool deliveredOnMainThread = false;
    QThread *mainThread = QThread::currentThread();
    QThread *worker = QThread::create([&] {
        const bool otherThread = QThread::currentThread() != mainThread;
        QMetaObject::invokeMethod(
            &app,
            [&, otherThread] {
                workerRan = otherThread;
                deliveredOnMainThread = QThread::currentThread() == mainThread;
            },
            Qt::QueuedConnection);
    });
    worker->start();

    // Judge what happened once everything has, or after two minutes: the handshakes take a while on an
    // emulated processor, which computes the elliptic curves in software.
    const auto done = [&] {
#ifndef QT_NO_SSL
        if (secureEcho != secret || !refused)
            return false;
#endif
        return echoed == message && workerRan && ticks >= 3;
    };
    QTimer watcher;
    bool finished = false;
    const auto finish = [&] {
        if (finished)
            return;
        finished = true;
        watcher.stop();
        check(ticks >= 3, "QTimer ticks");
        check(echoed == message, "TCP echo over loopback");
        if (echoed != message)
            qWarning() << "  client:" << client.state() << client.errorString() << "server:" << server.errorString();
#ifndef QT_NO_SSL
        check(encrypted, "TLS handshake");
        if (!encrypted)
            qWarning() << "  client:" << secureClient.errorString();
        else
            std::printf("  %s, %s\n", secureClient.sessionCipher().name().toUtf8().constData(),
                        secureClient.sessionCipher().protocolString().toUtf8().constData());
        check(secureEcho == secret, "TLS echo over loopback",
              encrypted ? secureClient.sessionCipher().name() + ", " + secureClient.sessionCipher().protocolString()
                        : QString());
        check(refused, "untrusted certificate refused");
#endif
        check(workerRan, "QThread ran");
        check(deliveredOnMainThread, "queued call from the thread");
        recordFinished();
        std::printf("Nt sample: %s\n", g_failures == 0 ? "PASS" : "FAIL");
        // The web page goes on being served: there is no reason to leave the event loop.
    };
    QObject::connect(&watcher, &QTimer::timeout, [&] {
        if (done())
            finish();
    });
    watcher.start(100);
    QTimer::singleShot(120000, &app, finish);
    const int code = app.exec();
    worker->wait();
    delete worker;
    return code != 0 ? code : g_failures;
}
