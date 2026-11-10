// MIT License - see LICENSE
//
// A small web service with Nt HTTP Server. Run it and open the address it prints:
//
//   hello_server [port]
//
//   curl http://127.0.0.1:8080/
//   curl http://127.0.0.1:8080/hello/Nt
//   curl http://127.0.0.1:8080/square/12
//   curl -X POST -d '{"a": 2, "b": 3}' http://127.0.0.1:8080/add
//   curl http://127.0.0.1:8080/count
//   curl http://127.0.0.1:8080/files/hello_server.cpp
//   websocket_echo client ws://127.0.0.1:8080/ws hello     (a WebSocket on the same port)
#include <QtCore>
#include <QtHttpServer>
#include <QtNetwork>
#include <QtWebSockets>

#include <cstdio>

//! [main]
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const quint16 port = argc > 1 ? static_cast<quint16>(QString::fromLocal8Bit(argv[1]).toUInt()) : 8080;

    QHttpServer server;
    //! [main]

    //! [routes]
    // A fixed path.
    server.route("/", [] { return "Hello from Nt HTTP Server\n"; });

    // <arg> becomes the parameter: a QString takes one path segment.
    server.route("/hello/<arg>", [](const QString &name) { return QString("Hello, %1!\n").arg(name); });

    // A number must fit the type, so /square/abc and /square/99999999999 do not match.
    server.route("/square/<arg>", [](int n) { return QJsonObject{{"n", n}, {"square", qint64(n) * n}}; });

    // Only POST, with access to the request.
    server.route("/add", QHttpServerRequest::Method::Post, [](const QHttpServerRequest &request) {
        const QJsonObject in = QJsonDocument::fromJson(request.body()).object();
        if (!in.contains("a") || !in.contains("b"))
            return QHttpServerResponse("expected {\"a\": number, \"b\": number}\n",
                                       QHttpServerResponse::StatusCode::BadRequest);
        return QHttpServerResponse(QJsonObject{{"sum", in.value("a").toDouble() + in.value("b").toDouble()}});
    });
    //! [routes]

    //! [responses]
    server.route("/teapot", [] { return QHttpServerResponse::StatusCode::ImATeapot; });
    server.route("/version", [] { return QJsonObject{{"version", QT_VERSION_STR}}; });
    server.route("/robots.txt", [] { return QHttpServerResponse("text/plain", "User-agent: *\nDisallow: /\n"); });
    server.route("/source", [] { return QHttpServerResponse::fromFile(QStringLiteral(__FILE__)); });
    //! [responses]

    //! [responder]
    // Counts to five, one line per piece of the body.
    server.route("/count", [](QHttpServerResponder &responder) {
        responder.writeBeginChunked("text/plain");
        for (int i = 1; i <= 5; ++i)
            responder.writeChunk(QByteArray::number(i) + "\n");
        responder.writeEndChunked("done\n");
    });

    // Streams a file next to this one from disk; the responder deletes the QFile when it is sent.
    server.route("/files/<arg>", [](const QString &name, QHttpServerResponder &responder) {
        if (name.contains("..")) {
            responder.write(QHttpServerResponder::StatusCode::Forbidden);
            return;
        }
        auto *file = new QFile(QFileInfo(QStringLiteral(__FILE__)).absolutePath() + "/" + name);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            responder.write(QHttpServerResponder::StatusCode::NotFound);
            return;
        }
        responder.write(file, "text/plain");
    });

    // Answers a second later. The responder is moved out of the handler, which returns at once.
    server.route("/later", [&app](QHttpServerResponder &responder) {
        auto later = std::make_shared<QHttpServerResponder>(std::move(responder));
        QTimer::singleShot(1000, &app, [later] { later->write("sorry for the wait\n", "text/plain"); });
    });
    //! [responder]

    //! [after]
    server.addAfterRequestHandler(&server, [](const QHttpServerRequest &, QHttpServerResponse &response) {
        QHttpHeaders headers = response.headers();
        headers.append(QHttpHeaders::WellKnownHeader::Server, "hello_server (Nt)");
        response.setHeaders(std::move(headers));
    });
    //! [after]

    //! [websocket]
    // WebSocket upgrades of /ws are accepted on the same port; the sockets echo what they receive.
    server.addWebSocketUpgradeVerifier(&server, [](const QHttpServerRequest &request) {
        if (request.url().path() == "/ws")
            return QHttpServerWebSocketUpgradeResponse::accept();
        return QHttpServerWebSocketUpgradeResponse::passToNext();
    });
    QObject::connect(&server, &QHttpServer::newWebSocketConnection, [&server] {
        QWebSocket *socket = server.nextPendingWebSocketConnection().release();
        QObject::connect(socket, &QWebSocket::textMessageReceived, socket,
                         [socket](const QString &message) { socket->sendTextMessage(message); });
        QObject::connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
    });
    //! [websocket]

    //! [bind]
    auto *tcpServer = new QTcpServer;
    if (!tcpServer->listen(QHostAddress::LocalHost, port) || !server.bind(tcpServer)) {
        std::fprintf(stderr, "cannot listen on port %u: %s\n", port, qPrintable(tcpServer->errorString()));
        delete tcpServer;
        return 1;
    }
    std::printf("listening on http://127.0.0.1:%u/\n", tcpServer->serverPort());
    //! [bind]
    std::fflush(stdout);

    return app.exec();
}
