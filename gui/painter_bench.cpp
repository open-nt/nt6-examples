// MIT License - see LICENSE
//
// A QPainter benchmark modeled on QCPainterBench. A live window draws the ruler, circles, bezier graphs, bars,
// icons & text (and optionally flower) scene over and over and shows the frame rate; Up/Down doubles or halves
// how many times the scene is rendered per frame, 1-6 toggle the tests, A toggles antialiasing and O switches
// between the raster and the OpenGL paint engine (4x MSAA, no vsync unless --vsync).
// With --batch it instead renders 1, 2, 4, 8, ... times per frame to an image and prints a table.
//
//   painter_bench [--tests ruler,circles,bezier,bars,icons,flower] [--size 1280x720] [--no-aa] [--opengl] [--vsync]
//                 [--count N] [--log] [--duration SECONDS]
//   painter_bench --batch [-platform offscreen] [--frames 60] [--max-count 64] [same options]
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWindow>
#include <QPainter>
#include <QRasterWindow>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QPen>
#include <QStringList>
#include <QSurfaceFormat>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace {

struct Test {
    const char *name;
    std::function<void(QPainter &, const QSize &, int)> draw; // painter, size, frame
    bool byDefault;
};

// A ruler that zooms in and out while it scrolls, so the tick spacing and the labels both change.
void ruler(QPainter &p, const QSize &s, int frame) {
    p.setPen(QColor("#333333"));
    const double unitPx = 6 * (1.5 + std::sin(frame * 0.02));
    const double offset = frame * 0.5;
    const int first = int(std::floor(offset / unitPx));
    const int last = int(std::ceil((offset + s.width()) / unitPx));
    for (int i = first; i <= last; ++i) {
        const int x = int(std::lround(i * unitPx - offset));
        const int len = i % 10 == 0 ? 30 : (i % 5 == 0 ? 20 : 10);
        p.drawLine(x, 0, x, len);
        p.drawLine(x, s.height(), x, s.height() - len);
    }
    p.setFont(QFont("Sans Serif", 8));
    for (int i = (first + 9) / 10 * 10; i <= last; i += 10) {
        if ((i / 10 + frame / 10) % 3 == 0)
            continue; // labels appear and disappear
        p.drawText(int(std::lround(i * unitPx - offset)) + 2, 44, QString::number(i));
    }
}

void circles(QPainter &p, const QSize &s, int frame) {
    const int n = 40;
    for (int i = 0; i < n; ++i) {
        const double a = (frame + i * 9) * 0.03;
        const double cx = s.width() * (0.1 + 0.8 * i / n);
        const double cy = s.height() * (0.5 + 0.3 * std::sin(a));
        const double r = 20 + 10 * std::sin(a * 1.7);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor::fromHsv((i * 9 + frame) % 360, 180, 230));
        p.drawEllipse(QPointF(cx, cy), r, r);

        QPen pen(QBrush(QColor("#222222")), 5);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(cx - r - 8, cy - r - 8, 2 * r + 16, 2 * r + 16), (frame * 16 * 3) % (360 * 16), 120 * 16);
    }
}

void bezier(QPainter &p, const QSize &s, int frame) {
    const int graphs = 3;
    const int points = 24;
    const double h = s.height() / double(graphs);
    for (int g = 0; g < graphs; ++g) {
        const double base = h * (g + 1) - 10;
        QPainterPath path;
        path.moveTo(0, base);
        double px = 0, py = base - h * 0.4;
        path.lineTo(px, py);
        for (int i = 1; i <= points; ++i) {
            const double x = s.width() * i / points;
            const double y = base - h * (0.2 + 0.5 * std::fabs(std::sin((i + g * 3) * 0.7 + frame * 0.02)));
            path.cubicTo((px + x) / 2, py, (px + x) / 2, y, x, y);
            px = x;
            py = y;
        }
        QPainterPath fill = path;
        fill.lineTo(s.width(), base);
        fill.closeSubpath();

        QLinearGradient grad(0, base - h, 0, base);
        grad.setColorAt(0, QColor(80 + g * 70, 140, 240, 200));
        grad.setColorAt(1, QColor(80 + g * 70, 140, 240, 20));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawPath(fill);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QBrush(QColor(40 + g * 70, 90, 200)), 2));
        p.drawPath(path);
    }
}

