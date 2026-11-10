// MIT License - see LICENSE
//
// Renders every widget of Nt Widgets in its states, in the light color scheme, to PNG files: the pictures in the
// reference documentation. It needs no display, since it paints with QWidget::render() on the offscreen platform.
//
//   render_widgets <directory>      writes <directory>/widgets/<class>.png
//
// The CMake target `widget_images` runs it into doc/images. The PNG encoder at the top is all there is to it: Nt has
// no image file support yet.
#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLCDNumber>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleHints>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCursor>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace {

// ---- PNG: a small encoder, with deflate using the fixed Huffman codes and a search for repeated bytes ----

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

class BitWriter {
public:
    void bits(uint32_t value, int count) { // least significant bit first
        for (int i = 0; i < count; ++i)
            put((value >> i) & 1);
    }
    void code(uint32_t value, int count) { // Huffman codes go most significant bit first
        for (int i = count - 1; i >= 0; --i)
            put((value >> i) & 1);
    }
    std::vector<uint8_t> finish() {
        if (m_used)
            m_bytes.push_back(m_current);
        return m_bytes;
    }

private:
    void put(uint32_t bit) {
        m_current = static_cast<uint8_t>(m_current | (bit << m_used));
        if (++m_used == 8) {
            m_bytes.push_back(m_current);
            m_current = 0;
            m_used = 0;
        }
    }
    std::vector<uint8_t> m_bytes;
    uint8_t m_current = 0;
    int m_used = 0;
};

void literalOrLength(BitWriter &w, int symbol) {
    if (symbol < 144)
        w.code(0x30u + static_cast<uint32_t>(symbol), 8);
    else if (symbol < 256)
        w.code(0x190u + static_cast<uint32_t>(symbol - 144), 9);
    else if (symbol < 280)
        w.code(static_cast<uint32_t>(symbol - 256), 7);
    else
        w.code(0xc0u + static_cast<uint32_t>(symbol - 280), 8);
}

std::vector<uint8_t> deflate(const std::vector<uint8_t> &in) {
    static const int lengthBase[] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int lengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int distBase[] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                   33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int distExtra[] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    BitWriter w;
    w.bits(1, 1); // the only block
    w.bits(1, 2); // fixed Huffman codes
    const size_t n = in.size();
    const size_t window = 32768;
    std::vector<int32_t> head(1 << 15, -1), prev(n, -1);
    auto hash = [&](size_t i) { return ((in[i] << 10) ^ (in[i + 1] << 5) ^ in[i + 2]) & 0x7fff; };
    size_t i = 0;
    while (i < n) {
        size_t bestLength = 0, bestDistance = 0;
        if (i + 3 <= n) {
            const int h = hash(i);
            int32_t candidate = head[static_cast<size_t>(h)];
            for (int tries = 0; candidate >= 0 && tries < 64 && i - static_cast<size_t>(candidate) <= window; ++tries) {
                size_t length = 0;
                const size_t limit = std::min<size_t>(258, n - i);
                while (length < limit && in[static_cast<size_t>(candidate) + length] == in[i + length])
                    ++length;
                if (length > bestLength) {
                    bestLength = length;
                    bestDistance = i - static_cast<size_t>(candidate);
                    if (length == limit)
                        break;
                }
                candidate = prev[static_cast<size_t>(candidate)];
            }
        }
        const size_t advance = bestLength >= 3 ? bestLength : 1;
        if (bestLength >= 3) {
            int code = 28;
            while (lengthBase[code] > static_cast<int>(bestLength))
                --code;
            literalOrLength(w, 257 + code);
            w.bits(static_cast<uint32_t>(static_cast<int>(bestLength) - lengthBase[code]), lengthExtra[code]);
            int dcode = 29;
            while (distBase[dcode] > static_cast<int>(bestDistance))
                --dcode;
            w.code(static_cast<uint32_t>(dcode), 5);
            w.bits(static_cast<uint32_t>(static_cast<int>(bestDistance) - distBase[dcode]), distExtra[dcode]);
        } else {
            literalOrLength(w, in[i]);
        }
        for (size_t k = 0; k < advance; ++k, ++i) {
            if (i + 3 <= n) {
                const size_t h = static_cast<size_t>(hash(i));
                prev[i] = head[h];
                head[h] = static_cast<int32_t>(i);
            }
        }
    }
    literalOrLength(w, 256);
    return w.finish();
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    auto be32 = [&](uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8)
            out.push_back(static_cast<uint8_t>(v >> shift));
    };
    be32(static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    be32(crc32(body.data(), body.size()));
}

