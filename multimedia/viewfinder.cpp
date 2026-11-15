// MIT License - see LICENSE
//
// Shows the default camera in a window, with a button that saves a picture in the current folder
// and a check box that plays the microphone back on the speakers.
//
//   viewfinder

#include <QtMultimedia>
#include <QtMultimediaWidgets>
#include <QtWidgets>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle(QStringLiteral("Viewfinder"));
    auto *layout = new QVBoxLayout(&window);

    auto *video = new QVideoWidget;
    QMediaCaptureSession session;
    QCamera camera;
    session.setCamera(&camera);
    session.setVideoOutput(video);
    camera.start();

    QImageCapture capture;
    session.setImageCapture(&capture);
    auto *shoot = new QPushButton(QStringLiteral("Take a picture"));
    QObject::connect(shoot, &QPushButton::clicked, &capture,
                     [&capture] { capture.captureToFile(QDir::currentPath()); });
    QObject::connect(&capture, &QImageCapture::readyForCaptureChanged, shoot, &QWidget::setEnabled);
    shoot->setEnabled(capture.isReadyForCapture());

    QAudioInput microphone;
    QAudioOutput speakers;
    auto *monitor = new QCheckBox(QStringLiteral("Hear the microphone"));
    QObject::connect(monitor, &QCheckBox::toggled, &session, [&](bool on) {
        session.setAudioInput(on ? &microphone : nullptr);
        session.setAudioOutput(on ? &speakers : nullptr);
    });

    auto *status = new QLabel;
    QObject::connect(&capture, &QImageCapture::imageSaved, status,
                     [status](int, const QString &file) { status->setText(QStringLiteral("Saved %1").arg(file)); });
    QObject::connect(&camera, &QCamera::errorOccurred, status,
                     [status](QCamera::Error, const QString &message) { status->setText(message); });
    if (!camera.isActive())
        status->setText(camera.errorString().isEmpty() ? QStringLiteral("No camera") : camera.errorString());

    layout->addWidget(video, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(shoot);
    row->addWidget(monitor);
    row->addWidget(status, 1);
    layout->addLayout(row);
    window.resize(800, 600);
    window.show();
    return app.exec();
}