void bars(QPainter &p, const QSize &s, int frame) {
    const int n = 120;
    const double w = s.width() / double(n);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        const double v = 0.1 + 0.9 * std::fabs(std::sin(i * 0.13 + frame * 0.05));
        const double bh = v * s.height() * 0.8;
        p.setBrush(QColor::fromHsv(200 + int(v * 60), 200, 220));
        p.drawRect(QRectF(i * w + 1, s.height() - bh, w - 2, bh));
    }
}

void icons(QPainter &p, const QSize &s, int frame) {
    // Six different icons, drawn once into images, as an icon set would be loaded.
    static const std::vector<QImage> set = [] {
        const char *colors[] = {"#e07a1f", "#3a7bd5", "#29a35a", "#c0392b", "#8e44ad", "#16a0a8"};
        std::vector<QImage> images;
        for (int k = 0; k < 6; ++k) {
            QImage img(24, 24, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter ip(&img);
            ip.setRenderHint(QPainter::Antialiasing);
            ip.setBrush(QColor(colors[k]));
            ip.setPen(QPen(QBrush(QColor("#333333")), 2));
            QPainterPath path;
            switch (k) {
            case 0: // circle
                ip.drawEllipse(2, 2, 20, 20);
                break;
            case 1: // square
                ip.drawRect(3, 3, 18, 18);
                break;
            case 2: // triangle
                path.moveTo(12, 2);
                path.lineTo(22, 21);
                path.lineTo(2, 21);
                path.closeSubpath();
                ip.drawPath(path);
                break;
            case 3: // star
                for (int i = 0; i < 10; ++i) {
                    const double a = i * 3.14159265358979 / 5 - 3.14159265358979 / 2;
                    const double r = i % 2 ? 5 : 11;
                    if (i == 0)
                        path.moveTo(12 + r * std::cos(a), 12.5 + r * std::sin(a));
                    else
                        path.lineTo(12 + r * std::cos(a), 12.5 + r * std::sin(a));
                }
                path.closeSubpath();
                ip.drawPath(path);
                break;
            case 4: // plus
                path.moveTo(9, 2);
                path.lineTo(15, 2);
                path.lineTo(15, 9);
                path.lineTo(22, 9);
                path.lineTo(22, 15);
                path.lineTo(15, 15);
                path.lineTo(15, 22);
                path.lineTo(9, 22);
                path.lineTo(9, 15);
                path.lineTo(2, 15);
                path.lineTo(2, 9);
                path.lineTo(9, 9);
                path.closeSubpath();
                ip.drawPath(path);
                break;
            default: // diamond
                path.moveTo(12, 1);
                path.lineTo(23, 12);
                path.lineTo(12, 23);
                path.lineTo(1, 12);
                path.closeSubpath();
                ip.drawPath(path);
                break;
            }
            images.push_back(img);
        }
        return images;
    }();
    p.setFont(QFont("Sans Serif", 8));
    p.setPen(QColor("#222222"));
    const int cell = 56;
    for (int y = 0, r = 0; y + cell < s.height(); y += cell, ++r) {
        for (int x = 0, c = 0; x + cell < s.width(); x += cell, ++c) {
            p.drawImage(x + 16, y + 4, set[(r * 7 + c * 3) % set.size()]);
            p.drawText(x + 6, y + 46, QString("It%1").arg((r * 31 + c + frame / 30) % 100));
        }
    }
}

// One static path drawn with a changing transform and color.
void flower(QPainter &p, const QSize &s, int frame) {
    static const QPainterPath petal = [] {
        QPainterPath path;
        path.moveTo(0, 0);
        path.cubicTo(30, -30, 60, -30, 100, 0);
        path.cubicTo(60, 30, 30, 30, 0, 0);
        return path;
    }();
    const int flowers = 6;
    p.setPen(QPen(QBrush(QColor("#222222")), 1));
    for (int f = 0; f < flowers; ++f) {
        p.save();
        p.translate(s.width() * (f + 0.5) / flowers, s.height() / 2);
        p.scale(1.2, 1.2);
        p.rotate(frame * (1 + f * 0.3));
        for (int i = 0; i < 12; ++i) {
            p.rotate(30);
            p.setBrush(QColor::fromHsv((i * 30 + f * 40 + frame) % 360, 200, 230, 180));
            p.drawPath(petal);
        }
        p.restore();
    }
}

const std::vector<Test> &allTests() {
    static const std::vector<Test> tests = {
        {"ruler", ruler, true},   {"circles", circles, true}, {"bezier", bezier, true},
        {"bars", bars, true},     {"icons", icons, true},     {"flower", flower, false},
    };
    return tests;
}

// The state of the live scene, shared by the raster and the OpenGL window so that switching keeps the settings.
// Every enabled test is drawn `count` times per frame, as fast as the window can be repainted.
class Scene {
public:
    enum Action { None, Quit, SwitchBackend };

    Scene(const std::vector<bool> &enabled, bool aa) : m_enabled(enabled), m_aa(aa) { m_clock.start(); }

    void setStatus(const QString &status) { m_status = status; }
    void setLog(bool log) { m_log = log; }
    void setCount(int count) { m_count = count; }

    void paint(QPainter &p, const QSize &s, const QString &backend) {
        QElapsedTimer paintTimer;
        paintTimer.start();

        p.setRenderHint(QPainter::Antialiasing, m_aa);
        p.setRenderHint(QPainter::TextAntialiasing, m_aa);
        p.fillRect(0, 0, s.width(), s.height(), QColor("#f4f1ea"));
        const std::vector<Test> &tests = allTests();
        for (size_t t = 0; t < tests.size(); ++t) {
            if (!m_enabled[t])
                continue;
            for (int c = 0; c < m_count; ++c)
                tests[t].draw(p, s, m_frame);
        }
        m_paintNs += paintTimer.nsecsElapsed();
        ++m_frame;
        ++m_sampleFrames;

        if (m_clock.elapsed() >= 500) {
            m_fps = m_sampleFrames * 1000.0 / m_clock.elapsed();
            m_paintMs = m_paintNs / 1e6 / m_sampleFrames;
            if (m_log) {
                const QByteArray name = backend.toUtf8();
                std::printf("%s count %d aa %s %.1f fps %.2f ms/frame\n", name.constData(), m_count,
                            m_aa ? "on" : "off", m_fps, m_paintMs);
            }
            m_sampleFrames = 0;
            m_paintNs = 0;
            m_clock.restart();
        }
        drawHud(p, s, backend);
    }

    Action key(int key) {
        std::fprintf(stderr, "KEY\n");
        const int n = int(allTests().size());
        if (key >= Qt::Key_1 && key < Qt::Key_1 + n) {
            m_enabled[key - Qt::Key_1] = !m_enabled[key - Qt::Key_1];
        } else if (key == Qt::Key_Up || key == Qt::Key_Plus || key == Qt::Key_Equal) {
            // A frame that already takes this long would freeze the window for seconds once doubled.
            if (m_paintMs < 250)
                m_count = std::min(m_count * 2, 1024);
        } else if (key == Qt::Key_Down || key == Qt::Key_Minus) {
            m_count = std::max(m_count / 2, 1);
        } else if (key == Qt::Key_A) {
            m_aa = !m_aa;
        } else if (key == Qt::Key_O) {
            return SwitchBackend;
        } else if (key == Qt::Key_Escape || key == Qt::Key_Q) {
            return Quit;
        }
        return None;
    }

private:
    void drawHud(QPainter &p, const QSize &s, const QString &backend) {
        const std::vector<Test> &tests = allTests();
        const int lineHeight = 18;
        const int lines = int(tests.size()) + 5;
        const int x = 16, y = 64, w = 420, h = lines * lineHeight + 16;
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 20, 215));
        p.drawRect(x, y, w, h);

        p.setFont(QFont("Sans Serif", 10));
        p.setPen(QColor("#ffffff"));
        int ty = y + 20;
        p.drawText(x + 10, ty, QString("%1 fps   %2 ms/frame").arg(m_fps, 0, 'f', 1).arg(m_paintMs, 0, 'f', 2));
        ty += lineHeight;
        p.drawText(x + 10, ty, QString("render count %1   antialiasing %2   %3x%4")
                                   .arg(m_count)
                                   .arg(m_aa ? "on" : "off")
                                   .arg(s.width())
                                   .arg(s.height()));
        ty += lineHeight;
        p.drawText(x + 10, ty, QString("backend: %1").arg(backend));
        for (size_t t = 0; t < tests.size(); ++t) {
            ty += lineHeight;
            p.setPen(m_enabled[t] ? QColor("#8be28b") : QColor("#9a9a9a"));
            p.drawText(x + 10, ty, QString("[%1] %2 %3").arg(t + 1).arg(tests[t].name).arg(m_enabled[t] ? "on" : "off"));
        }
        ty += lineHeight;
        p.setPen(QColor("#ffffff"));
        p.drawText(x + 10, ty, "Up/Down: render count   A: antialiasing   O: OpenGL");
        ty += lineHeight;
        p.drawText(x + 10, ty, m_status.isEmpty() ? QString("1-6: toggle tests   Esc: quit") : m_status);
    }

    std::vector<bool> m_enabled;
    bool m_aa;
    bool m_log = false;
    int m_count = 1;
    int m_frame = 0;
    int m_sampleFrames = 0;
    qint64 m_paintNs = 0;
    double m_fps = 0;
    double m_paintMs = 0;
    QString m_status;
    QElapsedTimer m_clock;
};

