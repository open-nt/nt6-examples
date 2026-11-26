// MIT License - see LICENSE
//
// The Material 3 Expressive style of Nt (QMaterialStyle): buttons, a text field, check boxes, radio buttons,
// switches, sliders, progress bars, tabs, combo boxes, menus, dialogs, a list, a button group, a split button, a
// floating action button, a floating toolbar and button sizes, in a scheme made from a seed color, light or dark. With Qt, which has
// no such style, it builds and runs in the default style.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScroller>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QWidget>
#if __has_include(<QMaterialStyle>)
#include <QMaterialStyle>
#define HAVE_MATERIAL_STYLE
#endif

namespace {

QPushButton *button(const QString &text, const char *kind) {
    auto *b = new QPushButton(text);
    if (kind)
        b->setProperty("materialButton", QString::fromLatin1(kind));
    return b;
}

void apply(const QColor &seed, bool dark) {
#ifdef HAVE_MATERIAL_STYLE
    auto *style = new QMaterialStyle(seed);
    style->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    QApplication::setStyle(style);
#else
    Q_UNUSED(seed)
    Q_UNUSED(dark)
#endif
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    const bool systemDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    QColor seed(0x67, 0x50, 0xa4);
#ifdef HAVE_MATERIAL_STYLE
    // The color of the device's own theme, where it has one (Android 12 and later).
    const QColor system = QMaterialStyle::systemSeedColor();
    if (system.isValid())
        seed = system;
#endif
    apply(seed, systemDark);

    QWidget window;
    auto *outer = new QVBoxLayout(&window);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *tabs = new QTabWidget;
    tabs->tabBar()->setExpanding(true); // Material's primary tabs share the width
    outer->addWidget(tabs);
    auto *controls = new QWidget;
    auto *layout = new QVBoxLayout(controls);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("Material 3 Expressive"));
    QFont heading = title->font();
    heading.setPixelSize(24);
    title->setFont(heading);
    layout->addWidget(title);

    auto *first = new QHBoxLayout;
    auto *count = button(QStringLiteral("Filled"), "filled");
    first->addWidget(count);
    first->addWidget(button(QStringLiteral("Tonal"), "tonal"));
    first->addWidget(button(QStringLiteral("Outlined"), "outlined"));
    layout->addLayout(first);
    auto *second = new QHBoxLayout;
    second->addWidget(button(QStringLiteral("Elevated"), "elevated"));
    second->addWidget(button(QStringLiteral("Text"), "text"));
    auto *disabled = button(QStringLiteral("Disabled"), nullptr);
    disabled->setEnabled(false);
    second->addWidget(disabled);
    layout->addLayout(second);
    int taps = 0;
    QObject::connect(count, &QPushButton::clicked,
                     [&taps, count] { count->setText(QStringLiteral("Tapped %1").arg(++taps)); });

    auto *edit = new QLineEdit;
    edit->setPlaceholderText(QStringLiteral("Outlined text field"));
    layout->addWidget(edit);

    auto *check = new QCheckBox(QStringLiteral("Check box"));
    check->setChecked(true);
    layout->addWidget(check);
    auto *radios = new QHBoxLayout;
    auto *one = new QRadioButton(QStringLiteral("Walnut"));
    one->setChecked(true);
    radios->addWidget(one);
    radios->addWidget(new QRadioButton(QStringLiteral("Pecan")));
    layout->addLayout(radios);

    auto *wifi = new QCheckBox(QStringLiteral("Switch"));
    wifi->setProperty("materialSwitch", true);
    wifi->setChecked(true);
    layout->addWidget(wifi);
    auto *dark = new QCheckBox(QStringLiteral("Dark theme"));
    dark->setProperty("materialSwitch", true);
    dark->setChecked(systemDark);
    layout->addWidget(dark);
    QObject::connect(dark, &QCheckBox::toggled, [&seed](bool on) { apply(seed, on); });

