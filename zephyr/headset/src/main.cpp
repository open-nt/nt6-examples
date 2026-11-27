// MIT License - see LICENSE
// Nt on Zephyr: a headset on the audio codec. The microphone is set up for the impedance in prj.conf (or
// CONFIG_APP_MIC_IMPEDANCE), its level is printed every second with the plugging and the button of the headset, and
// what it picks up is played back on the headphones, so that the bias and gain can be judged by ear.
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtMultimedia/QAudioHeadset>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QMediaDevices>

#include <cmath>
#include <cstring>

int main() {
    int argc = 1;
    char name[] = "headset";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);

    const QAudioDevice input = QMediaDevices::defaultAudioInput();
    const QAudioDevice output = QMediaDevices::defaultAudioOutput();
    qInfo() << "input:" << input.description() << " output:" << output.description();
    if (input.isNull() || output.isNull()) {
        qWarning() << "no audio codec in the devicetree";
        return 1;
    }

    QAudioHeadset headset;
    if (!headset.isAvailable()) {
        qWarning() << "this codec has no headset control";
    } else {
        headset.setMicrophoneImpedance(CONFIG_APP_MIC_IMPEDANCE);
        if (CONFIG_APP_MIC_GAIN > -100)
            headset.setMicrophoneGain(float(CONFIG_APP_MIC_GAIN));
        qInfo() << headset.codecName() << "microphone:" << headset.microphoneImpedance() << "ohm, bias"
                << headset.microphoneBias() << "gain" << headset.microphoneGain() << "dB on input"
                << headset.microphoneInput() << headset.microphoneChannel();
        QObject::connect(&headset, &QAudioHeadset::microphoneConnectedChanged,
                         [](bool on) { qInfo() << (on ? "microphone plugged in" : "microphone unplugged"); });
        QObject::connect(&headset, &QAudioHeadset::buttonPressedChanged,
                         [](bool on) { qInfo() << (on ? "button pressed" : "button released"); });
        QObject::connect(&headset, &QAudioHeadset::errorOccurred, [&headset] { qWarning() << headset.errorString(); });
    }

    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioSource source(input, format);
    QAudioSink sink(output, format);
    QIODevice *mic = source.start();
    QIODevice *headphones = CONFIG_APP_SIDETONE ? sink.start() : nullptr;
    if (!mic) {
        qWarning() << "cannot record:" << source.error();
        return 1;
    }

    double sumSquares = 0;
    qint64 samples = 0;
    QObject::connect(mic, &QIODevice::readyRead, [&] {
        const QByteArray data = mic->readAll();
        const auto count = data.size() / qsizetype(sizeof(qint16));
        for (qsizetype i = 0; i < count; ++i) {
            qint16 sample;
            std::memcpy(&sample, data.constData() + i * qsizetype(sizeof(qint16)), sizeof(sample));
            sumSquares += double(sample) * double(sample);
        }
        samples += count;
        if (headphones)
            headphones->write(data);
    });

    QTimer meter;
    QObject::connect(&meter, &QTimer::timeout, [&] {
        const double rms = samples ? std::sqrt(sumSquares / double(samples)) : 0.0;
        const double dbfs = rms > 0 ? 20.0 * std::log10(rms / 32768.0) : -120.0;
        qInfo().nospace() << "level " << QString::number(dbfs, 'f', 1) << " dBFS over " << samples << " samples"
                          << (headset.isMicrophoneConnected() ? ", microphone sensed" : "");
        sumSquares = 0;
        samples = 0;
    });
    meter.start(1000);

    return app.exec();
}