class RasterBench : public QRasterWindow {
public:
    RasterBench(Scene &scene, const std::function<void()> &switchBackend)
        : m_scene(scene), m_switch(switchBackend) {
        setTitle("Nt painter benchmark (raster)");
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        m_scene.paint(p, size(), "raster");
        QTimer::singleShot(1, this, [this] { update(); });
    }

    void mousePressEvent(QMouseEvent *) override { std::fprintf(stderr, "MOUSE\n"); }
    void keyPressEvent(QKeyEvent *event) override {
        const Scene::Action action = m_scene.key(event->key());
        if (action == Scene::Quit)
            close();
        else if (action == Scene::SwitchBackend)
            m_switch();
    }

private:
    Scene &m_scene;
    const std::function<void()> &m_switch;
};

class GLBench : public QOpenGLWindow {
public:
    GLBench(Scene &scene, const std::function<void()> &switchBackend, const QSurfaceFormat &format)
        : m_scene(scene), m_switch(switchBackend) {
        setFormat(format);
        setTitle("Nt painter benchmark (OpenGL)");
        // The next frame starts after the swap, so the event loop sleeps between frames and Nt can read the
        // input events that EGL pulled into the X connection's queue.
        connect(this, &QOpenGLWindow::frameSwapped, this, [this] { QTimer::singleShot(1, this, [this] { update(); }); });
    }

protected:
    void initializeGL() override {
        const GLubyte *renderer = context()->functions()->glGetString(GL_RENDERER);
        m_backend = QString("OpenGL, %1").arg(renderer ? reinterpret_cast<const char *>(renderer) : "unknown");
    }

