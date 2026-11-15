// MIT License - see LICENSE
//
// Renders one picture of each kind of chart, for the reference documentation of Nt Charts:
//
//   render_charts <directory>      writes <directory>/charts/<name>.png
//
// The data is fixed, so the pictures only change when the drawing does.
#include <QApplication>
#include <QAreaSeries>
#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QHorizontalBarSeries>
#include <QImage>
#include <QLineSeries>
#include <QLogValueAxis>
#include <QPainter>
#include <QPercentBarSeries>
#include <QPieSeries>
#include <QScatterSeries>
#include <QSplineSeries>
#include <QStackedBarSeries>
#include <QValueAxis>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace {

std::string g_directory;

bool save(QChart *chart, const char *name, int width = 480, int height = 320) {
    chart->setGeometry(QRectF(0, 0, width, height));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        chart->paint(&painter);
    }
    const std::string path = g_directory + "/charts/" + name + ".png";
    const bool ok = image.save(QString::fromStdString(path));
    if (!ok)
        std::fprintf(stderr, "render_charts: cannot write %s\n", path.c_str());
    delete chart;
    return ok;
}

const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun"};

QStringList months() {
    QStringList list;
    for (const char *m : kMonths)
        list.append(QString::fromLatin1(m));
    return list;
}

bool lines() {
    auto *chart = new QChart;
    chart->setTitle("Requests per second");
    const char *names[] = {"api", "web", "batch"};
    for (int s = 0; s < 3; ++s) {
        auto *series = new QLineSeries;
        series->setName(names[s]);
        for (int i = 0; i <= 24; ++i)
            series->append(i, 40.0 + 25.0 * s + 18.0 * std::sin(i / 3.0 + s) + 6.0 * std::cos(i * 1.7));
        chart->addSeries(series);
    }
    chart->createDefaultAxes();
    auto *x = static_cast<QValueAxis *>(chart->axes(Qt::Horizontal).first());
    x->setRange(0, 24);
    x->setTickCount(7);
    x->setTitleText("Hour");
    auto *y = static_cast<QValueAxis *>(chart->axes(Qt::Vertical).first());
    y->applyNiceNumbers();
    return save(chart, "qlineseries");
}

bool splines() {
    auto *chart = new QChart;
    chart->setTitle("Temperature");
    auto *series = new QSplineSeries;
    series->setName("Sensor 1");
    const double values[] = {12, 14, 19, 23, 26, 24, 20, 17, 13};
    for (int i = 0; i < 9; ++i)
        series->append(i * 3, values[i]);
    series->setPointsVisible(true);
    series->setMarkerSize(7);
    chart->addSeries(series);
    chart->createDefaultAxes();
    chart->legend()->hide();
    static_cast<QValueAxis *>(chart->axes(Qt::Vertical).first())->setLabelFormat("%d °C");
    return save(chart, "qsplineseries");
}

bool scatter() {
    auto *chart = new QChart;
    chart->setTitle("Latency against load");
    auto *a = new QScatterSeries;
    a->setName("eu-west");
    auto *b = new QScatterSeries;
    b->setName("us-east");
    b->setMarkerShape(QScatterSeries::MarkerShapeRotatedRectangle);
    for (int i = 0; i < 40; ++i) {
        const double load = 5 + (i * 37 % 90);
        a->append(load, 20 + load * 0.6 + 12 * std::sin(i * 2.3));
        b->append(load + 3, 35 + load * 0.9 + 15 * std::cos(i * 1.9));
    }
    a->setMarkerSize(10);
    b->setMarkerSize(11);
    chart->addSeries(a);
    chart->addSeries(b);
    chart->createDefaultAxes();
    chart->axes(Qt::Horizontal).first()->setTitleText("Load (%)");
    chart->axes(Qt::Vertical).first()->setTitleText("ms");
    static_cast<QValueAxis *>(chart->axes(Qt::Horizontal).first())->applyNiceNumbers();
    static_cast<QValueAxis *>(chart->axes(Qt::Vertical).first())->applyNiceNumbers();
    return save(chart, "qscatterseries");
}