    layout->addWidget(new QLabel(QStringLiteral("Seed color")));
    auto *seeds = new QHBoxLayout;
    const std::pair<const char *, QColor> choices[] = {{"Violet", QColor(0x67, 0x50, 0xa4)},
                                                       {"Green", QColor(0x00, 0x6c, 0x4c)},
                                                       {"Orange", QColor(0xb3, 0x59, 0x00)}};
    for (const auto &[name, color] : choices) {
        auto *pick = button(QString::fromLatin1(name), "outlined");
        seeds->addWidget(pick);
        QObject::connect(pick, &QPushButton::clicked, [&seed, color = color, dark] {
            seed = color;
            apply(seed, dark->isChecked());
        });
    }
#ifdef HAVE_MATERIAL_STYLE
    if (system.isValid()) {
        auto *pick = button(QStringLiteral("System"), "outlined");
        seeds->addWidget(pick);
        QObject::connect(pick, &QPushButton::clicked, [&seed, system, dark] {
            seed = system;
            apply(seed, dark->isChecked());
        });
    }
#endif
    layout->addLayout(seeds);
    layout->addStretch(1);
    tabs->addTab(controls, QStringLiteral("Controls"));

    // A slider that sets two progress bars, and a slider with steps.
    auto *more = new QWidget;
    auto *page = new QVBoxLayout(more);
    page->setContentsMargins(16, 16, 16, 16);
    page->setSpacing(16);
    page->addWidget(new QLabel(QStringLiteral("Slider")));
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    page->addWidget(slider);
    page->addWidget(new QLabel(QStringLiteral("Progress")));
    auto *progress = new QProgressBar;
    progress->setTextVisible(false);
    page->addWidget(progress);
    auto *labelled = new QProgressBar;
    page->addWidget(labelled);
    // The part that is done as a wave.
    auto *wavy = new QProgressBar;
    wavy->setProperty("materialProgress", QStringLiteral("wavy"));
    wavy->setTextVisible(false);
    page->addWidget(wavy);
    QObject::connect(slider, &QSlider::valueChanged, wavy, &QProgressBar::setValue);
    QObject::connect(slider, &QSlider::valueChanged, progress, &QProgressBar::setValue);
    QObject::connect(slider, &QSlider::valueChanged, labelled, &QProgressBar::setValue);
    slider->setValue(40);
    page->addWidget(new QLabel(QStringLiteral("Steps")));
    auto *steps = new QSlider(Qt::Horizontal);
    steps->setRange(0, 5);
    steps->setTickInterval(1);
    steps->setTickPosition(QSlider::TicksBelow);
    steps->setValue(2);
    page->addWidget(steps);
    auto *off = new QSlider(Qt::Horizontal);
    off->setValue(60);
    off->setEnabled(false);
    page->addWidget(off);
    // A connected button group, for a choice of one.
    page->addWidget(new QLabel(QStringLiteral("Button group")));
    auto *group = new QWidget;
    group->setProperty("materialButtonGroup", QStringLiteral("connected"));
    auto *periods = new QHBoxLayout(group);
    periods->setContentsMargins(0, 0, 0, 0);
    for (const char *name : {"Day", "Week", "Month"}) {
        auto *period = new QPushButton(QString::fromLatin1(name));
        period->setCheckable(true);
        period->setAutoExclusive(true);
        period->setChecked(periods->count() == 0);
        periods->addWidget(period);
    }
    page->addWidget(group);
    // Busy: a range of 0 to 0, as a line, or as the loading indicator in either of its two forms.
    page->addWidget(new QLabel(QStringLiteral("Busy")));
    auto *busy = new QProgressBar;
    busy->setRange(0, 0);
    page->addWidget(busy);
    auto *indicators = new QHBoxLayout;
    indicators->setSpacing(24);
    for (const char *form : {"loading", "containedLoading"}) {
        auto *indicator = new QProgressBar;
        indicator->setProperty("materialProgress", QString::fromLatin1(form));
        indicator->setRange(0, 0);
        indicators->addWidget(indicator);
    }
    indicators->addStretch(1);
    page->addLayout(indicators);
    page->addStretch(1);
    // A floating toolbar, with choices that stay checked. It would go over the content of an editor.
    auto *format = new QToolBar;
    format->setProperty("materialToolBar", QStringLiteral("floating"));
    for (const char *name : {"B", "I", "U"})
        format->addAction(QString::fromLatin1(name))->setCheckable(true);
    format->actions().first()->setChecked(true);
    format->addSeparator();
    format->addAction(QStringLiteral("Aa"));
    page->addWidget(format, 0, Qt::AlignHCenter);
    tabs->addTab(more, QStringLiteral("More"));

