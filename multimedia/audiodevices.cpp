// MIT License - see LICENSE
//
// Lists the audio devices, then plays a one-second 440 Hz tone on the default output through a
// QAudioSink in pull mode, and prints when devices are plugged in or out.
//
//   audiodevices              list, play the tone, and quit
//   audiodevices --watch      keep running and report device changes

#include <QtCore>
#include <QtMultimedia>

#include <cmath>
#include <cstdio>

namespace {

void listDevices() {
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        std::printf("output %s%s: %s\n", device.isDefault() ? "(default) " : "", device.id().constData(),
                    qPrintable(device.description()));
    }
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        std::printf("input  %s%s: %s\n", device.isDefault() ? "(default) " : "", device.id().constData(),
                    qPrintable(device.description()));
    }
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool watch = app.arguments().contains(QStringLiteral("--watch"));
    listDevices();

    QMediaDevices devices;
    QObject::connect(&devices, &QMediaDevices::audioOutputsChanged, [] {
        std::printf("outputs changed\n");
        listDevices();
    });
    QObject::connect(&devices, &QMediaDevices::audioInputsChanged, [] {
        std::printf("inputs changed\n");
        listDevices();
    });

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    QByteArray samples(format.bytesForDuration(1000000), Qt::Uninitialized);
    auto *data = reinterpret_cast<qint16 *>(samples.data());
    for (int i = 0; i < format.sampleRate(); ++i)
        data[i] = qint16(8000 * std::sin(2 * M_PI * 440 * i / format.sampleRate()));

    QBuffer buffer(&samples);
    buffer.open(QIODevice::ReadOnly);
    QAudioSink sink(format);
    QObject::connect(&sink, &QAudioSink::stateChanged, [&](QtAudio::State state) {
        if (state == QtAudio::IdleState) { // played to the end
            sink.stop();
            if (!watch)
                QCoreApplication::quit();
        }
    });
    sink.start(&buffer);

    if (sink.state() == QtAudio::StoppedState) {
        std::printf("no output device\n");
        if (!watch)
            return 1;
    }
    return app.exec();
}