bool area() {
    auto *chart = new QChart;
    chart->setTitle("Memory");
    auto *upper = new QLineSeries;
    auto *lower = new QLineSeries;
    auto *used = new QLineSeries;
    for (int i = 0; i <= 30; ++i) {
        const double base = 2.0 + 0.8 * std::sin(i / 4.0);
        lower->append(i, 0);
        used->append(i, base);
        upper->append(i, base + 1.5 + 0.6 * std::cos(i / 3.0));
    }
    auto *cache = new QAreaSeries(upper, used);
    cache->setName("Cache");
    auto *heap = new QAreaSeries(used, lower);
    heap->setName("Heap");
    chart->addSeries(heap);
    chart->addSeries(cache);
    chart->createDefaultAxes();
    auto *y = static_cast<QValueAxis *>(chart->axes(Qt::Vertical).first());
    y->setRange(0, 6);
    y->setTickCount(7);
    y->setLabelFormat("%d GB");
    return save(chart, "qareaseries");
}

QBarSet *set(const char *label, std::initializer_list<qreal> values) {
    auto *s = new QBarSet(label);
    for (const qreal v : values)
        s->append(v);
    return s;
}

bool bars(QAbstractBarSeries *series, const char *title, const char *name, bool horizontal = false) {
    auto *chart = new QChart;
    chart->setTitle(title);
    series->append(set("North", {12, 15, 17, 14, 19, 22}));
    series->append(set("South", {9, 11, 10, 13, 15, 14}));
    series->append(set("West", {6, 8, 12, 11, 9, 13}));
    chart->addSeries(series);
    auto *categories = new QBarCategoryAxis;
    categories->append(months());
    auto *values = new QValueAxis;
    chart->addAxis(categories, horizontal ? Qt::AlignLeft : Qt::AlignBottom);
    chart->addAxis(values, horizontal ? Qt::AlignBottom : Qt::AlignLeft);
    series->attachAxis(categories);
    series->attachAxis(values);
    values->applyNiceNumbers();
    chart->legend()->setAlignment(Qt::AlignBottom);
    return save(chart, name);
}

bool pie() {
    auto *chart = new QChart;
    chart->setTitle("Disk usage");
    auto *series = new QPieSeries;
    series->append("System", 18);
    series->append("Applications", 34);
    series->append("Documents", 22);
    series->append("Media", 41);
    QPieSlice *free = series->append("Free", 25);
    free->setExploded(true);
    series->setLabelsVisible(true);
    chart->addSeries(series);
    chart->legend()->setAlignment(Qt::AlignRight);
    return save(chart, "qpieseries");
}

bool donut() {
    auto *chart = new QChart;
    chart->setTitle("Error budget");
    auto *series = new QPieSeries;
    series->setHoleSize(0.45);
    series->setPieSize(0.85);
    series->append("Used", 62);
    series->append("Remaining", 38);
    chart->addSeries(series);
    chart->legend()->setAlignment(Qt::AlignBottom);
    return save(chart, "donut", 320, 320);
}

bool dateTime() {
    auto *chart = new QChart;
    chart->setTitle("CPU load");
    auto *series = new QLineSeries;
    series->setName("node-1");
    const qint64 start = QDateTime(QDate(2026, 3, 2), QTime(8, 0)).toMSecsSinceEpoch();
    for (int i = 0; i <= 60; ++i)
        series->append(static_cast<qreal>(start + i * 60000LL), 45 + 30 * std::sin(i / 8.0) + 8 * std::sin(i * 1.3));
    chart->addSeries(series);
    auto *x = new QDateTimeAxis;
    x->setFormat("hh:mm");
    x->setTickCount(7);
    auto *y = new QValueAxis;
    y->setRange(0, 100);
    y->setLabelFormat("%d%%");
    chart->addAxis(x, Qt::AlignBottom);
    chart->addAxis(y, Qt::AlignLeft);
    series->attachAxis(x);
    series->attachAxis(y);
    chart->legend()->hide();
    return save(chart, "qdatetimeaxis");
}

