// MIT License - see LICENSE
//
// The Material 3 Expressive style of Nt (QMaterialStyle): buttons, a text field, check boxes, radio buttons,
// switches, sliders, progress bars, tabs, combo boxes, menus, dialogs, a list, a button group, a split button, a
// floating action button, a floating toolbar, button sizes, text fields, icon buttons, a top app bar, a navigation
// bar, a snackbar, circular progress, chips, a card, a divider, a date picker, badges, a drawer, a bottom sheet, a
// group box, a text editor and, on a wide screen, a navigation rail, in a scheme made from a seed color, light or
// dark. With Qt, which has no such style, it builds and runs in the default style.
#include <QApplication>
#include <QCalendarWidget>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QStatusBar>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScroller>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTime>
#include <QTimeEdit>
#include <QToolBar>
#include <QToolButton>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QWidget>
#if __has_include(<QMaterialStyle>)
#include <QMaterialStyle>
#define HAVE_MATERIAL_STYLE
#endif
// Google's icons (Apache 2.0, not MIT), in a package of Nt's own: with it linked, the gallery shows icon buttons.
#if __has_include(<QMaterialSymbols>)
#include <QMaterialSymbols>
#define HAVE_MATERIAL_SYMBOLS
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

// On a screen that is wide, and high enough for it, the navigation is a rail at the side; otherwise it is a bar at the
// bottom.
class NavigationPlace : public QObject {
public:
    explicit NavigationPlace(QTabWidget *tabs) : QObject(tabs), m_tabs(tabs) {}
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize) {
            const QSize size = static_cast<QResizeEvent *>(event)->size();
            const bool rail = size.width() > size.height() && size.height() >= 480;
            // Very wide, the rail is the expanded one, with the labels beside the icons.
            const bool expanded = rail && size.width() >= 1200;
            m_tabs->tabBar()->setProperty("materialTabBar", expanded ? QStringLiteral("navigationRailExpanded")
                                                            : rail   ? QStringLiteral("navigationRail")
                                                                     : QStringLiteral("navigationBar"));
            m_tabs->setTabPosition(rail ? QTabWidget::West : QTabWidget::South);
        }
        return false;
    }

private:
    QTabWidget *m_tabs;
};

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

    QMainWindow window;
    // A top app bar: the window's title, and actions after it.
    auto *appBar = new QToolBar;
    appBar->setProperty("materialToolBar", QStringLiteral("appBar"));
    appBar->setWindowTitle(QStringLiteral("Material 3"));
#ifdef HAVE_MATERIAL_SYMBOLS
    QAction *menuAction =
        appBar->addAction(QMaterialSymbols::icon(QStringLiteral("menu")), QStringLiteral("Menu"));
    appBar->addSeparator(); // the title goes here
    QAction *search =
        appBar->addAction(QMaterialSymbols::icon(QStringLiteral("search")), QStringLiteral("Search"));
    QAction *overflow =
        appBar->addAction(QMaterialSymbols::icon(QStringLiteral("more_vert")), QStringLiteral("More"));
#endif
    window.addToolBar(appBar);
    // The pages, and a bottom navigation bar to go between them.
    auto *tabs = new QTabWidget;
    tabs->setTabPosition(QTabWidget::South);
    tabs->tabBar()->setProperty("materialTabBar", QStringLiteral("navigationBar"));
    window.setCentralWidget(tabs);
    window.installEventFilter(new NavigationPlace(tabs));
    // The status bar as a snackbar, which floats above the navigation bar while it has a message.
    auto *snackbar = new QStatusBar;
    snackbar->setProperty("materialStatusBar", QStringLiteral("snackbar"));
    auto *undo = new QPushButton(QStringLiteral("Undo"));
    snackbar->addPermanentWidget(undo);
    window.setStatusBar(snackbar);
    QObject::connect(undo, &QPushButton::clicked, snackbar, &QStatusBar::clearMessage);
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
    edit->setProperty("materialLabel", QStringLiteral("Outlined text field"));
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

    // A divider, chips and a card.
    auto *line = new QFrame;
    line->setFrameStyle(static_cast<int>(QFrame::HLine) | QFrame::Sunken);
    layout->addWidget(line);
    auto *chips = new QHBoxLayout;
    const auto chip = [chips](const char *kind, const QString &text) {
        auto *b = button(text, nullptr);
        b->setProperty("materialChip", QString::fromLatin1(kind));
        chips->addWidget(b);
        return b;
    };
    auto *assist = chip("assist", QStringLiteral("Share"));
