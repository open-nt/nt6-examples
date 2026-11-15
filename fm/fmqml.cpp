// MIT License - see LICENSE
//
// fmqml: sound effects, a jingle and chip music from a QML document.
//
//   fmqml                 runs the document below for eight seconds
//   fmqml game.qml        runs a document of your own
//
// The document plays a coin every second with a little random pitch, a looping engine hum whose
// pitch follows a "speed" property, a jingle from a bank, and a laser on the emulated OPL2.
#include <QtCore>
#include <QtFMQml>
#include <QtQml>

namespace {

// The bank that the document's FmSoundBank loads, written next to it.
QByteArray bankJson() {
    QtFM::PatchBank bank;
    bank.setName(QStringLiteral("demo"));
    bank.insert(QStringLiteral("coin"), QtFM::Patch::generate(QtFM::Category::Pickup, 3));
    bank.insert(QStringLiteral("blip"), QtFM::Patch::generate(QtFM::Category::Blip, 8));
    return bank.toJson();
}

const char *document = R"QML(
import QtQml
import QtFM

QtObject {
    id: game
    property real speed: 0

    property FmSound coin: FmSound {
        patch: FmPatch.generate(FmPatch.Pickup, 3)
        pitchVariance: 1            // +/- semitones per play
        maxInstances: 4
    }
    property FmSound engineHum: FmSound {
        patch: FmPatch.generate(FmPatch.Engine, 42)
        loop: true
        pitch: game.speed / 10      // bound live, smoothed on the audio thread
    }
    property FmSound laser: FmSound {
        patch: FmPatch.generate(FmPatch.Laser, 4, FmChip.OPL2)
        chip: FmChip.OPL2           // played on the emulated AdLib
    }

    property FmSoundBank bank: FmSoundBank { source: "sfx.json" }
    property FmSound blip: FmSound { patch: game.bank.sound("blip") }

    property FmSequence jingle: FmSequence {
        bank: game.bank
        tempo: 160
        notes: [
            { beat: 0, sound: "coin" },
            { beat: 0.5, sound: "coin", pitch: 4 },
            { beat: 1, sound: "coin", pitch: 7 },
            { beat: 1.5, sound: "coin", pitch: 12, gain: -3 }
        ]
    }

    property FmMusic music: FmMusic {
        source: Qt.application.arguments.length > 1 ? Qt.application.arguments[1] : ""
        loops: false
    }

    property Timer clock: Timer {
        interval: 1000; repeat: true; running: true
        property int tick: 0
        onTriggered: {
            ++tick
            game.coin.play()
            if (tick === 2) game.jingle.play()
            if (tick === 3) game.laser.play()
            if (tick === 4) game.blip.play()
            if (tick === 8) Qt.quit()
        }
    }
    property Timer rev: Timer {
        interval: 50; repeat: true; running: true
        onTriggered: game.speed = (game.speed + 2) % 140
    }

    Component.onCompleted: {
        engineHum.play()
        if (music.source != "")
            music.play()
    }
}
)QML";

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    QtFM::registerQmlTypes(); // "import QtFM" in documents

    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit);
    QQmlComponent component(&engine);
    QTemporaryDir dir;
    QFile bank(dir.filePath(QStringLiteral("sfx.json")));
    if (!dir.isValid() || !bank.open(QIODevice::WriteOnly) || bank.write(bankJson()) < 0)
        return 1;
    bank.close();
    if (argc > 1 && !QString::fromLocal8Bit(argv[1]).endsWith(QLatin1String(".qml"))) {
        // A music file for the built-in document.
        component.setData(document, QUrl::fromLocalFile(dir.filePath(QStringLiteral("game.qml"))));
    } else if (argc > 1) {
        component.loadUrl(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    } else {
        component.setData(document, QUrl::fromLocalFile(dir.filePath(QStringLiteral("game.qml"))));
    }
    QObject *game = component.create();
    if (!game) {
        qWarning().noquote() << component.errorString();
        return 1;
    }
    if (!QtFM::FmEngine::instance()->isStarted())
        qWarning("fmqml: no sound output");
    const int result = app.exec();
    delete game;
    return result;
}
