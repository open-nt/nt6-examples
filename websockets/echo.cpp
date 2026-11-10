// MIT License - see LICENSE
//
// A WebSocket echo server and a client for it:
//
//   websocket_echo server [port]          answers each message with the same message
//   websocket_echo client <url> <text>    sends <text> and prints the answer
//
// Try the server with a browser console:
//   const ws = new WebSocket("ws://127.0.0.1:1234"); ws.onmessage = e => console.log(e.data); ws.send("hi")
#include <QtCore>
#include <QtWebSockets>

#include <cstdio>

static int runServer(QCoreApplication &app, quint16 port) {
    //! [server]
    QWebSocketServer server(QStringLiteral("Nt echo"), QWebSocketServer::NonSecureMode);
    if (!server.listen(QHostAddress::LocalHost, port)) {
        std::fprintf(stderr, "cannot listen: %s\n", qPrintable(server.errorString()));
        return 1;
    }
    QObject::connect(&server, &QWebSocketServer::newConnection, [&server] {
        QWebSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QWebSocket::textMessageReceived, socket,
                         [socket](const QString &message) { socket->sendTextMessage(message); });
        QObject::connect(socket, &QWebSocket::binaryMessageReceived, socket,
                         [socket](const QByteArray &message) { socket->sendBinaryMessage(message); });
        QObject::connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
    });
    //! [server]

    //! [origin]
    // Browsers send the origin of the page; refuse pages from other sites.
    QObject::connect(&server, &QWebSocketServer::originAuthenticationRequired,
                     [](QWebSocketCorsAuthenticator *authenticator) {
                         const QString origin = authenticator->origin();
                         authenticator->setAllowed(origin.isEmpty() || origin.startsWith("http://127.0.0.1") ||
                                                   origin.startsWith("http://localhost"));
                     });
    //! [origin]

    //! [subprotocols]
    server.setSupportedSubprotocols({QStringLiteral("echo.v2"), QStringLiteral("echo.v1")});
    //! [subprotocols]

    std::printf("listening on %s\n", qPrintable(server.serverUrl().toString()));
    std::fflush(stdout);
    return app.exec();
}

static int runClient(QCoreApplication &app, const QUrl &url, const QString &text) {
    //! [client]
    QWebSocket socket;
    QObject::connect(&socket, &QWebSocket::connected, [&] {
        std::printf("connected, subprotocol \"%s\"\n", qPrintable(socket.subprotocol()));
        socket.sendTextMessage(text);
    });
    QObject::connect(&socket, &QWebSocket::textMessageReceived, [&](const QString &message) {
        std::printf("received: %s\n", qPrintable(message));
        socket.close();
    });
    QObject::connect(&socket, &QWebSocket::disconnected, &app, &QCoreApplication::quit);
    QObject::connect(&socket, &QWebSocket::errorOccurred, [&](QAbstractSocket::SocketError) {
        std::fprintf(stderr, "error: %s\n", qPrintable(socket.errorString()));
        app.exit(1);
    });
    //! [client]

    QWebSocketHandshakeOptions options;
    options.setSubprotocols({QStringLiteral("echo.v1")});
    socket.open(url, options);
    return app.exec();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() >= 2 && args.at(1) == QLatin1StringView("server"))
        return runServer(app, args.size() > 2 ? static_cast<quint16>(args.at(2).toUInt()) : 1234);
    if (args.size() >= 4 && args.at(1) == QLatin1StringView("client"))
        return runClient(app, QUrl(args.at(2)), args.at(3));
    std::fprintf(stderr, "usage: %s server [port] | client <url> <text>\n", argv[0]);
    return 2;
}
