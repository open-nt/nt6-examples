// MIT License - see LICENSE
//
// Draws every sprite of "Mochimaru no Daibouken" on one sheet, for review.
//
//   sprite_sheet [out.png] [--scale 6]
#include "sprites.h"

#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QtCore/qdebug.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {

// Draws pixel by pixel, so that the scaling is exact whatever the paint engine does.
void drawScaled(QPainter &p, const QImage &img, int x0, int y0, int scale) {
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = img.pixel(x, y);
            if (qAlpha(c))
                p.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, QColor::fromRgb(c));
        }
}

} // namespace

int main(int argc, char **argv) {
    ::setenv("QT_QPA_PLATFORM", "offscreen", 0);
    QGuiApplication app(argc, argv);

    QString out = "sprite_sheet.png";
    int scale = 6;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--scale" && i + 1 < argc)
            scale = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (arg != "-platform")
            out = arg;
        else
            ++i;
    }

    // Render everything first: a bad grid is reported before anything is drawn.
    std::map<std::string, QImage> images;
    bool ok = true;
    for (const Sprites::Def &d : Sprites::all()) {
        QString error;
        QImage img = Sprites::render(d, &error);
        if (img.isNull()) {
            std::fprintf(stderr, "sprite_sheet: %s\n", qPrintable(error));
            ok = false;
        }
        images[d.id] = img;
    }
    if (!ok)
        return 1;

    const int margin = 24, gap = 18, headerH = 34, labelH = 26, sheetW = 1180;
    QImage sheet(sheetW, 3200, QImage::Format_ARGB32);
    sheet.fill(QColor("#fff3f8"));
    QPainter p(&sheet);
    QFont titleFont("Sans Serif", 20);
    titleFont.setBold(true);
    QFont headFont("Sans Serif", 13);
    headFont.setBold(true);
    QFont labelFont("Sans Serif", 10);

    p.setFont(titleFont);
    p.setPen(QColor("#b0304a"));
    p.drawText(QRect(margin, 10, sheetW - 2 * margin, 40), Qt::AlignLeft | Qt::AlignVCenter,
               "Mochimaru no Daibouken  -  sprite review");
    int y = 64;

    std::vector<std::string> groups;
    for (const Sprites::Def &d : Sprites::all())
        if (std::find(groups.begin(), groups.end(), d.group) == groups.end())
            groups.push_back(d.group);

    for (const std::string &group : groups) {
        p.setFont(headFont);
        p.setPen(QColor("#4a3040"));
        p.drawText(QRect(margin, y, sheetW - 2 * margin, headerH), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromStdString(group));
        y += headerH;
        int x = margin;
        int rowH = 0;
        for (const Sprites::Def &d : Sprites::all()) {
            if (d.group != group)
                continue;
            const QImage &img = images[d.id];
            const int w = img.width() * scale, h = img.height() * scale;
            const int cellW = std::max(w, 90);
            if (x + cellW > sheetW - margin) {
                x = margin;
                y += rowH + gap;
                rowH = 0;
            }
            p.fillRect(x, y, cellW, h, QColor("#c8ebf9"));
            drawScaled(p, img, x + (cellW - w) / 2, y, scale);
            p.setFont(labelFont);
            p.setPen(QColor("#6a5060"));
            p.drawText(QRect(x, y + h + 2, cellW, labelH - 4), Qt::AlignHCenter | Qt::AlignTop,
                       QString::fromStdString(d.label));
            x += cellW + gap;
            rowH = std::max(rowH, h + labelH);
        }
        y += rowH + gap + 6;
    }
    p.end();

    if (!sheet.copy(0, 0, sheetW, y).save(out)) {
        std::fprintf(stderr, "sprite_sheet: cannot write %s\n", qPrintable(out));
        return 1;
    }
    std::printf("%s: %d sprites\n", qPrintable(out), int(Sprites::all().size()));
    return 0;
}