// Writes `image` as an 8-bit RGB PNG; the alpha channel is dropped.
bool writePng(const QImage &image, const std::string &path) {
    const int w = image.width(), h = image.height();
    const size_t stride = static_cast<size_t>(w) * 3;
    std::vector<uint8_t> raw;
    std::vector<uint8_t> previous(stride, 0), row(stride);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = image.pixel(x, y);
            row[static_cast<size_t>(x) * 3] = static_cast<uint8_t>(qRed(p));
            row[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>(qGreen(p));
            row[static_cast<size_t>(x) * 3 + 2] = static_cast<uint8_t>(qBlue(p));
        }
        // Choose among no filter, Sub and Up the one whose bytes are the smallest in sum, as a signed value.
        std::vector<uint8_t> candidates[3];
        candidates[0] = row;
        candidates[1].resize(stride);
        candidates[2].resize(stride);
        for (size_t i = 0; i < stride; ++i) {
            candidates[1][i] = static_cast<uint8_t>(row[i] - (i >= 3 ? row[i - 3] : 0));
            candidates[2][i] = static_cast<uint8_t>(row[i] - previous[i]);
        }
        int best = 0;
        long bestSum = -1;
        for (int f = 0; f < 3; ++f) {
            long sum = 0;
            for (uint8_t b : candidates[f])
                sum += b < 128 ? b : 256 - b;
            if (bestSum < 0 || sum < bestSum) {
                best = f;
                bestSum = sum;
            }
        }
        raw.push_back(static_cast<uint8_t>(best));
        raw.insert(raw.end(), candidates[best].begin(), candidates[best].end());
        previous = row;
    }
    // zlib: header, deflate, Adler-32
    std::vector<uint8_t> z{0x78, 0x01};
    const std::vector<uint8_t> compressed = deflate(raw);
    z.insert(z.end(), compressed.begin(), compressed.end());
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    const uint32_t adler = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8)
        z.push_back(static_cast<uint8_t>(adler >> shift));

    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> header;
    for (uint32_t v : {static_cast<uint32_t>(w), static_cast<uint32_t>(h)})
        for (int shift = 24; shift >= 0; shift -= 8)
            header.push_back(static_cast<uint8_t>(v >> shift));
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8 bits, RGB, deflate, filters, no interlace
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), file) == png.size();
    std::fclose(file);
    return ok;
}

// ---- The pictures ----

constexpr int kScale = 2; // pixels of the picture per unit of the widgets

// Puts `inner` in a widget of its own, so that radio buttons made like this do not share a group.
QWidget *alone(QWidget *inner, QWidget *parent) {
    auto *box = new QWidget(parent);
    auto *layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    inner->setParent(box);
    layout->addWidget(inner);
    return box;
}

