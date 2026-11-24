// MIT License - see LICENSE
//
// A text chat on the local network, written with Nt Comms. One instance hosts a session, the others find it
// with multicast discovery and join; every line you type goes to everybody in the session.
//
//   comms_chat --name alice --host Lobby
//   comms_chat --name bob   --join Lobby
//
// It needs only Nt Core, Network and Comms, so it also runs on an Android device (adb shell), where it is built
// with the NDK. Commands: /who lists the peers, /quit leaves.
#include <QtComms/qcommschat.h>
#include <QtComms/qcommssession.h>
#include <QtComms/qcommssessionbrowser.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QTimer>

#include <cstdio>
#include <string>
#include <unistd.h>

using namespace QtComms;

static void say(const QString &line) {
    std::printf("%s\n", line.toUtf8().constData());
    std::fflush(stdout);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("comms_chat"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Chat on the local network with Nt Comms"));
    parser.addHelpOption();
    QCommandLineOption nameOption({QStringLiteral("n"), QStringLiteral("name")}, QStringLiteral("Your name."),
                                  QStringLiteral("name"), QStringLiteral("guest"));
    QCommandLineOption hostOption({QStringLiteral("H"), QStringLiteral("host")},
                                  QStringLiteral("Host a session called <session>."), QStringLiteral("session"));
    QCommandLineOption joinOption({QStringLiteral("j"), QStringLiteral("join")},
                                  QStringLiteral("Join the session called <session>."), QStringLiteral("session"));
    QCommandLineOption timeoutOption(QStringLiteral("timeout"), QStringLiteral("Seconds to look for a session."),
                                     QStringLiteral("seconds"), QStringLiteral("10"));
    parser.addOption(nameOption);
    parser.addOption(hostOption);
    parser.addOption(joinOption);
    parser.addOption(timeoutOption);
    parser.process(app);
    if (parser.isSet(hostOption) == parser.isSet(joinOption)) {
        std::fprintf(stderr, "Give either --host or --join.\n");
        return 2;
    }
    const QString name = parser.value(nameOption);

    Session session;
    Chat chat(&session);
    chat.setCallsign(name);
    SessionBrowser browser;

    QObject::connect(&session, &Session::peerJoined, [](const Peer &p) { say(QStringLiteral("* %1 joined").arg(p.name)); });
    QObject::connect(&session, &Session::peerLeft, [&session](quint16 number) {
        say(QStringLiteral("* peer %1 left").arg(number));
        Q_UNUSED(session)
    });
    QObject::connect(&session, &Session::rejected, [&app](Session::RejectReason, const QString &text, const QCborMap &) {
        std::fprintf(stderr, "Could not join: %s\n", text.toUtf8().constData());
        app.exit(1);
    });
    QObject::connect(&session, &Session::stateChanged, [](Session::State state) {
        if (state == Session::State::Joined || state == Session::State::Hosting)
            say(QStringLiteral("* connected; type a message, /who or /quit"));
    });
    QObject::connect(&chat, &Chat::messageReceived, [&chat](const ChatMessage &m) {
        if (m.senderUid == chat.uid())
            return;
        say(QStringLiteral("<%1> %2").arg(m.system ? QStringLiteral("system") : m.senderCallsign, m.text));
    });

    if (parser.isSet(hostOption)) {
        if (!session.host(parser.value(hostOption), name)) {
            std::fprintf(stderr, "Could not host: %s\n", session.errorString().toUtf8().constData());
            return 1;
        }
    } else {
        const QString wanted = parser.value(joinOption);
        auto tryJoin = [&](const SessionInfo &) {
            const SessionInfo info = browser.find(wanted);
            if (info.isValid() && session.state() == Session::State::Idle) {
                browser.stop();
                session.join(info, name);
            }
        };
        QObject::connect(&browser, &SessionBrowser::sessionFound, tryJoin);
        QObject::connect(&browser, &SessionBrowser::sessionUpdated, tryJoin);
        if (!browser.start()) {
            std::fprintf(stderr, "Could not browse: %s\n", browser.errorString().toUtf8().constData());
            return 1;
        }
        say(QStringLiteral("* looking for \"%1\"...").arg(wanted));
        QTimer::singleShot(parser.value(timeoutOption).toInt() * 1000, &app, [&] {
            if (session.state() == Session::State::Idle) {
                std::fprintf(stderr, "No session called \"%s\" was found.\n", wanted.toUtf8().constData());
                app.exit(1);
            }
        });
    }

    // Standard input: one message or command per line.
    std::string pending;
    QSocketNotifier input(0, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, [&](QSocketDescriptor, QSocketNotifier::Type) {
        char buffer[512];
        const ssize_t n = ::read(0, buffer, sizeof buffer);
        if (n <= 0) {
            input.setEnabled(false);
            session.leave();
            app.quit();
            return;
        }
        pending.append(buffer, static_cast<std::size_t>(n));
        for (std::size_t eol; (eol = pending.find('\n')) != std::string::npos;) {
            const QString line = QString::fromUtf8(pending.substr(0, eol).c_str()).trimmed();
            pending.erase(0, eol + 1);
            if (line.isEmpty())
                continue;
            if (line == QStringLiteral("/quit")) {
                session.leave();
                app.quit();
            } else if (line == QStringLiteral("/who")) {
                for (const Peer &p : session.peers())
                    say(QStringLiteral("  %1%2").arg(p.name, p.number == session.localPeer() ? QStringLiteral(" (you)") : QString()));
            } else if (session.state() != Session::State::Joined && session.state() != Session::State::Hosting) {
                say(QStringLiteral("* not connected yet"));
            } else {
                chat.send(line);
            }
        }
    });

    return app.exec();
}
