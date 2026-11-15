// MIT License - see LICENSE
//
// Vector drawing with QPainter: Bezier paths, gradients, pens with caps, joins and dashes, and
// transformations, antialiased.
//
//   painting                    shows the scenes in a window, animated, with the frame rate in the title
//   painting --gl               the same with OpenGL (QOpenGLWindow, 4x multisampling), not waiting for vsync
//   painting --gl-vsync         the same, synchronized with the display
//   painting --write <dir>      writes the pictures of the documentation to <dir>/painter/*.png
//   painting --bench [frames]   draws each scene at 800 by 600, in software into an image and with OpenGL into a
//                               multisampled framebuffer object, and prints the time per frame of each
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QOpenGLWindow>
#include <QPainter>
#include <QPainterPath>
#include <QRasterWindow>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

void flower(QPainter &painter, const QSizeF &size, double phase) {
    QPainterPath petal;
    petal.moveTo(0, 0);
    petal.cubicTo(30, -40, 90, -40, 120, 0);
    petal.cubicTo(90, 40, 30, 40, 0, 0);

    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(size.width() / 2, size.height() / 2);
    painter.scale(size.height() / 300, size.height() / 300);
    painter.rotate(phase);
    for (int i = 0; i < 12; ++i) {
        QRadialGradient shade(QPointF(60, 0), 60);
        shade.setColorAt(0, QColor::fromHsv(i * 30, 160, 255));
        shade.setColorAt(1, QColor::fromHsv(i * 30, 255, 170));
        painter.setPen(QPen(QColor(60, 30, 60), 1.5));
        painter.setBrush(shade);
        painter.drawPath(petal);
        painter.rotate(30);
    }
    painter.setBrush(QColor(250, 210, 60));
    painter.drawEllipse(QPointF(0, 0), 22, 22);
}

void gradients(QPainter &painter, const QSizeF &size, double phase) {
    const qreal w = size.width() / 3, h = size.height();
    QLinearGradient linear(0, 0, w, h);
    linear.setColorAt(0, QColor(40, 90, 200));
    linear.setColorAt(1, QColor(230, 240, 255));
    painter.fillRect(QRectF(0, 0, w, h), linear);

    QRadialGradient radial(QPointF(w * 1.5, h / 2), w / 2, QPointF(w * 1.35, h * 0.35));
    radial.setColorAt(0, Qt::white);
    radial.setColorAt(1, QColor(200, 40, 60));
    painter.fillRect(QRectF(w, 0, w, h), radial);

    QConicalGradient conical(QPointF(w * 2.5, h / 2), phase);
    for (int i = 0; i <= 6; ++i)
        conical.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 220, 240));
    painter.fillRect(QRectF(2 * w, 0, w, h), conical);
}

void pens(QPainter &painter, const QSizeF &size, double phase) {
    painter.setRenderHint(QPainter::Antialiasing);
    const Qt::PenCapStyle caps[3] = {Qt::FlatCap, Qt::SquareCap, Qt::RoundCap};
    const Qt::PenStyle styles[3] = {Qt::SolidLine, Qt::DashLine, Qt::DashDotLine};
    for (int i = 0; i < 3; ++i) {
        QPen pen(QColor(30, 30, 80), 14, styles[i], caps[i], Qt::RoundJoin);
        pen.setDashOffset(phase / 10);
        painter.setPen(pen);
        const qreal y = size.height() * (i + 1) / 5.0;
        painter.drawLine(QPointF(30, y), QPointF(size.width() / 2 - 20, y));
    }
    // Arcs of a gauge, with round caps.
    const qreal side = std::min(size.height() - 60, size.width() / 2 - 60);
    const QRectF dial(size.width() * 0.75 - side / 2, (size.height() - side) / 2, side, side);
    painter.setPen(QPen(QColor(220, 220, 230), 18, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(dial, -45 * 16, 270 * 16);
    painter.setPen(QPen(QColor(40, 170, 110), 18, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(dial, 225 * 16, -static_cast<int>(std::fmod(phase, 270.0) * 16));
}

void transforms(QPainter &painter, const QSizeF &size, double phase) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(size.width() / 2, size.height() / 2);
    for (int i = 0; i < 24; ++i) {
        painter.save();
        painter.rotate(i * 15 + phase);
        painter.translate(size.height() / 3, 0);
        painter.scale(1.0 + 0.5 * std::sin((i + phase / 10) * kPi / 6), 1.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromHsv(i * 15, 200, 230, 200));
        painter.drawRoundedRect(QRectF(-20, -8, 40, 16), 6, 6);
        painter.restore();
    }
}

void lines(QPainter &painter, const QSizeF &size, double phase) {
    // Many thin lines: the cost of strokes.
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(20, 60, 140, 160), 1.2));
    const QPointF c(size.width() / 2, size.height() / 2);
    for (int i = 0; i < 360; i += 2) {
        const double a = (i + phase) * kPi / 180;
        painter.drawLine(c, c + QPointF(std::cos(a), std::sin(a)) * (size.height() / 2.2));
    }
}

struct Scene {
    const char *name;
    std::function<void(QPainter &, const QSizeF &, double)> draw;
};

const std::vector<Scene> &scenes() {
    static const std::vector<Scene> list = {
        {"flower", flower}, {"gradients", gradients}, {"pens", pens}, {"transforms", transforms}, {"lines", lines}};
    return list;
}

QImage render(const Scene &scene, QSize size, double phase) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene.draw(painter, QSizeF(size), phase);
    return image;
}