// Shows `host`, gives the keyboard focus to `focus` (or to nothing), and writes its picture.
bool picture(QWidget &host, QWidget *focus, const std::string &directory, const char *name) {
    host.show();
    QCoreApplication::processEvents();
    if (QWidget *current = host.focusWidget())
        current->clearFocus();
    if (focus)
        focus->setFocus();
    QCoreApplication::processEvents();
    QImage image(host.size() * kScale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(kScale);
    image.fill(0u);
    host.render(&image);
    const std::string path = directory + "/widgets/" + name + ".png";
    if (!writePng(image, path)) {
        std::fprintf(stderr, "render_widgets: cannot write %s\n", path.c_str());
        return false;
    }
    std::printf("%s  %dx%d\n", path.c_str(), image.width(), image.height());
    return true;
}

// A row or a column of widgets in a window of its own.
QWidget *host(int width, int height, QBoxLayout::Direction direction = QBoxLayout::TopToBottom) {
    auto *window = new QWidget;
    window->resize(width, height);
    auto *layout = new QBoxLayout(direction, window);
    layout->setSpacing(6);
    return window;
}

bool labels(const std::string &dir) {
    QWidget &w = *host(240, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *plain = new QLabel("A label shows a text.", &w);
    auto *bold = new QLabel("The same, in bold and larger.", &w);
    QFont font = bold->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 3);
    bold->setFont(font);
    auto *wrapped = new QLabel("With word wrap on, a long text is broken into lines to fit the width.", &w);
    wrapped->setWordWrap(true);
    wrapped->setFixedHeight(2 * wrapped->fontMetrics().height() + 2); // a label has no height for its width yet
    auto *right = new QLabel("Aligned to the right", &w);
    right->setAlignment(Qt::AlignRight);
    auto *disabled = new QLabel("A disabled label", &w);
    disabled->setEnabled(false);
    for (QLabel *l : {plain, bold, wrapped, right, disabled})
        layout->addWidget(l);
    w.resize(240, w.sizeHint().height());
    return picture(w, nullptr, dir, "qlabel");
}

bool pushButtons(const std::string &dir) {
    QWidget &w = *host(380, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto row = [&](std::initializer_list<QPushButton *> buttons) {
        auto *line = new QHBoxLayout;
        for (QPushButton *b : buttons)
            line->addWidget(b);
        line->addStretch();
        layout->addLayout(line);
    };
    auto *normal = new QPushButton("Normal", &w);
    auto *focused = new QPushButton("Focused", &w);
    auto *pressed = new QPushButton("Pressed", &w);
    pressed->setDown(true);
    auto *checked = new QPushButton("Checked", &w);
    checked->setCheckable(true);
    checked->setChecked(true);
    auto *flat = new QPushButton("Flat", &w);
    flat->setFlat(true);
    auto *disabled = new QPushButton("Disabled", &w);
    disabled->setEnabled(false);
    row({normal, focused, pressed});
    row({checked, flat, disabled});
    w.resize(380, w.sizeHint().height());
    return picture(w, focused, dir, "qpushbutton");
}

bool checkBoxes(const std::string &dir) {
    QWidget &w = *host(220, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *off = new QCheckBox("Unchecked", &w);
    auto *on = new QCheckBox("Checked", &w);
    on->setChecked(true);
    auto *focused = new QCheckBox("Focused", &w);
    auto *disabled = new QCheckBox("Disabled", &w);
    disabled->setEnabled(false);
    auto *disabledOn = new QCheckBox("Disabled and checked", &w);
    disabledOn->setChecked(true);
    disabledOn->setEnabled(false);
    for (QCheckBox *b : {off, on, focused, disabled, disabledOn})
        layout->addWidget(b);
    w.resize(220, w.sizeHint().height());
    return picture(w, focused, dir, "qcheckbox");
}

bool radioButtons(const std::string &dir) {
    QWidget &w = *host(220, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    // Each in a widget of its own, so that they are not exclusive to each other.
    auto *off = new QRadioButton("Not chosen", &w);
    auto *on = new QRadioButton("Chosen", &w);
    on->setChecked(true);
    auto *focused = new QRadioButton("Focused", &w);
    auto *disabled = new QRadioButton("Disabled", &w);
    disabled->setEnabled(false);
    auto *disabledOn = new QRadioButton("Disabled and chosen", &w);
    disabledOn->setChecked(true);
    disabledOn->setEnabled(false);
    for (QRadioButton *b : {off, on, focused, disabled, disabledOn})
        layout->addWidget(alone(b, &w));
    w.resize(220, w.sizeHint().height());
    return picture(w, focused, dir, "qradiobutton");
}

bool lineEdits(const std::string &dir) {
    QWidget &w = *host(240, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *placeholder = new QLineEdit(&w);
    placeholder->setPlaceholderText("Placeholder text");
    auto *typed = new QLineEdit("Text with the cursor", &w);
    typed->setCursorPosition(9);
    auto *selected = new QLineEdit("Some selected text", &w);
    selected->setSelection(5, 8);
    auto *password = new QLineEdit("secret", &w);
    password->setEchoMode(QLineEdit::Password);
    auto *readOnly = new QLineEdit("Read-only text", &w);
    readOnly->setReadOnly(true);
    auto *disabled = new QLineEdit("Disabled", &w);
    disabled->setEnabled(false);
    for (QLineEdit *e : {placeholder, typed, selected, password, readOnly, disabled})
        layout->addWidget(e);
    w.resize(240, w.sizeHint().height());
    return picture(w, typed, dir, "qlineedit");
}

bool sliders(const std::string &dir) {
    QWidget &w = *host(280, 120, QBoxLayout::LeftToRight);
    auto *left = new QVBoxLayout;
    auto *low = new QSlider(Qt::Horizontal, &w);
    low->setValue(0);
    auto *middle = new QSlider(Qt::Horizontal, &w);
    middle->setValue(35);
    auto *high = new QSlider(Qt::Horizontal, &w);
    high->setValue(99);
    auto *focused = new QSlider(Qt::Horizontal, &w);
    focused->setValue(60);
    auto *disabled = new QSlider(Qt::Horizontal, &w);
    disabled->setValue(35);
    disabled->setEnabled(false);
    for (QSlider *s : {low, middle, high, focused, disabled})
        left->addWidget(s);
    left->addStretch();
    auto *vertical = new QSlider(Qt::Vertical, &w);
    vertical->setValue(60);
    static_cast<QBoxLayout *>(w.layout())->addLayout(left, 1);
    w.layout()->addWidget(vertical);
    return picture(w, focused, dir, "qslider");
}

bool progressBars(const std::string &dir) {
    QWidget &w = *host(240, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *none = new QProgressBar(&w);
    auto *some = new QProgressBar(&w);
    some->setValue(35);
    auto *full = new QProgressBar(&w);
    full->setValue(100);
    auto *format = new QProgressBar(&w);
    format->setRange(0, 200);
    format->setValue(50);
    format->setFormat("%v of %m");
    auto *silent = new QProgressBar(&w);
    silent->setValue(70);
    silent->setTextVisible(false);
    auto *disabled = new QProgressBar(&w);
    disabled->setValue(35);
    disabled->setEnabled(false);
    for (QProgressBar *b : {none, some, full, format, silent, disabled})
        layout->addWidget(b);
    w.resize(240, w.sizeHint().height());
    return picture(w, nullptr, dir, "qprogressbar");
}

bool spinBoxes(const std::string &dir) {
    QWidget &w = *host(200, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *plain = new QSpinBox(&w);
    plain->setValue(35);
    auto *focused = new QSpinBox(&w);
    focused->setRange(0, 100);
    focused->setValue(72);
    focused->setSuffix(" %");
    auto *prefixed = new QSpinBox(&w);
    prefixed->setRange(-100, 100);
    prefixed->setValue(-5);
    prefixed->setPrefix("Offset ");
    auto *atEnd = new QSpinBox(&w);
    atEnd->setValue(99);
    auto *disabled = new QSpinBox(&w);
    disabled->setValue(12);
    disabled->setEnabled(false);
    for (QSpinBox *s : {plain, focused, prefixed, atEnd, disabled})
        layout->addWidget(s);
    w.resize(200, w.sizeHint().height());
    return picture(w, focused, dir, "qspinbox");
}

bool comboBoxes(const std::string &dir) {
    QWidget &w = *host(220, 200);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    const QStringList items{"Teal", "Orange", "Green", "Purple"};
    auto *closed = new QComboBox(&w);
    closed->addItems(items);
    auto *disabled = new QComboBox(&w);
    disabled->addItems(items);
    disabled->setEnabled(false);
    auto *open = new QComboBox(&w);
    open->addItems(items);
    open->setCurrentIndex(1);
    layout->addWidget(closed);
    layout->addWidget(disabled);
    layout->addWidget(open);
    layout->addStretch();
    w.show();
    QCoreApplication::processEvents();
    open->showPopup();
    return picture(w, nullptr, dir, "qcombobox");
}

bool groupBoxes(const std::string &dir) {
    QWidget &w = *host(260, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto make = [&](const char *title, bool checkable, bool checked) {
        auto *group = new QGroupBox(title, &w);
        auto *inner = new QHBoxLayout(group);
        inner->addWidget(new QCheckBox("An option", group));
        inner->addWidget(new QPushButton("A button", group));
        // The group box enables and disables the widgets that are in it, so they come first.
        group->setCheckable(checkable);
        group->setChecked(checked);
        layout->addWidget(group);
    };
    make("A group box", false, true);
    make("Checkable and checked", true, true);
    make("Checkable and unchecked", true, false);
    w.resize(260, w.sizeHint().height());
    return picture(w, nullptr, dir, "qgroupbox");
}

bool tabWidgets(const std::string &dir) {
    QWidget &w = *host(300, 150);
    auto *tabs = new QTabWidget(&w);
    for (const char *name : {"General", "Details", "Disabled"}) {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->addWidget(new QLabel(QString("The page \"%1\".").arg(name), page));
        layout->addWidget(new QPushButton("A button", page));
        layout->addStretch();
        tabs->addTab(page, name);
    }
    tabs->setTabEnabled(2, false);
    w.layout()->addWidget(tabs);
    return picture(w, nullptr, dir, "qtabwidget");
}

bool frames(const std::string &dir) {
    QWidget &w = *host(380, 10);
    auto *grid = new QGridLayout;
    static_cast<QBoxLayout *>(w.layout())->addLayout(grid);
    struct Sample {
        const char *name;
        int style;
        int lineWidth;
    };
    const Sample samples[] = {{"Box", QFrame::Box | QFrame::Plain, 1},
                              {"Box, raised", QFrame::Box | QFrame::Raised, 2},
                              {"Panel, sunken", QFrame::Panel | QFrame::Sunken, 2},
                              {"StyledPanel", QFrame::StyledPanel | QFrame::Sunken, 1}};
    int column = 0;
    for (const Sample &sample : samples) {
        auto *frame = new QFrame(&w);
        frame->setFrameStyle(sample.style);
        frame->setLineWidth(sample.lineWidth);
        frame->setMinimumSize(70, 44);
        auto *caption = new QLabel(sample.name, &w);
        caption->setAlignment(Qt::AlignHCenter);
        grid->addWidget(frame, 0, column);
        grid->addWidget(caption, 1, column);
        ++column;
    }
    auto *hline = new QFrame(&w);
    hline->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    auto *caption = new QLabel("A sunken HLine", &w);
    caption->setAlignment(Qt::AlignHCenter);
    grid->addWidget(hline, 2, 0, 1, 4);
    grid->addWidget(caption, 3, 0, 1, 4);
    w.resize(380, w.sizeHint().height());
    return picture(w, nullptr, dir, "qframe");
}

bool gridLayouts(const std::string &dir) {
    QWidget &w = *host(300, 10);
    auto *grid = new QGridLayout;
    static_cast<QBoxLayout *>(w.layout())->addLayout(grid);
    grid->addWidget(new QLabel("Name", &w), 0, 0);
    grid->addWidget(new QLineEdit("Ada Lovelace", &w), 0, 1, 1, 2);
    grid->addWidget(new QLabel("Role", &w), 1, 0);
    auto *role = new QComboBox(&w);
    role->addItems({"Engineer", "Designer", "Writer"});
    grid->addWidget(role, 1, 1, 1, 2);
    grid->addWidget(new QLabel("Notes", &w), 2, 0, Qt::AlignTop);
    auto *notes = new QFrame(&w);
    notes->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    notes->setMinimumSize(0, 40);
    grid->addWidget(notes, 2, 1, 1, 2);
    grid->addWidget(new QPushButton("OK", &w), 3, 1);
    grid->addWidget(new QPushButton("Cancel", &w), 3, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    w.resize(300, w.sizeHint().height());
    return picture(w, nullptr, dir, "qgridlayout");
}

bool formLayouts(const std::string &dir) {
    QWidget &w = *host(320, 10);
    auto *form = new QFormLayout;
    static_cast<QBoxLayout *>(w.layout())->addLayout(form);
    form->addRow("Name:", new QLineEdit("Ada Lovelace", &w));
    auto *age = new QSpinBox(&w);
    age->setValue(36);
    form->addRow("Age:", age);
    auto *role = new QComboBox(&w);
    role->addItems({"Engineer", "Designer", "Writer"});
    form->addRow("Role:", role);
    auto *notes = new QFrame(&w);
    notes->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    notes->setMinimumSize(0, 46);
    notes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    form->addRow("Notes:", notes);
    form->addRow(new QPushButton("Save", &w));
    w.resize(320, w.sizeHint().height());
    return picture(w, nullptr, dir, "qformlayout");
}

bool stackedWidgets(const std::string &dir) {
    QWidget &w = *host(280, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *choice = new QComboBox(&w);
    choice->addItems({"General", "Advanced", "About"});
    layout->addWidget(choice);
    auto *group = new QGroupBox(&w);
    auto *inner = new QVBoxLayout(group);
    auto *stack = new QStackedWidget(group);
    inner->addWidget(stack);
    for (int page = 0; page < 3; ++page) {
        auto *content = new QWidget;
        auto *column = new QVBoxLayout(content);
        column->setContentsMargins(0, 0, 0, 0);
        if (page == 0) {
            column->addWidget(new QCheckBox("Check for updates", content));
            column->addWidget(new QCheckBox("Send usage reports", content));
        } else if (page == 1) {
            column->addWidget(new QLabel("The advanced page.", content));
        } else {
            column->addWidget(new QLabel("The about page.", content));
        }
        stack->addWidget(content);
    }
    layout->addWidget(group);
    w.resize(280, w.sizeHint().height());
    return picture(w, nullptr, dir, "qstackedwidget");
}

bool toolTips(const std::string &dir) {
    QWidget &w = *host(280, 120);
    auto *button = new QPushButton("Save", &w);
    button->setToolTip("Saves the document");
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    layout->addWidget(button);
    layout->addStretch();
    w.show();
    QCoreApplication::processEvents();
    QToolTip::showText(button->mapToGlobal(QPoint(30, 12)), "Saves the document", button);
    return picture(w, nullptr, dir, "qtooltip");
}

bool scrollBars(const std::string &dir) {
    QWidget &w = *host(300, 10, QBoxLayout::LeftToRight);
    auto *left = new QVBoxLayout;
    static_cast<QBoxLayout *>(w.layout())->addLayout(left);
    auto bar = [&](int value, int maximum, int page, bool enabled) {
        auto *b = new QScrollBar(Qt::Horizontal, &w);
        b->setRange(0, maximum);
        b->setPageStep(page);
        b->setValue(value);
        b->setEnabled(enabled);
        left->addWidget(b);
    };
    bar(0, 100, 50, true);
    bar(40, 100, 50, true);
    bar(100, 100, 50, true);
    bar(300, 1000, 40, true);
    bar(0, 100, 50, false);
    left->addStretch();
    auto *vertical = new QScrollBar(Qt::Vertical, &w);
    vertical->setRange(0, 100);
    vertical->setPageStep(40);
    vertical->setValue(30);
    vertical->setMinimumSize(0, 100);
    w.layout()->addWidget(vertical);
    w.resize(300, w.sizeHint().height());
    return picture(w, nullptr, dir, "qscrollbar");
}

bool scrollAreas(const std::string &dir) {
    QWidget &w = *host(260, 150);
    auto *area = new QScrollArea(&w);
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    for (int i = 1; i <= 6; ++i) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(QString("Setting %1").arg(i), page));
        row->addWidget(new QCheckBox("Enabled", page));
        layout->addLayout(row);
    }
    layout->addWidget(new QPushButton("Apply", page));
    area->setWidget(page);
    area->setWidgetResizable(true);
    w.layout()->addWidget(area);
    w.show();
    QCoreApplication::processEvents();
    area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum() / 3);
    return picture(w, nullptr, dir, "qscrollarea");
}

// A table with the labels of a small staff list, for the pictures of the table and of its header.
QTableWidget *staffTable(QWidget *parent) {
    auto *table = new QTableWidget(7, 3, parent);
    table->setHorizontalHeaderLabels({"Name", "Role", "Status"});
    const char *rows[][3] = {{"Ada", "Engineer", "Active"},    {"Grace", "Admiral", "Active"},
                             {"Linus", "Maintainer", "Away"},  {"Margaret", "Director", "Active"},
                             {"Dennis", "Researcher", "Away"}, {"Barbara", "Professor", "Active"},
                             {"Ken", "Engineer", "Active"}};
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 3; ++c)
            table->setItem(r, c, new QTableWidgetItem(rows[r][c]));
    table->setColumnWidth(0, 110);
    table->setColumnWidth(1, 120);
    table->setColumnWidth(2, 70);
    return table;
}

bool listWidgets(const std::string &dir) {
    QWidget &w = *host(220, 150);
    auto *list = new QListWidget(&w);
    list->addItems({"Inbox", "Drafts", "Sent", "Archive", "Spam", "Trash", "Projects", "Receipts"});
    list->item(2)->setForeground(QBrush(QColor(190, 70, 20)));
    list->item(3)->setBackground(QBrush(QColor(255, 226, 150)));
    list->item(4)->setFlags(Qt::NoItemFlags);
    list->item(1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    list->setCurrentRow(0);
    list->setCurrentRow(1);
    w.layout()->addWidget(list);
    return picture(w, list, dir, "qlistwidget");
}

bool plainTextEdits(const std::string &dir) {
    QWidget &w = *host(300, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *typed = new QPlainTextEdit(&w);
    typed->setPlainText("Text of many lines, with a selection\nthat goes over the end of a line,\nand a long line that "
                        "wraps at the width of the widget.");
    QTextCursor cursor = typed->textCursor();
    cursor.setPosition(8);
    cursor.setPosition(52, QTextCursor::KeepAnchor);
    typed->setTextCursor(cursor);
    auto *empty = new QPlainTextEdit(&w);
    empty->setPlaceholderText("Write something here");
    auto *log = new QPlainTextEdit(&w);
    log->setReadOnly(true);
    log->appendPlainText("A read-only log:");
    log->appendPlainText("12:00  started");
    log->appendPlainText("12:01  ready");
    for (QPlainTextEdit *edit : {typed, empty, log}) {
        edit->setFixedHeight(edit == typed ? 84 : edit == log ? 66 : 52);
        layout->addWidget(edit);
    }
    w.resize(300, w.sizeHint().height());
    return picture(w, typed, dir, "qplaintextedit");
}

bool toolButtons(const std::string &dir) {
    QWidget &w = *host(300, 10, QBoxLayout::LeftToRight);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *text = new QToolButton(&w);
    text->setText("Text");
    auto *checked = new QToolButton(&w);
    checked->setText("Checked");
    checked->setCheckable(true);
    checked->setChecked(true);
    auto *raised = new QToolButton(&w);
    raised->setText("Flat");
    raised->setAutoRaise(true);
    auto *disabled = new QToolButton(&w);
    disabled->setText("Disabled");
    disabled->setEnabled(false);
    layout->addWidget(text);
    layout->addWidget(checked);
    layout->addWidget(raised);
    layout->addWidget(disabled);
    for (Qt::ArrowType type : {Qt::LeftArrow, Qt::RightArrow, Qt::UpArrow, Qt::DownArrow}) {
        auto *arrow = new QToolButton(&w);
        arrow->setArrowType(type);
        layout->addWidget(arrow);
    }
    layout->addStretch();
    w.resize(300, w.sizeHint().height());
    return picture(w, nullptr, dir, "qtoolbutton");
}

bool dials(const std::string &dir) {
    QWidget &w = *host(10, 10, QBoxLayout::LeftToRight);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *plain = new QDial(&w);
    plain->setValue(25);
    auto *notches = new QDial(&w);
    notches->setNotchesVisible(true);
    notches->setValue(75);
    auto *wrapping = new QDial(&w);
    wrapping->setWrapping(true);
    wrapping->setValue(50);
    auto *disabled = new QDial(&w);
    disabled->setValue(40);
    disabled->setEnabled(false);
    for (QDial *dial : {plain, notches, wrapping, disabled}) {
        dial->setFixedSize(70, 70);
        layout->addWidget(dial);
    }
    w.resize(w.sizeHint());
    return picture(w, nullptr, dir, "qdial");
}

bool lcdNumbers(const std::string &dir) {
    QWidget &w = *host(10, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    int row = 0;
    for (QLCDNumber::SegmentStyle style : {QLCDNumber::Filled, QLCDNumber::Outline, QLCDNumber::Flat}) {
        auto *lcd = new QLCDNumber(6, &w);
        lcd->setSegmentStyle(style);
        lcd->display(row == 0 ? 12.34 : row == 1 ? 5678 : -90.1);
        lcd->setFixedHeight(48);
        layout->addWidget(lcd);
        ++row;
    }
    auto *hex = new QLCDNumber(4, &w);
    hex->setMode(QLCDNumber::Hex);
    hex->display(0xBEEF);
    hex->setFixedHeight(48);
    layout->addWidget(hex);
    w.resize(160, w.sizeHint().height());
    return picture(w, nullptr, dir, "qlcdnumber");
}

bool splitters(const std::string &dir) {
    QWidget &w = *host(320, 190);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *outer = new QSplitter(Qt::Vertical, &w);
    auto *inner = new QSplitter(Qt::Horizontal);
    auto pane = [](const char *name) {
        auto *frame = new QFrame;
        frame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        auto *label = new QLabel(name, frame);
        label->setAlignment(Qt::AlignCenter);
        auto *box = new QVBoxLayout(frame);
        box->addWidget(label);
        return frame;
    };
    for (const char *name : {"Left", "Middle", "Right"})
        inner->addWidget(pane(name));
    outer->addWidget(inner);
    outer->addWidget(pane("Below"));
    inner->setSizes({100, 140, 70});
    outer->setSizes({110, 60});
    layout->addWidget(outer);
    return picture(w, nullptr, dir, "qsplitter");
}

bool statusBars(const std::string &dir) {
    QWidget &w = *host(320, 10);
    auto *layout = static_cast<QBoxLayout *>(w.layout());
    auto *message = new QStatusBar(&w);
    message->showMessage("A message");
    auto *widgets = new QStatusBar(&w);
    widgets->addWidget(new QLabel("Ready", widgets), 1);
    widgets->addPermanentWidget(new QLabel("Ln 3, Col 9", widgets));
    widgets->addPermanentWidget(new QPushButton("Quit", widgets));
    layout->addWidget(message);
    layout->addWidget(widgets);
    w.resize(320, w.sizeHint().height());
    return picture(w, nullptr, dir, "qstatusbar");
}

bool menus(const std::string &dir) {
    QWidget &w = *host(250, 160);
    auto *menu = new QMenu(&w);
    menu->addAction("Open");
    QAction *save = menu->addAction("Save As");
    save->setShortcut(QKeySequence("Ctrl+S"));
    menu->addSeparator();
    QAction *wrap = menu->addAction("Wrap lines");
    wrap->setCheckable(true);
    wrap->setChecked(true);
    QAction *numbers = menu->addAction("Line numbers");
    numbers->setCheckable(true);
    menu->addMenu("More")->addAction("Preferences");
    menu->addAction("Disabled")->setEnabled(false);
    w.show();
    QCoreApplication::processEvents();
    menu->popup(w.mapToGlobal(QPoint(12, 8)));
    menu->setActiveAction(save);
    return picture(w, nullptr, dir, "qmenu");
}

bool menuBars(const std::string &dir) {
    auto *window = new QWidget;
    window->resize(300, 130);
    auto *bar = new QMenuBar(window);
    QMenu *file = bar->addMenu("&File");
    bar->addMenu("&Edit")->addAction("Copy");
    bar->addMenu("&Help")->addAction("About");
    file->addAction("&New");
    file->addAction("&Open...")->setShortcut(QKeySequence("Ctrl+O"));
    file->addSeparator();
    file->addAction("&Quit")->setShortcut(QKeySequence("Ctrl+Q"));
    bar->setGeometry(0, 0, 300, bar->sizeHint().height());
    window->show();
    QCoreApplication::processEvents();
    const QRect item = bar->actionGeometry(file->menuAction());
    bar->setActiveAction(file->menuAction());
    file->popup(window->mapToGlobal(QPoint(item.left() + 2, item.bottom() + 3)));
    return picture(*window, nullptr, dir, "qmenubar");
}

bool headerViews(const std::string &dir) {
    QWidget &w = *host(360, 200);
    auto *table = new QTableWidget(4, 3, &w);
    table->setHorizontalHeaderLabels({"Name", "Role", "Status"});
    table->setVerticalHeaderLabels({"A", "B", "C", "D"});
    table->setColumnWidth(0, 110);
    table->setColumnWidth(1, 90);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setShowGrid(false);
    w.layout()->addWidget(table);
    return picture(w, nullptr, dir, "qheaderview");
}

bool tableWidgets(const std::string &dir) {
    QWidget &w = *host(380, 250);
    QTableWidget *table = staffTable(&w);
    table->setAlternatingRowColors(true);
    table->item(2, 2)->setForeground(QBrush(QColor(190, 70, 20)));
    table->item(5, 0)->setBackground(QBrush(QColor(255, 226, 150)));
    table->item(0, 2)->setTextAlignment(Qt::AlignCenter);
    table->setCurrentCell(1, 1);
    w.layout()->addWidget(table);
    return picture(w, table, dir, "qtablewidget");
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: render_widgets <directory>\n");
        return 2;
    }
    // The pictures do not depend on the machine: no display, and sizes in units of one pixel.
    ::setenv("QT_QPA_PLATFORM", "offscreen", 1);
    ::setenv("QT_SCALE_FACTOR", "1", 1);
    QApplication app(argc, argv);
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);

    const std::string directory = argv[1];
    ::mkdir(directory.c_str(), 0755);
    ::mkdir((directory + "/widgets").c_str(), 0755);
    bool ok = true;
    for (bool (*render)(const std::string &) :
         {labels,     pushButtons, checkBoxes,  radioButtons,   lineEdits,   sliders,     progressBars,   spinBoxes,
          comboBoxes, groupBoxes,  tabWidgets,  frames,         gridLayouts, formLayouts, stackedWidgets, toolTips,
          scrollBars, scrollAreas, listWidgets, plainTextEdits, headerViews, toolButtons, dials,          lcdNumbers,
          splitters,  statusBars,  menus,       menuBars,       tableWidgets})
        ok = render(directory) && ok;
    return ok ? 0 : 1;
}