    void paintGL() override {
        QPainter p(this);
        m_scene.paint(p, size(), m_backend);
    }

    void mousePressEvent(QMouseEvent *) override { std::fprintf(stderr, "MOUSE\n"); }
    void keyPressEvent(QKeyEvent *event) override {
        const Scene::Action action = m_scene.key(event->key());
        if (action == Scene::Quit)
            close();
        else if (action == Scene::SwitchBackend)
            m_switch();
    }

private:
    Scene &m_scene;
    const std::function<void()> &m_switch;
    QString m_backend = "OpenGL";
};

} // namespace

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    QStringList only;
    QSize size(1280, 720);
    int frames = 60, maxCount = 64, count = 1, duration = 0;
    bool aa = true, batch = false, opengl = false, vsync = false, log = false;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!std::strcmp(a, "--tests") && i + 1 < argc)
            only = QString(argv[++i]).split(',');
        else if (!std::strcmp(a, "--size") && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
                size = QSize(w, h);
        } else if (!std::strcmp(a, "--frames") && i + 1 < argc)
            frames = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(a, "--max-count") && i + 1 < argc)
            maxCount = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(a, "--count") && i + 1 < argc)
            count = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(a, "--duration") && i + 1 < argc)
            duration = std::max(0, std::atoi(argv[++i]));
        else if (!std::strcmp(a, "--log"))
            log = true;
        else if (!std::strcmp(a, "--no-aa"))
            aa = false;
        else if (!std::strcmp(a, "--batch"))
            batch = true;
        else if (!std::strcmp(a, "--opengl"))
            opengl = true;
        else if (!std::strcmp(a, "--vsync"))
            vsync = true;
    }

    if (!batch) {
        std::vector<bool> enabled;
        for (const Test &test : allTests())
            enabled.push_back(only.isEmpty() ? test.byDefault : only.contains(test.name));
        Scene scene(enabled, aa);
        scene.setLog(log);
        scene.setCount(count);
        if (duration > 0)
            QTimer::singleShot(duration * 1000, &app, [&app] { app.quit(); });

        // Unlike the raster window, OpenGL swaps at the display's refresh rate unless the interval is 0.
        QSurfaceFormat format;
        format.setSamples(aa ? 4 : 0);
        format.setSwapInterval(vsync ? 1 : 0);

        std::function<void()> switchBackend;
        RasterBench raster(scene, switchBackend);
        GLBench gl(scene, switchBackend, format);
        raster.resize(size);
        gl.resize(size);

        // Only one window is shown at a time. Nothing here may run a nested event loop (processEvents or
        // QEventLoop): doing so makes Nt stop delivering window events, including keys, to an OpenGL window.
        QWindow *current = nullptr;
        bool pending = false;
        auto commit = [&](QWindow *next) {
            if (current && current != next)
                current->hide();
            scene.setStatus(QString());
            current = next;
        };
        auto activate = [&](QWindow *next) {
            if (pending)
                return;
            if (current)
                next->setGeometry(current->geometry());
            next->show();
            if (next != &gl) {
                commit(next);
                return;
            }
            // The OpenGL context is created once the window has been exposed, so poll for it.
            pending = true;
            auto waited = std::make_shared<QElapsedTimer>();
            waited->start();
            QTimer *poll = new QTimer(&app);
            QObject::connect(poll, &QTimer::timeout, [&, poll, waited] {
                if (gl.isValid()) {
                    commit(&gl);
                } else if (waited->elapsed() > 2000) {
                    gl.hide();
                    scene.setStatus("OpenGL context could not be created");
                    if (!current) {
                        raster.show();
                        current = &raster;
                    }
                } else {
                    return;
                }
                pending = false;
                poll->stop();
                poll->deleteLater();
            });
            poll->start(20);
        };
        switchBackend = [&] { activate(current == &raster ? static_cast<QWindow *>(&gl) : &raster); };

        activate(opengl ? static_cast<QWindow *>(&gl) : &raster);
        return app.exec();
    }

    QImage target(size, QImage::Format_ARGB32_Premultiplied);
    std::printf("QPainter raster, %dx%d, antialiasing %s, %d frames per measurement\n", size.width(), size.height(),
                aa ? "on" : "off", frames);
    std::printf("%-10s %8s %12s %10s\n", "test", "count", "ms/frame", "fps");

    for (const Test &test : allTests()) {
        if (only.isEmpty() ? !test.byDefault : !only.contains(test.name))
            continue;
        for (int count = 1; count <= maxCount; count *= 2) {
            QElapsedTimer timer;
            for (int f = -1; f < frames; ++f) {
                if (f == 0) // frame -1 warms up caches
                    timer.start();
                QPainter p(&target);
                p.setRenderHint(QPainter::Antialiasing, aa);
                p.setRenderHint(QPainter::TextAntialiasing, aa);
                p.fillRect(target.rect(), QColor("#f4f1ea"));
                for (int c = 0; c < count; ++c)
                    test.draw(p, size, std::max(f, 0));
            }
            const double ms = timer.nsecsElapsed() / 1e6 / frames;
            std::printf("%-10s %8d %12.3f %10.1f\n", test.name, count, ms, 1000.0 / ms);
            std::fflush(stdout);
            if (ms > 1000.0)
                break; // already unusably slow
        }
    }
    return 0;
}
