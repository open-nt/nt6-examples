#pragma once

#include <QObject>

#include <string>

namespace shapes {

class Shape : public QObject {
    Q_OBJECT
public:
    explicit Shape(QObject *parent = nullptr) : QObject(parent) {}

    int sides() const { return m_sides; }
    void setSides(int n) {
        if (n == m_sides)
            return;
        m_sides = n;
        emit sidesChanged(n);
        emit renamed(std::to_string(n) + "-gon");
    }

signals:
    // No bodies: ntmoc generates them.
    void sidesChanged(int sides);
    void renamed(const std::string &name);
    void reset();
    void resized(int width, int height = 0);
    void resized(double scale);

private:
    int m_sides = 3;
};

} // namespace shapes
