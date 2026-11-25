// MIT License - see LICENSE
//
// Fingers on a touch screen: a circle follows each one, and with two the label shows the distance between them and the
// scale that a pinch gesture would make. A widget that sets Qt::WA_AcceptTouchEvents and accepts the QTouchEvent
// events gets the whole sequence; the other widgets of an application get the first finger as the mouse.
#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <map>

class Canvas : public QWidget {
public:
    explicit Canvas(QLabel *label) : m_label(label) {
        setAttribute(Qt::WA_AcceptTouchEvents);
        setMinimumHeight(300);
    }

protected:
    bool event(QEvent *e) override {
        switch (e->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            touch(static_cast<QTouchEvent *>(e));
            e->accept();
            return true;
        default:
            return QWidget::event(e);
        }
    }

    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(30, 34, 44));
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor colors[] = {QColor(250, 210, 60), QColor(90, 180, 250), QColor(240, 110, 110), QColor(120, 220, 140),
                                 QColor(200, 130, 240)};
        int n = 0;
        for (const auto &[id, position] : m_fingers) {
            painter.setBrush(colors[n++ % 5]);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(position, 40, 40);
            painter.setPen(Qt::white);
            painter.drawText(QRectF(position.x() - 40, position.y() - 40, 80, 80), Qt::AlignCenter, QString::number(id));
        }
    }

private:
    void touch(QTouchEvent *e) {
        for (const QEventPoint &p : e->points()) {
            if (p.state() == QEventPoint::Released)
                m_fingers.erase(p.id());
            else
                m_fingers[p.id()] = p.position();
        }
        if (e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
            m_fingers.clear();
            m_start = 0;
        }
        QString text = QStringLiteral("Fingers: %1").arg(m_fingers.size());
        if (m_fingers.size() == 2) {
            auto it = m_fingers.begin();
            const QPointF a = it->second, b = (++it)->second;
            const double distance = std::hypot(a.x() - b.x(), a.y() - b.y());
            if (m_start == 0)
                m_start = distance;
            text += QStringLiteral("  distance %1  scale %2").arg(distance, 0, 'f', 0).arg(distance / m_start, 0, 'f', 2);
        } else {
            m_start = 0;
        }
        m_label->setText(text);
        update();
    }

    QLabel *m_label;
    std::map<int, QPointF> m_fingers;
    double m_start = 0;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *label = new QLabel(QStringLiteral("Fingers: 0"));
    layout->addWidget(label);
    layout->addWidget(new Canvas(label), 1);
    window.show();
    return app.exec();
}