#ifdef HAVE_MATERIAL_SYMBOLS
    assist->setIcon(QMaterialSymbols::icon(QStringLiteral("share")));
#else
    Q_UNUSED(assist)
#endif
    chip("filter", QStringLiteral("Unread"))->setCheckable(true);
    auto *entered = chip("input", QStringLiteral("Hazel"));
    QObject::connect(entered, &QPushButton::clicked, entered, &QWidget::hide);
    chips->addStretch(1);
    layout->addLayout(chips);
    auto *card = new QFrame;
    card->setProperty("materialCard", QStringLiteral("filled"));
    auto *contents = new QVBoxLayout(card);
    contents->setContentsMargins(16, 12, 16, 12);
    contents->addWidget(new QLabel(QStringLiteral("A filled card")));
    contents->addWidget(new QLabel(QStringLiteral("Its layout holds what is in it.")));
    layout->addWidget(card);
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
    // The circular indicator, busy, and part of the way, which the slider sets.
    auto *spinning = new QProgressBar;
    spinning->setProperty("materialProgress", QStringLiteral("circular"));
    spinning->setRange(0, 0);
    indicators->addWidget(spinning);
    auto *ring = new QProgressBar;
    ring->setProperty("materialProgress", QStringLiteral("circular"));
    ring->setTextVisible(false);
    ring->setValue(slider->value());
    indicators->addWidget(ring);
    QObject::connect(slider, &QSlider::valueChanged, ring, &QProgressBar::setValue);
    indicators->addStretch(1);
    page->addLayout(indicators);
    page->addStretch(1);
    // A floating toolbar, with choices that stay checked. It would go over the content of an editor.
    auto *format = new QToolBar;
    format->setProperty("materialToolBar", QStringLiteral("floating"));
#ifdef HAVE_MATERIAL_SYMBOLS
    for (const char *name : {"format_bold", "format_italic", "format_underlined"})
        format->addAction(QMaterialSymbols::icon(QString::fromLatin1(name)), QString::fromLatin1(name))
            ->setCheckable(true);
#else
    for (const char *name : {"B", "I", "U"})
        format->addAction(QString::fromLatin1(name))->setCheckable(true);