    // A combo box, a menu and a dialog.
    auto *popups = new QWidget;
    auto *column = new QVBoxLayout(popups);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(16);
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("Walnut"), QStringLiteral("Pecan"), QStringLiteral("Chestnut"),
                     QStringLiteral("Almond")});
    column->addWidget(combo);
    auto *menuButton = button(QStringLiteral("Menu"), "outlined");
    auto *menu = new QMenu(menuButton);
    menu->addAction(QStringLiteral("Share"));
    menu->addAction(QStringLiteral("Rename"));
    menu->addSeparator();
    menu->addAction(QStringLiteral("Delete"));
    menuButton->setMenu(menu);
    column->addWidget(menuButton);
    auto *ask = button(QStringLiteral("Dialog"), "filled");
    column->addWidget(ask);
    // A split button: its action, and a menu of others.
    auto *send = new QToolButton;
    send->setText(QStringLiteral("Send"));
    send->setProperty("materialButton", QStringLiteral("filled"));
    auto *others = new QMenu(send);
    others->addAction(QStringLiteral("Send later"));
    others->addAction(QStringLiteral("Save draft"));
    send->setMenu(others);
    send->setPopupMode(QToolButton::MenuButtonPopup);
    column->addWidget(send, 0, Qt::AlignLeft);
    int sent = 0;
    QObject::connect(send, &QToolButton::clicked,
                     [&sent, send] { send->setText(QStringLiteral("Sent %1").arg(++sent)); });
    QObject::connect(ask, &QPushButton::clicked, [&window] {
        auto *box = new QMessageBox(&window);
        box->setText(QStringLiteral("Delete the note?"));
        box->setInformativeText(QStringLiteral("It cannot be brought back."));
        box->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    });
    column->addStretch(1);
    // A floating action button, which opens a menu of what can be made.
    auto *fab = button(QString(), "fab");
    auto *create = new QMenu(fab);
    create->addAction(QStringLiteral("Note"));
    create->addAction(QStringLiteral("Shopping list"));
    create->addAction(QStringLiteral("Reminder"));
    fab->setMenu(create);
    column->addWidget(fab, 0, Qt::AlignRight);
    tabs->addTab(popups, QStringLiteral("Popups"));

    // A list that scrolls under a finger.
    auto *list = new QListWidget;
    list->setFrameShape(QFrame::NoFrame);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    const char *const nuts[] = {"Acorn", "Almond",    "Beechnut", "Brazil nut", "Candlenut", "Cashew",
                                "Chestnut", "Filbert", "Ginkgo",   "Hazelnut",   "Hickory",   "Kola nut",
                                "Macadamia", "Pecan",  "Pine nut", "Pistachio",  "Walnut"};
    for (const char *nut : nuts)
        list->addItem(QString::fromLatin1(nut));
    QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
    tabs->addTab(list, QStringLiteral("Lists"));

    // The five sizes of a button; the last is square, with corners that turn round while it is checked.
    auto *sizes = new QWidget;
    auto *rows = new QVBoxLayout(sizes);
    rows->setContentsMargins(16, 16, 16, 16);
    rows->setSpacing(16);
    const std::pair<const char *, const char *> each[] = {{"extraSmall", "Extra small"},
                                                         {"small", "Small"},
                                                         {"medium", "Medium"},
                                                         {"large", "Large"},
                                                         {"extraLarge", "Huge"}};
    for (const auto &[size, name] : each) {
        auto *sized = button(QString::fromLatin1(name), "filled");
        sized->setProperty("materialButtonSize", QString::fromLatin1(size));
        rows->addWidget(sized, 0, Qt::AlignLeft);
    }
    auto *square = button(QStringLiteral("Square"), "tonal");
    square->setProperty("materialButtonSize", QStringLiteral("large"));
    square->setProperty("materialButtonShape", QStringLiteral("square"));
    square->setCheckable(true);
    rows->addWidget(square, 0, Qt::AlignLeft);
    rows->addStretch(1);
    tabs->addTab(sizes, QStringLiteral("Sizes"));

    window.show();
    return app.exec();
}
