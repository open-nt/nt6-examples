// MIT License - see LICENSE
//
// A QOpenGLWidget in a widget window: OpenGL ES draws the background, QPainter the spinning square on top, and a
// button and a label are ordinary widgets above it.
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

class GlWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    GlWidget() {
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
        timer->start(16);
        m_clock.start();
    }

protected:
    void initializeGL() override { initializeOpenGLFunctions(); }
    void paintGL() override {
        const float t = static_cast<float>(m_clock.elapsed()) / 1000.0f;
        glClearColor(0.2f, 0.15f + 0.1f * std::sin(t), 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.rotate(static_cast<double>(t) * 60.0);
        painter.setBrush(QColor(250, 210, 60));
        painter.drawRect(QRectF(-60, -60, 120, 120));
    }

private:
    QElapsedTimer m_clock;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *label = new QLabel(QStringLiteral("QOpenGLWidget on Android"));
    layout->addWidget(label);
    layout->addWidget(new GlWidget, 1);
    auto *button = new QPushButton(QStringLiteral("Quit"));
    QObject::connect(button, &QPushButton::clicked, &app, &QApplication::quit);
    layout->addWidget(button);
    window.show();
    return app.exec();
}
