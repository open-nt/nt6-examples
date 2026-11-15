// MIT License - see LICENSE
//
// Lists the cameras and their formats, then streams from the default camera for a few seconds,
// printing the frame rate, and saves a picture.
//
//   camera                      list, stream for 3 seconds, save a picture in the current folder
//   camera --list               only list
//   camera --seconds 10         stream for 10 seconds

#include <QtCore>
#include <QtMultimedia>

#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    for (const QCameraDevice &device : QMediaDevices::videoInputs()) {
        std::printf("%s%s: %s\n", device.isDefault() ? "(default) " : "", device.id().constData(),
                    qPrintable(device.description()));
        for (const QCameraFormat &format : device.videoFormats())
            std::printf("    %s %dx%d %.0f-%.0f fps\n",
                        qPrintable(QVideoFrameFormat::pixelFormatToString(format.pixelFormat())),
                        format.resolution().width(), format.resolution().height(), double(format.minFrameRate()),
                        double(format.maxFrameRate()));
    }
    if (args.contains(QStringLiteral("--list")))
        return 0;
    const int seconds = args.indexOf(QStringLiteral("--seconds")) > 0
                            ? args.value(args.indexOf(QStringLiteral("--seconds")) + 1).toInt()
                            : 3;

    QMediaCaptureSession session;
    QCamera camera;
    QVideoSink sink;
    QImageCapture imageCapture;
    session.setCamera(&camera);
    session.setVideoSink(&sink);
    session.setImageCapture(&imageCapture);
    QObject::connect(&camera, &QCamera::errorOccurred, [](QCamera::Error, const QString &message) {
        std::printf("camera error: %s\n", qPrintable(message));
        QCoreApplication::exit(1);
    });
    camera.start();
    if (!camera.isActive()) {
        std::printf("no camera: %s\n", qPrintable(camera.errorString()));
        return 1;
    }

    int frames = 0;
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, [&frames](const QVideoFrame &frame) {
        if (frames++ == 0) {
            QVideoFrame mapped = frame;
            mapped.map(QVideoFrame::ReadOnly);
            std::printf("first frame: %dx%d %s, %d plane(s), %d bytes a line\n", frame.width(), frame.height(),
                        qPrintable(QVideoFrameFormat::pixelFormatToString(frame.pixelFormat())), mapped.planeCount(),
                        mapped.bytesPerLine(0));
            mapped.unmap();
        }
    });

    QObject::connect(&imageCapture, &QImageCapture::imageSaved,
                     [](int, const QString &fileName) { std::printf("saved %s\n", qPrintable(fileName)); });
    QTimer::singleShot(seconds * 1000, &imageCapture,
                       [&imageCapture] { imageCapture.captureToFile(QDir::currentPath()); });

    QTimer::singleShot(seconds * 1000 + 500, &app, [&] {
        std::printf("%d frames in %d s: %.1f fps\n", frames, seconds, double(frames) / seconds);
        camera.stop();
        QCoreApplication::quit();
    });
    return app.exec();
}