bool logAxis() {
    auto *chart = new QChart;
    chart->setTitle("Response sizes");
    auto *series = new QLineSeries;
    series->setPointsVisible(true);
    series->setMarkerSize(6);
    for (int i = 0; i <= 10; ++i)
        series->append(i, std::pow(10.0, 1 + i * 0.45));
    chart->addSeries(series);
    auto *x = new QValueAxis;
    auto *y = new QLogValueAxis;
    y->setLabelFormat("%g");
    y->setMinorTickCount(8);
    y->setMinorGridLineVisible(true);
    chart->addAxis(x, Qt::AlignBottom);
    chart->addAxis(y, Qt::AlignLeft);
    series->attachAxis(x);
    series->attachAxis(y);
    chart->legend()->hide();
    return save(chart, "qlogvalueaxis");
}

bool theme(QChart::ChartTheme which, const char *name) {
    auto *chart = new QChart;
    chart->setTheme(which);
    chart->setTitle(name);
    auto *bars = new QBarSeries;
    bars->append(set("Plan", {5, 7, 6, 9}));
    bars->append(set("Actual", {4, 8, 7, 10}));
    chart->addSeries(bars);
    auto *trend = new QLineSeries;
    trend->setName("Trend");
    for (int i = 0; i < 4; ++i)
        trend->append(i, 4.5 + i * 1.6);
    chart->addSeries(trend);
    chart->createDefaultAxes();
    static_cast<QValueAxis *>(chart->axes(Qt::Vertical).first())->setRange(0, 12);
    chart->legend()->setAlignment(Qt::AlignBottom);
    std::string file = "theme-" + std::string(name);
    for (char &c : file)
        c = static_cast<char>(c == ' ' ? '-' : std::tolower(static_cast<unsigned char>(c)));
    return save(chart, file.c_str(), 360, 260);
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: render_charts <directory>\n");
        return 2;
    }
    ::setenv("QT_QPA_PLATFORM", "offscreen", 1);
    ::setenv("QT_SCALE_FACTOR", "1", 1);
    QApplication app(argc, argv);
    g_directory = argv[1];
    ::mkdir(g_directory.c_str(), 0755);
    ::mkdir((g_directory + "/charts").c_str(), 0755);
    bool ok = lines();
    ok = splines() && ok;
    ok = scatter() && ok;
    ok = area() && ok;
    ok = bars(new QBarSeries, "Sales by region", "qbarseries") && ok;
    ok = bars(new QStackedBarSeries, "Sales, stacked", "qstackedbarseries") && ok;
    ok = bars(new QPercentBarSeries, "Share of sales", "qpercentbarseries") && ok;
    ok = bars(new QHorizontalBarSeries, "Sales by region", "qhorizontalbarseries", true) && ok;
    ok = pie() && ok;
    ok = donut() && ok;
    ok = dateTime() && ok;
    ok = logAxis() && ok;
    const struct {
        QChart::ChartTheme theme;
        const char *name;
    } themes[] = {{QChart::ChartThemeLight, "Light"},
                  {QChart::ChartThemeDark, "Dark"},
                  {QChart::ChartThemeBlueCerulean, "Blue Cerulean"},
                  {QChart::ChartThemeBrownSand, "Brown Sand"},
                  {QChart::ChartThemeBlueNcs, "Blue NCS"},
                  {QChart::ChartThemeHighContrast, "High Contrast"},
                  {QChart::ChartThemeBlueIcy, "Blue Icy"},
                  {QChart::ChartThemeQt, "Qt"}};
    for (const auto &t : themes)
        ok = theme(t.theme, t.name) && ok;
    return ok ? 0 : 1;
}