// Draws every scene in a grid of cells; `phase` animates them.
void drawGrid(QPainter &painter, int width, int height, double phase) {
    painter.fillRect(QRect(0, 0, width, height), Qt::white);
    const auto &list = scenes();
    const int cols = 3, rows = 2;
    const QSizeF cell(width / static_cast<double>(cols), height / static_cast<double>(rows));
    for (std::size_t i = 0; i < list.size(); ++i) {
        painter.save();
        painter.translate(cell.width() * static_cast<double>(i % cols), cell.height() * static_cast<double>(i / cols));
        painter.setClipRect(QRect(0, 0, static_cast<int>(cell.width()), static_cast<int>(cell.height())));
        list[i].draw(painter, cell, phase);
        painter.restore();
    }
}

// Counts frames and shows the rate in the title of `window` once a second.
class FrameCounter {
public:
    FrameCounter() { m_second.start(); }
    void frame(QWindow *window, const char *what) {
        ++m_frames;
        if (m_second.elapsed() >= 1000) {
            window->setTitle(QStringLiteral("Nt painting (%1) - %2 frames per second")
                                 .arg(QString::fromLatin1(what))
                                 .arg(m_frames * 1000.0 / static_cast<double>(m_second.restart()), 0, 'f', 1));
            m_frames = 0;
        }
    }

private:
    QElapsedTimer m_second;
    int m_frames = 0;
};

class Window : public QRasterWindow {
public:
    Window() {
        resize(900, 600);
        connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
        m_timer.start(0);
        m_clock.start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        drawGrid(painter, width(), height(), static_cast<double>(m_clock.elapsed()) / 20.0);
        m_counter.frame(this, "raster");
    }

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
    FrameCounter m_counter;
};

// The same scenes with OpenGL: each swapped frame asks for the next.
class GLWindow : public QOpenGLWindow {
public:
    explicit GLWindow(bool vsync) {
        QSurfaceFormat format;
        format.setSamples(4);
        format.setSwapInterval(vsync ? 1 : 0);
        setFormat(format);
        resize(900, 600);
        connect(this, &QOpenGLWindow::frameSwapped, this, [this] { update(); });
        m_clock.start();
    }

protected:
    void paintGL() override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        drawGrid(painter, width(), height(), static_cast<double>(m_clock.elapsed()) / 20.0);
        painter.end();
        m_counter.frame(this, "OpenGL");
    }

private:
    QElapsedTimer m_clock;
    FrameCounter m_counter;
};

// Times `frames` frames of `scene` with OpenGL into a 4x multisampled framebuffer object, waiting for each to
// finish; returns milliseconds per frame, or a negative number without OpenGL.
double benchOpenGL(const Scene &scene, int frames) {
    static QOffscreenSurface *surface = nullptr;
    static QOpenGLContext *context = nullptr;
    if (!context) {
        surface = new QOffscreenSurface;
        surface->create();
        context = new QOpenGLContext;
        if (!surface->isValid() || !context->create() || !context->makeCurrent(surface))
            return -1.0;
    }
    QOpenGLFramebufferObjectFormat format;
    format.setSamples(4);
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QOpenGLFramebufferObject fbo(QSize(800, 600), format);
    QOpenGLPaintDevice device(fbo.size());
    fbo.bind();
    QElapsedTimer timer;
    timer.start();
    for (int f = 0; f < frames; ++f) {
        QPainter painter(&device);
        painter.fillRect(QRect(0, 0, 800, 600), Qt::white);
        scene.draw(painter, QSizeF(800, 600), f);
        painter.end();
        context->functions()->glFinish();
    }
    const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6 / frames;
    fbo.release();
    return ms;
}

} // namespace

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    if (argc >= 3 && std::strcmp(argv[1], "--write") == 0) {
        const QString dir = QString::fromLocal8Bit(argv[2]) + QStringLiteral("/painter");
        QDir().mkpath(dir);
        bool ok = true;
        for (const Scene &s : scenes())
            ok = render(s, QSize(360, 240), 30)
                     .save(dir + u'/' + QString::fromLatin1(s.name) + QStringLiteral(".png")) &&
                 ok;
        return ok ? 0 : 1;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0) {
        const int frames = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 100;
        std::printf("%-12s %12s %12s %12s %12s\n", "scene", "raster ms", "raster fps", "OpenGL ms", "OpenGL fps");
        for (const Scene &s : scenes()) {
            QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
            QElapsedTimer timer;
            timer.start();
            for (int f = 0; f < frames; ++f) {
                image.fill(Qt::white);
                QPainter painter(&image);
                s.draw(painter, QSizeF(800, 600), f);
            }
            const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6 / frames;
            const double gl = benchOpenGL(s, frames);
            if (gl < 0)
                std::printf("%-12s %12.3f %12.1f %12s %12s\n", s.name, ms, 1000.0 / ms, "-", "-");
            else
                std::printf("%-12s %12.3f %12.1f %12.3f %12.1f\n", s.name, ms, 1000.0 / ms, gl, 1000.0 / gl);
        }
        return 0;
    }
    if (argc >= 2 && (std::strcmp(argv[1], "--gl") == 0 || std::strcmp(argv[1], "--gl-vsync") == 0)) {
        GLWindow window(std::strcmp(argv[1], "--gl-vsync") == 0);
        window.show();
        return app.exec();
    }
    Window window;
    window.show();
    return app.exec();
}
