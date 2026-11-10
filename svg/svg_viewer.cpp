// MIT License - see LICENSE
//
// Shows an SVG file in a window, or draws the picture below into an SVG file:
//
//   svg_viewer picture.svg          shows the file
//   svg_viewer --write out.svg      paints with QPainter on a QSvgGenerator
//   svg_viewer                      shows the picture that --write paints, from memory
#include <QtCore/qbuffer.h>
#include <QtGui/qimage.h>
#include <QtGui/qpainter.h>
#include <QtSvg/qsvggenerator.h>
#include <QtSvg/qsvgrenderer.h>
#include <QtSvgWidgets/qsvgwidget.h>
#include <QtWidgets/qapplication.h>

#include <cstdio>
#include <cstring>

//! [generate]
static bool paintPicture(QPaintDevice *device) {
    QPainter painter(device);
    if (!painter.isActive())
        return false;
    painter.fillRect(0, 0, 240, 160, QColor(250, 250, 245));
    painter.setPen(QPen(QColor(40, 60, 90), 3));
    painter.setBrush(QColor(120, 170, 220));
    painter.drawRect(20, 20, 120, 80);
    painter.setBrush(QColor(240, 180, 60));
    painter.drawEllipse(110, 60, 100, 80);
    painter.setPen(QColor(40, 60, 90));
    painter.drawLine(20, 140, 100, 140);
    return true;
}

static bool writePicture(const QString &fileName) {
    QSvgGenerator generator;
    generator.setFileName(fileName);
    generator.setSize(QSize(240, 160));
    generator.setViewBox(QRect(0, 0, 240, 160));
    generator.setTitle("A picture");
    generator.setDescription("Painted with QPainter on a QSvgGenerator.");
    return paintPicture(&generator); // the document is complete when the painter is destroyed
}
//! [generate]

//! [render]
static QImage renderToImage(const QByteArray &svg, const QSize &size) {
    QSvgRenderer renderer(svg);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (renderer.isValid()) {
        renderer.setAspectRatioMode(Qt::KeepAspectRatio);
        QPainter painter(&image);
        renderer.render(&painter);
    }
    return image;
}
//! [render]

int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        if (!writePicture(QString::fromLocal8Bit(argv[2]))) {
            std::fprintf(stderr, "svg_viewer: cannot write %s\n", argv[2]);
            return 1;
        }
        return 0;
    }

    QApplication app(argc, argv);
    //! [widget]
    QSvgWidget widget;
    widget.renderer()->setAspectRatioMode(Qt::KeepAspectRatio);
    if (argc > 1) {
        widget.load(QString::fromLocal8Bit(argv[1]));
    } else {
        // No file: show what the generator writes, straight from memory.
        QBuffer buffer;
        QSvgGenerator generator;
        generator.setOutputDevice(&buffer);
        generator.setSize(QSize(240, 160));
        paintPicture(&generator);
        widget.load(buffer.data());
    }
    if (!widget.renderer()->isValid()) {
        std::fprintf(stderr, "svg_viewer: not an SVG document\n");
        return 1;
    }
    widget.setWindowTitle("Nt SVG");
    widget.resize(widget.sizeHint());
    widget.show();
    //! [widget]

    const QImage preview = renderToImage(QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 8 8'>"
                                                    "<circle cx='4' cy='4' r='3' fill='teal'/></svg>"),
                                         QSize(64, 64));
    if (preview.isNull())
        return 1;
    return app.exec();
}
