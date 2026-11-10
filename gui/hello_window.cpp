// MIT License - see LICENSE
//
// A window that you paint on: a circle follows the mouse, a click changes its color and Escape quits.
// Run it with a display, or without one: `hello_window -platform offscreen`.
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRasterWindow>

//! [window]
class HelloWindow : public QRasterWindow {
public:
    HelloWindow() {
        setTitle("Hello, Nt GUI");
        resize(480, 320);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(0, 0, width(), height(), QColor("#f4f1ea"));

        painter.setPen(QColor("#d8d2c4"));
        for (int x = 0; x < width(); x += 40)
            painter.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 40)
            painter.drawLine(0, y, width(), y);

        painter.setPen(QPen(QBrush(QColor("#2b2b2b")), 3));
        painter.setBrush(m_colors[m_color]);
        painter.drawEllipse(m_mouse.x() - 40, m_mouse.y() - 40, 80, 80);

        QFont font("Sans Serif", 14);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#2b2b2b"));
        painter.drawText(QRect(0, 12, width(), 30), Qt::AlignHCenter, "Hello, Nt GUI");
        font.setBold(false);
        font.setPointSize(10);
        painter.setFont(font);
        painter.drawText(QRect(12, 0, width() - 24, height() - 10), Qt::AlignLeft | Qt::AlignBottom,
                         "Move the mouse, click to change the color, press Escape to quit.");
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        m_mouse = event->pos();
        update();
    }

    void mousePressEvent(QMouseEvent *event) override {
        m_mouse = event->pos();
        m_color = (m_color + 1) % 3;
        update();
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape)
            close();
    }

private:
    QPoint m_mouse{240, 160};
    int m_color = 0;
    const QColor m_colors[3] = {QColor("#e4572e"), QColor("#29a35a"), QColor("#3a7bd5")};
};
//! [window]

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    HelloWindow window;
    window.show();
    return app.exec();
}
