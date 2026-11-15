// MIT License - see LICENSE
//
// Decodes a WAV file to mono 32-bit floating point at 16 kHz, as a speech recogniser wants it, and
// prints its peak level.
//
//   decoder music.wav

#include <QtCore>
#include <QtMultimedia>

#include <cmath>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        std::printf("usage: decoder file.wav\n");
        return 2;
    }

    QAudioDecoder decoder;
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    decoder.setAudioFormat(format);
    decoder.setSource(QUrl::fromLocalFile(app.arguments().at(1)));
    float peak = 0.0f;
    qint64 frames = 0;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&] {
        const QAudioBuffer buffer = decoder.read();
        const auto *samples = buffer.constData<QAudioBuffer::F32M>();
        for (qsizetype i = 0; i < buffer.frameCount(); ++i)
            peak = std::max(peak, std::fabs(samples[i].channels[0]));
        frames += buffer.frameCount();
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, [&] {
        std::printf("%lld frames, %lld ms, peak %.3f\n", static_cast<long long>(frames),
                    static_cast<long long>(decoder.duration()), double(peak));
        QCoreApplication::quit();
    });
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), [&](QAudioDecoder::Error) {
        std::printf("cannot decode: %s\n", qPrintable(decoder.errorString()));
        QCoreApplication::exit(1);
    });
    decoder.start();
    return app.exec();
}
