// MIT License - see LICENSE
#pragma once

#include <QObject>

//! [class]
// A class with signals and slots derives from QObject and starts with Q_OBJECT. ntmoc reads the
// header and writes the code behind the signals; nt_enable_automoc() in CMake runs it.
class Thermostat : public QObject {
    Q_OBJECT
public:
    explicit Thermostat(QObject *parent = nullptr) : QObject(parent) {}

    double temperature() const { return m_temperature; }

public slots:
    // A slot is an ordinary member function. Putting it under "slots" also lets it be connected
    // by name with SLOT() and called with QMetaObject::invokeMethod().
    void setTemperature(double celsius) {
        if (celsius == m_temperature)
            return; // emit only on a real change, so connected objects cannot loop forever
        m_temperature = celsius;
        emit temperatureChanged(celsius);
        if (celsius > 30.0)
            emit overheated();
    }

signals:
    // Signals are declared, never defined: ntmoc writes their bodies.
    void temperatureChanged(double celsius);
    void overheated();

private:
    double m_temperature = 20.0;
};
//! [class]

//! [receiver]
class Display : public QObject {
    Q_OBJECT
public:
    QString text;

public slots:
    void show(double celsius) { text = QString::number(celsius) + " C"; }
};
//! [receiver]
