// MIT License - see LICENSE
//
// Records a few seconds from the default microphone to a WAV file.
//
//   recorder out.wav            record 5 seconds to out.wav
//   recorder out.wav 10         record 10 seconds

#include <QtCore>
#include <QtMultimedia>

#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        std::printf("usage: recorder out.wav [seconds]\n");
        return 2;
    }
    const int seconds = args.size() > 2 ? args.at(2).toInt() : 5;

    QMediaCaptureSession session;
    QAudioInput input;
    QMediaRecorder recorder;
    session.setAudioInput(&input);
    session.setRecorder(&recorder);
    recorder.setOutputLocation(QUrl::fromLocalFile(args.at(1)));
    QObject::connect(&recorder, &QMediaRecorder::durationChanged, [](qint64 ms) {
        std::printf("\r%lld ms", static_cast<long long>(ms));
        std::fflush(stdout);
    });
    recorder.record();
    if (recorder.recorderState() != QMediaRecorder::RecordingState) {
        std::printf("cannot record: %s\n", qPrintable(recorder.errorString()));
        return 1;
    }
    QTimer::singleShot(seconds * 1000, &app, [&] {
        recorder.stop();
        std::printf("\nwrote %s\n", qPrintable(recorder.actualLocation().toLocalFile()));
        QCoreApplication::quit();
    });
    return app.exec();
}