#endif
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
    // A calendar in a dialog: Material's date picker. Without week numbers it fits a phone.
    auto *when = button(QStringLiteral("Pick a date"), "tonal");
    column->addWidget(when);
    QObject::connect(when, &QPushButton::clicked, [when] {
        QDialog dialog(when->window());
        auto *rows = new QVBoxLayout(&dialog);
        rows->setContentsMargins(16, 16, 16, 16);
        auto *calendar = new QCalendarWidget;
        calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        calendar->setProperty("materialDatePicker", QStringLiteral("modal")); // with the headline of the chosen date
        calendar->setSelectedDate(when->property("date").isValid() ? when->property("date").toDate()
                                                                   : QDate::currentDate());
        rows->addWidget(calendar);
        auto *answers = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        rows->addWidget(answers);
        QObject::connect(answers, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(answers, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            when->setProperty("date", calendar->selectedDate());
            when->setText(calendar->selectedDate().toString(Qt::ISODate));
        }
    });
    // A QTimeEdit as Material's time input, in a dialog.
    auto *clock = button(QStringLiteral("Pick a time"), "tonal");
    column->addWidget(clock);
    QObject::connect(clock, &QPushButton::clicked, [clock] {
        QDialog dialog(clock->window());
        auto *rows = new QVBoxLayout(&dialog);
        rows->setContentsMargins(24, 24, 24, 16);
        rows->setSpacing(16);
        rows->addWidget(new QLabel(QStringLiteral("Enter time")));
        auto *time = new QTimeEdit(clock->property("time").isValid() ? clock->property("time").toTime()
                                                                     : QTime::currentTime());
        time->setDisplayFormat(QStringLiteral("HH:mm"));
        time->setProperty("materialTimeInput", true);
        rows->addWidget(time, 0, Qt::AlignHCenter);
        auto *answers = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        rows->addWidget(answers);
        QObject::connect(answers, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(answers, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            clock->setProperty("time", time->time());
            clock->setText(time->time().toString(QStringLiteral("HH:mm")));
        }
    });
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
    // A filled field with a line under it, and one that says what is wrong while it is not a number.
    auto *name = new QLineEdit;
    name->setProperty("materialTextField", QStringLiteral("filled"));
    name->setProperty("materialLabel", QStringLiteral("Name"));
    name->setProperty("materialSupportingText", QStringLiteral("As it is to be shown"));
    name->setMaxLength(30);
    name->setProperty("materialCounter", true); // characters typed, out of 30
#ifdef HAVE_MATERIAL_SYMBOLS
    name->addAction(QMaterialSymbols::icon(QStringLiteral("person")), QLineEdit::LeadingPosition);
#endif
    column->addWidget(name);
    auto *weight = new QLineEdit;
    weight->setProperty("materialLabel", QStringLiteral("Weight in grams"));
    weight->setProperty("materialError", QString()); // keeps the room for it
    weight->setProperty("materialSuffix", QStringLiteral("g"));
    weight->setInputMethodHints(Qt::ImhDigitsOnly);
    column->addWidget(weight);
    // A spin box: an outlined field with a button each way.
    auto *howMany = new QSpinBox;
    howMany->setRange(0, 99);
    howMany->setSuffix(QStringLiteral(" nuts"));
    column->addWidget(howMany);
    QObject::connect(weight, &QLineEdit::textChanged, [weight](const QString &text) {
        bool number = false;
        text.toDouble(&number);
        weight->setProperty("materialError",
                            text.isEmpty() || number ? QString() : QStringLiteral("A weight is a number"));
    });
    int sent = 0;
    QObject::connect(send, &QToolButton::clicked, [&sent, send, snackbar] {
        send->setText(QStringLiteral("Sent %1").arg(++sent));
        snackbar->showMessage(QStringLiteral("Message sent"), 6000);
    });
    QObject::connect(ask, &QPushButton::clicked, [&window] {
        auto *box = new QMessageBox(&window);
        box->setWindowTitle(QStringLiteral("Delete note"));
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
    tabs->addTab(popups, QStringLiteral("Input"));

    // A list that scrolls under a finger.
    auto *list = new QListWidget;
    list->setFrameShape(QFrame::NoFrame);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    const char *const nuts[] = {"Acorn", "Almond",    "Beechnut", "Brazil nut", "Candlenut", "Cashew",
                                "Chestnut", "Filbert", "Ginkgo",   "Hazelnut",   "Hickory",   "Kola nut",
                                "Macadamia", "Pecan",  "Pine nut", "Pistachio",  "Walnut"};
    // Two lines each: a line break in the text puts what follows under the name, in the Material style.
    for (const char *nut : nuts)
        list->addItem(QString::fromLatin1(nut) + QStringLiteral("\nA nut, or so it is called"));
    QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
    // Over the list, a search bar that filters it, and secondary tabs: all the nuts, or those named "nut".
    auto *lists = new QWidget;
    auto *over = new QVBoxLayout(lists);
    over->setContentsMargins(0, 8, 0, 0);
    over->setSpacing(4);
    auto *find = new QLineEdit;
    find->setProperty("materialTextField", QStringLiteral("search"));
    find->setPlaceholderText(QStringLiteral("Search the nuts"));
    find->setClearButtonEnabled(true);
    auto *beside = new QHBoxLayout;
    beside->setContentsMargins(16, 0, 16, 0);
    beside->addWidget(find);
    over->addLayout(beside);
    auto *which = new QTabBar;
    which->setProperty("materialTabBar", QStringLiteral("secondary"));
    which->setExpanding(true);
    which->addTab(QStringLiteral("All"));
    which->addTab(QStringLiteral("Called nut"));
    over->addWidget(which);
    over->addWidget(list, 1);
    const auto filter = [list, find, which] {
        for (int i = 0; i < list->count(); ++i) {
            const QString name = list->item(i)->text().section(QLatin1Char('\n'), 0, 0);
            list->item(i)->setHidden(
                !name.contains(find->text(), Qt::CaseInsensitive) ||
                (which->currentIndex() == 1 && !name.contains(QStringLiteral("nut"), Qt::CaseInsensitive)));
        }
    };
    QObject::connect(find, &QLineEdit::textChanged, list, filter);
    QObject::connect(which, &QTabBar::currentChanged, list, filter);
    tabs->addTab(lists, QStringLiteral("Lists"));

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
#ifdef HAVE_MATERIAL_SYMBOLS
    // Icon buttons of the four kinds; the heart stays checked.
    auto *icons = new QHBoxLayout;
    icons->setSpacing(8);
    const std::pair<const char *, const char *> kinds[] = {
        {"standard", "search"}, {"filled", "add"}, {"tonal", "edit"}, {"outlined", "share"}, {"filled", "favorite"}};
    for (const auto &[kind, symbol] : kinds) {
        auto *icon = new QToolButton;
        icon->setProperty("materialIconButton", QString::fromLatin1(kind));
        // Without a color: the style gives it that of the button, and it is drawn at the size it is shown at.
        const bool heart = QLatin1String(symbol) == QLatin1String("favorite");
        QIcon picture = QMaterialSymbols::icon(QString::fromLatin1(symbol), QColor(), heart);
        icon->setIcon(picture);
        icon->setCheckable(heart);
        icons->addWidget(icon);
    }
    icons->addStretch(1);
    rows->addLayout(icons);
#endif
    rows->addStretch(1);
    tabs->addTab(sizes, QStringLiteral("Sizes"));

#ifdef HAVE_MATERIAL_SYMBOLS
    const char *const destinations[] = {"home", "settings", "edit", "folder", "star"};
    for (int i = 0; i < tabs->count() && i < 5; ++i)
        tabs->setTabIcon(i, QMaterialSymbols::icon(QString::fromLatin1(destinations[i])));
#endif

#ifdef HAVE_MATERIAL_SYMBOLS
    // The menu button opens a navigation drawer with the pages, and the search button a bottom sheet.
    QObject::connect(menuAction, &QAction::triggered, [&window, tabs] {
        QDialog drawer(&window);
        drawer.setProperty("materialSheet", QStringLiteral("drawer"));
        auto *rows = new QVBoxLayout(&drawer);
        rows->setContentsMargins(0, 16, 0, 16);
        auto *heading = new QLabel(QStringLiteral("Pages"));
        heading->setContentsMargins(28, 0, 28, 8);
        rows->addWidget(heading);
        auto *list = new QListWidget;
        list->setProperty("materialList", QStringLiteral("navigationDrawer"));
        list->setIconSize(QSize(24, 24));
        for (int i = 0; i < tabs->count(); ++i)
            list->addItem(new QListWidgetItem(tabs->tabIcon(i), tabs->tabText(i)));
        list->setCurrentRow(tabs->currentIndex());
        rows->addWidget(list);
        QObject::connect(list, &QListWidget::itemClicked, [&drawer, tabs, list](QListWidgetItem *item) {
            tabs->setCurrentIndex(list->row(item));
            drawer.accept();
        });
        drawer.exec();
    });
    QObject::connect(search, &QAction::triggered, [&window] {
        QDialog sheet(&window);
        sheet.setProperty("materialSheet", QStringLiteral("bottom"));
        auto *rows = new QVBoxLayout(&sheet);
        rows->setContentsMargins(24, 24, 24, 24);
        rows->setSpacing(16);
        rows->addWidget(new QLabel(QStringLiteral("A bottom sheet. Tap beside it or press Back to close it.")));
        auto *what = new QLineEdit;
        what->setProperty("materialLabel", QStringLiteral("Search for"));
        rows->addWidget(what);
        auto *go = button(QStringLiteral("Search"), "filled");
        rows->addWidget(go);
        QObject::connect(go, &QPushButton::clicked, &sheet, &QDialog::accept);
        sheet.exec();
    });
#endif

#ifdef HAVE_MATERIAL_SYMBOLS
    // The three dots open the overflow menu, under them.
    QObject::connect(overflow, &QAction::triggered, [&window, appBar, overflow, tabs, snackbar] {
        QWidget *dots = appBar->widgetForAction(overflow);
        if (!dots)
            return;
        auto *more = new QMenu(&window);
        QObject::connect(more, &QMenu::aboutToHide, more, &QObject::deleteLater);
        QObject::connect(more->addAction(QStringLiteral("Clear badges")), &QAction::triggered, [tabs, dots] {
            tabs->tabBar()->setProperty("materialBadges", QVariantList());
            dots->setProperty("materialBadge", false);
        });
        QObject::connect(more->addAction(QStringLiteral("Show a message")), &QAction::triggered,
                         [snackbar] { snackbar->showMessage(QStringLiteral("From the overflow menu"), 4000); });
        // A form with a checkable group box, which is a titled card, and a text editor of several lines.
        QObject::connect(more->addAction(QStringLiteral("Feedback")), &QAction::triggered, [&window] {
            QDialog dialog(&window);
            dialog.setWindowTitle(QStringLiteral("Feedback"));
            auto *rows = new QVBoxLayout(&dialog);
            rows->setContentsMargins(24, 24, 24, 16);
            rows->setSpacing(16);
            auto *reply = new QGroupBox(QStringLiteral("Send me a reply"));
            reply->setCheckable(true);
            auto *choices = new QVBoxLayout(reply);
            auto *byMail = new QRadioButton(QStringLiteral("By mail"));
            byMail->setChecked(true);
            choices->addWidget(byMail);
            choices->addWidget(new QRadioButton(QStringLiteral("By phone")));
            rows->addWidget(reply);
            auto *text = new QPlainTextEdit;
            text->setPlaceholderText(QStringLiteral("What should be better?"));
            text->setMinimumHeight(120);
            rows->addWidget(text);
            auto *answers = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            rows->addWidget(answers);
            QObject::connect(answers, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            QObject::connect(answers, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialog.exec();
        });
        QObject::connect(more->addAction(QStringLiteral("About")), &QAction::triggered, [&window] {
            QMessageBox::information(&window, QStringLiteral("Material 3"),
                                     QStringLiteral("The Material style of Nt, in a Qt Widgets application."));
        });
        more->popup(dots->mapToGlobal(QPoint(dots->width() - more->sizeHint().width(), dots->height())));
    });
#endif

    // Badges: a dot on More, a count on Lists, and a dot on the app bar's last button.
    tabs->tabBar()->setProperty("materialBadges", QVariantList{false, true, false, 3});
#ifdef HAVE_MATERIAL_SYMBOLS
    if (QWidget *overflowButton = appBar->widgetForAction(overflow))
        overflowButton->setProperty("materialBadge", true);
#endif
    window.show();
    return app.exec();
}
