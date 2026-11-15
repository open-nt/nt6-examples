// MIT License - see LICENSE
//
// A QOpenGLWindow that clears with OpenGL and paints with QPainter on top, animated by frameSwapped(). It prints
// the frame rate each second: with --no-vsync the swap interval is 0, so it renders as fast as it can instead of at
// the refresh rate of the display.
//
//   glwindow [--no-vsync] [--frames N]
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWindow>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class Window : public QOpenGLWindow, protected QOpenGLFunctions {
public:
    Window() {
        connect(this, &QOpenGLWindow::frameSwapped, this, [this] {
            ++m_frames;
            update(); // the next frame
        });
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        m_clock.start();
    }

    void paintGL() override {
        const float t = static_cast<float>(m_clock.elapsed()) / 1000.0f;
        glClearColor(0.1f, 0.12f, 0.2f + 0.1f * std::sin(t), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.rotate(static_cast<double>(t) * 45.0);
        painter.setPen(QPen(QColor(250, 210, 60), 6, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(QColor(60, 140, 220, 160));
        painter.drawRoundedRect(QRectF(-80, -50, 160, 100), 16, 16);
    }

public:
    int frames() const { return m_frames; }

private:
    QElapsedTimer m_clock;
    int m_frames = 0;
};

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    bool vsync = true;
    int stopAfter = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-vsync") == 0)
            vsync = false;
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            stopAfter = std::atoi(argv[++i]);
    }
    QSurfaceFormat format;
    format.setSamples(4);
    format.setSwapInterval(vsync ? 1 : 0);

    Window window;
    window.setFormat(format);
    window.setTitle(vsync ? "Nt OpenGL window" : "Nt OpenGL window (no vsync)");
    window.resize(640, 400);
    window.show();

    QElapsedTimer second;
    second.start();
    int last = 0;
    QObject::connect(&window, &QOpenGLWindow::frameSwapped, &window, [&] {
        if (stopAfter > 0 && window.frames() >= stopAfter)
            app.quit();
        if (second.elapsed() >= 1000) {
            std::printf("%.1f frames per second\n",
                        (window.frames() - last) * 1000.0 / static_cast<double>(second.restart()));
            std::fflush(stdout);
            last = window.frames();
        }
    });
    return app.exec();
}
