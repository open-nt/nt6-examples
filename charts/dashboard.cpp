// MIT License - see LICENSE
//
// A dashboard of four charts: a live line chart, bars, a donut and a scatter plot. Clicking a bar or a slice
// prints it; dragging over the line chart zooms, and a right click zooms out.
//
//   chart_dashboard                  shows the dashboard
//   chart_dashboard --write out.png  draws the line chart to an image instead
#include <QApplication>
#include <QAreaSeries>
#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QGridLayout>
#include <QImage>
#include <QLineSeries>
#include <QPainter>
#include <QPieSeries>
#include <QScatterSeries>
#include <QTimer>
#include <QValueAxis>
#include <QWidget>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

double sample(int t) { return 50.0 + 25.0 * std::sin(t / 9.0) + 10.0 * std::sin(t / 2.3); }

QChart *lineChart(QLineSeries **live) {
    auto *series = new QLineSeries;
    series->setName("CPU");
    for (int t = 0; t < 60; ++t)
        series->append(t, sample(t));

    auto *chart = new QChart;
    chart->setTitle("Load");
    chart->addSeries(series);
    chart->createDefaultAxes();
    chart->axes(Qt::Vertical).first()->setRange(0, 100);
    *live = series;
    return chart;
}

QChart *barChart() {
    auto *done = new QBarSet("Done");
    *done << 12 << 18 << 9 << 15;
    auto *open = new QBarSet("Open");
    *open << 4 << 6 << 3 << 8;

    auto *series = new QBarSeries;
    series->append(done);
    series->append(open);
    series->setLabelsVisible(true);
    QObject::connect(series, &QBarSeries::clicked, [](int index, QBarSet *set) {
        std::printf("%s: %g\n", set->label().toUtf8().constData(), set->at(index));
    });

    auto *axisX = new QBarCategoryAxis;
    axisX->append({"Core", "GUI", "Network", "Charts"});
    auto *axisY = new QValueAxis;
    axisY->setRange(0, 20);

    auto *chart = new QChart;
    chart->setTitle("Tickets");
    chart->addSeries(series);
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    return chart;
}

QChart *pieChart() {
    auto *series = new QPieSeries;
    series->setHoleSize(0.4);
    series->append("Passed", 182);
    series->append("Skipped", 11);
    QPieSlice *failed = series->append("Failed", 7);
    failed->setExploded(true);
    failed->setLabelVisible(true);
    QObject::connect(series, &QPieSeries::hovered, [](QPieSlice *slice, bool state) { slice->setExploded(state); });

    auto *chart = new QChart;
    chart->setTitle("Tests");
    chart->addSeries(series);
    chart->legend()->setAlignment(Qt::AlignRight);
    return chart;
}

QChart *scatterChart() {
    auto *series = new QScatterSeries;
    series->setName("Requests");
    series->setMarkerSize(9);
    for (int i = 0; i < 50; ++i)
        series->append(i * 37 % 100, 20 + (i * 37 % 100) * 0.7 + 10 * std::sin(i));

    auto *chart = new QChart;
    chart->setTitle("Latency");
    chart->addSeries(series);
    chart->createDefaultAxes();
    return chart;
}

QChart *areaChart() {
    auto *upper = new QLineSeries;
    for (int t = 0; t < 24; ++t)
        upper->append(t, 2.0 + std::sin(t / 4.0));

    auto *series = new QAreaSeries(upper);
    series->setName("Memory");

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->createDefaultAxes();
    return chart;
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QLineSeries *live = nullptr;

    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        QChart *chart = lineChart(&live);
        chart->resize(QSizeF(640, 400));
        QImage image(640, 400, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        chart->paint(&painter);
        painter.end();
        image.save(argv[2]);
        delete chart;
        delete areaChart();
        return 0;
    }

    QWidget window;
    window.setWindowTitle("Nt Charts dashboard");
    auto *grid = new QGridLayout(&window);

    auto *view = new QChartView(lineChart(&live));
    view->setRubberBand(QChartView::HorizontalRubberBand);
    grid->addWidget(view, 0, 0);
    grid->addWidget(new QChartView(barChart()), 0, 1);
    grid->addWidget(new QChartView(pieChart()), 1, 0);
    grid->addWidget(new QChartView(scatterChart()), 1, 1);

    // New samples arrive every 250 ms; the window shows the last 60.
    int t = 60;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QList<QPointF> points = live->points();
        points.removeFirst();
        points.append(QPointF(t, sample(t)));
        ++t;
        live->replace(points);
        if (!live->chart()->isZoomed())
            live->chart()->axes(Qt::Horizontal).first()->setRange(t - 60, t - 1);
    });
    timer.start(250);

    window.resize(960, 640);
    window.show();
    return app.exec();
}
