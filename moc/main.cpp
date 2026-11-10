#include "shape.h"

#include <QCoreApplication>
#include <QTimer>

#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    shapes::Shape shape;
    QObject::connect(&shape, &shapes::Shape::sidesChanged,
                     [](int n) { std::cout << "sides = " << n << "\n"; });
    QObject::connect(&shape, &shapes::Shape::renamed,
                     [](const std::string &name) { std::cout << "name  = " << name << "\n"; });
    QObject::connect(&shape, qOverload<double>(&shapes::Shape::resized),
                     [](double s) { std::cout << "scale = " << s << "\n"; });

    int step = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        shape.setSides(4 + step);
        emit shape.resized(1.5 * (step + 1));
        if (++step == 2)
            QCoreApplication::quit();
    });
    timer.start(50);

    return app.exec();
}
