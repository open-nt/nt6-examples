// MIT License - see LICENSE
//
// The Nt logo, and one of every widget on the pages of a tab widget. The first page, Nt, is a showcase of all the
// widgets that Nt has, each one live. Run it with a display, or without one: `hello_widgets -platform offscreen`.
// A number as an argument opens that page first: `hello_widgets 3`.
//
// The logo is nt-logo.svg, which hello_widgets.qrc compiles into the program as the resource :/nt-logo.svg. A
// QSvgWidget from Nt SVG shows it.
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLCDNumber>
#include <QLabel>
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
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSvgWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

//! [form]
// A label and a widget side by side, with the labels of all rows of a page the same width.
static QHBoxLayout *row(const QString &text, QWidget *widget, QWidget *page, int labelWidth = 90) {
    auto *label = new QLabel(text, page);
    label->setFixedWidth(labelWidth);
    auto *layout = new QHBoxLayout;
    layout->addWidget(label);
    layout->addWidget(widget, 1);
    return layout;
}

// The widgets of Nt, one row each: the name of the class and a live example. Everything reports to the status line.
static QWidget *showcasePage(QLabel *status) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(5);
    const int names = 104;

    auto *label = new QLabel("Plain text, nothing to edit.", page);
    layout->addLayout(row("QLabel", label, page, names));

    auto *buttons = new QWidget(page);
    auto *buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    auto *push = new QPushButton("Click me", buttons);
    push->setToolTip("A push button: click it");
    auto *toggle = new QPushButton("Toggle", buttons);
    toggle->setCheckable(true);
    auto *flat = new QPushButton("Flat", buttons);
    flat->setFlat(true);
    buttonsLayout->addWidget(push);
    buttonsLayout->addWidget(toggle);
    buttonsLayout->addWidget(flat);
    buttonsLayout->addStretch();
    layout->addLayout(row("QPushButton", buttons, page, names));
    int clicks = 0;
    QObject::connect(push, &QPushButton::clicked,
                     [=]() mutable { status->setText(QString("Clicked %1 times.").arg(++clicks)); });
    QObject::connect(toggle, &QPushButton::toggled,
                     [=](bool on) { status->setText(on ? "Toggled on." : "Toggled off."); });

    auto *check = new QCheckBox("Check me", page);
    layout->addLayout(row("QCheckBox", check, page, names));
    QObject::connect(check, &QCheckBox::toggled, [=](bool on) { status->setText(on ? "Checked." : "Unchecked."); });

    // Radio buttons with the same parent are exclusive, so they get a widget of their own.
    auto *radios = new QWidget(page);
    auto *radiosLayout = new QHBoxLayout(radios);
    radiosLayout->setContentsMargins(0, 0, 0, 0);
    auto *yes = new QRadioButton("Yes", radios);
    auto *no = new QRadioButton("No", radios);
    yes->setChecked(true);
    radiosLayout->addWidget(yes);
    radiosLayout->addWidget(no);
    radiosLayout->addStretch();
    layout->addLayout(row("QRadioButton", radios, page, names));
    for (QRadioButton *choice : {yes, no})
        QObject::connect(choice, &QRadioButton::toggled, [=](bool on) {
            if (on)
                status->setText("Chose " + choice->text() + ".");
        });

    auto *edit = new QLineEdit(page);
    edit->setPlaceholderText("Type here");
    edit->setToolTip("A line edit: type some text");
    layout->addLayout(row("QLineEdit", edit, page, names));
    QObject::connect(edit, &QLineEdit::textChanged, [=](const QString &text) { status->setText("Text: " + text); });

    auto *spin = new QSpinBox(page);
    spin->setRange(0, 100);
    spin->setValue(35);
    spin->setSuffix(" %");
    layout->addLayout(row("QSpinBox", spin, page, names));

    auto *combo = new QComboBox(page);
    combo->addItems({"Teal", "Orange", "Green", "Purple"});
    layout->addLayout(row("QComboBox", combo, page, names));
    QObject::connect(combo, &QComboBox::currentIndexChanged,
                     [=](int) { status->setText("Chose " + combo->currentText() + "."); });

    auto *slider = new QSlider(Qt::Horizontal, page);
    slider->setRange(0, 100);
    slider->setValue(35);
    layout->addLayout(row("QSlider", slider, page, names));

    auto *bar = new QProgressBar(page);
    bar->setValue(35);
    layout->addLayout(row("QProgressBar", bar, page, names));
    // The spin box, the slider and the progress bar show one value; setValue() does nothing for the same value.
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(slider, &QSlider::valueChanged, bar, &QProgressBar::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);

    auto *scrollBar = new QScrollBar(Qt::Horizontal, page);
    scrollBar->setRange(0, 100);
    scrollBar->setPageStep(25);
    scrollBar->setValue(20);
    layout->addLayout(row("QScrollBar", scrollBar, page, names));
    QObject::connect(scrollBar, &QScrollBar::valueChanged,
                     [=](int value) { status->setText(QString("Scrolled to %1.").arg(value)); });

    auto *frames = new QWidget(page);
    auto *framesLayout = new QHBoxLayout(frames);
    framesLayout->setContentsMargins(0, 0, 0, 0);
    for (int style : {QFrame::Box | QFrame::Plain, QFrame::Panel | QFrame::Raised, QFrame::Panel | QFrame::Sunken,
                      QFrame::StyledPanel | QFrame::Sunken}) {
        auto *frame = new QFrame(frames);
        frame->setFrameStyle(style);
        frame->setLineWidth(2);
        frame->setFixedSize(46, 20);
        framesLayout->addWidget(frame);
    }
    framesLayout->addStretch();
    layout->addLayout(row("QFrame", frames, page, names));

    auto *tip = new QLabel("Rest the mouse here for a tool tip.", page);
    tip->setToolTip("A tool tip, from QWidget::setToolTip()");
    layout->addLayout(row("QToolTip", tip, page, names));

    auto *group = new QGroupBox("Checkable", page);
    group->setCheckable(true);
    auto *groupLayout = new QHBoxLayout(group);
    groupLayout->addWidget(new QLabel("Unchecking disables what is inside.", group));
    layout->addLayout(row("QGroupBox", group, page, names));

    auto *tabs = new QTabWidget(page);
    for (const char *name : {"Tabs", "Pages"}) {
        auto *inner = new QWidget;
        auto *innerLayout = new QVBoxLayout(inner);
        innerLayout->addWidget(new QLabel(QString("The page \"%1\".").arg(name), inner));
        tabs->addTab(inner, name);
    }
    tabs->setFixedHeight(70);
    layout->addLayout(row("QTabWidget", tabs, page, names));

    // A table that is small enough to leave the rest of the page in view: the rows are low and it has no row headers.
    auto *table = new QTableWidget(3, 3, page);
    table->setHorizontalHeaderLabels({"Name", "Role", "Status"});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    const char *staff[][3] = {{"Ada", "Engineer", "Active"}, {"Grace", "Admiral", "Active"}, {"Linus", "Maintainer", "Away"}};
    for (int r = 0; r < 3; ++r) {
        table->setRowHeight(r, 22);
        for (int c = 0; c < 3; ++c)
            table->setItem(r, c, new QTableWidgetItem(staff[r][c]));
    }
    table->setColumnWidth(0, 90);
    table->setColumnWidth(1, 100);
    table->setFixedHeight(table->horizontalHeader()->sizeHint().height() + 3 * 22 + 4);
    layout->addLayout(row("QTableWidget", table, page, names));
    QObject::connect(table, &QTableWidget::cellClicked, [=](int r, int c) {
        status->setText(QString("Cell %1, %2: %3").arg(r).arg(c).arg(table->item(r, c)->text()));
    });

    auto *area = new QScrollArea(page);
    auto *areaPage = new QWidget;
    auto *areaLayout = new QVBoxLayout(areaPage);
    for (int i = 1; i <= 6; ++i)
        areaLayout->addWidget(new QLabel(QString("Line %1 of a page that scrolls").arg(i), areaPage));
    area->setWidget(areaPage);
    area->setWidgetResizable(true);
    area->setFixedHeight(56);
    layout->addLayout(row("QScrollArea", area, page, names));
    layout->addStretch();
    return page;
}

// Buttons, check boxes and radio buttons.
static QWidget *buttonsPage(QLabel *status) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *buttons = new QHBoxLayout;
    auto *button = new QPushButton("Click me", page);
    auto *toggle = new QPushButton("Toggle", page);
    toggle->setCheckable(true);
    auto *flat = new QPushButton("Flat", page);
    flat->setFlat(true);
    buttons->addWidget(button);
    buttons->addWidget(toggle);
    buttons->addWidget(flat);
    buttons->addSpacing(12);
    auto *tool = new QToolButton(page);
    tool->setText("Tool");
    tool->setCheckable(true);
    buttons->addWidget(tool);
    for (Qt::ArrowType type : {Qt::LeftArrow, Qt::RightArrow}) {
        auto *arrow = new QToolButton(page);
        arrow->setArrowType(type);
        arrow->setAutoRaise(true);
        buttons->addWidget(arrow);
        QObject::connect(arrow, &QToolButton::clicked, [=] {
            status->setText(type == Qt::LeftArrow ? "Tool button: left." : "Tool button: right.");
        });
    }
    buttons->addStretch();
    layout->addLayout(buttons);

    auto *clicks = new QLabel("The button was clicked 0 times.", page);
    auto *shout = new QCheckBox("Shout", page);
    auto *enabled = new QCheckBox("Toggle button enabled", page);
    enabled->setChecked(true);
    layout->addWidget(clicks);
    layout->addWidget(shout);
    layout->addWidget(enabled);

    auto *group = new QGroupBox("Size", page);
    auto *groupLayout = new QHBoxLayout(group);
    auto *small = new QRadioButton("Small", group);
    auto *medium = new QRadioButton("Medium", group);
    auto *large = new QRadioButton("Large", group);
    medium->setChecked(true);
    groupLayout->addWidget(small);
    groupLayout->addWidget(medium);
    groupLayout->addWidget(large);
    groupLayout->addStretch();
    layout->addWidget(group);

    // A stacked widget shows the page of the chosen size, and only that one.
    auto *details = new QStackedWidget(page);
    const char *sizes[] = {"Small: a size that is easy to carry.", "Medium: the size that most people take.",
                           "Large: a size with room for everything."};
    for (const char *text : sizes)
        details->addWidget(new QLabel(text, details));
    details->setCurrentIndex(1);
    details->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    layout->addWidget(details);
    int index = 0;
    for (QRadioButton *choice : {small, medium, large}) {
        const int stackIndex = index++;
        QObject::connect(choice, &QRadioButton::toggled, [=](bool on) {
            if (on)
                details->setCurrentIndex(stackIndex);
        });
    }
    layout->addStretch();

    int count = 0;
    QObject::connect(button, &QPushButton::clicked, [=]() mutable {
        const QString text = QString("The button was clicked %1 times.").arg(++count);
        clicks->setText(shout->isChecked() ? text.toUpper() : text);
    });
    QObject::connect(enabled, &QCheckBox::toggled, toggle, &QPushButton::setEnabled);
    QObject::connect(toggle, &QPushButton::toggled,
                     [=](bool on) { status->setText(on ? "Toggled on." : "Toggled off."); });
    for (QRadioButton *choice : {small, medium, large})
        QObject::connect(choice, &QRadioButton::toggled, [=](bool on) {
            if (on)
                status->setText("Size: " + choice->text());
        });
    return page;
}

// Text and numbers to type, and a list to choose from.
static QWidget *inputPage(QLabel *status) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *name = new QLineEdit(page);
    name->setPlaceholderText("Your name");
    auto *password = new QLineEdit(page);
    password->setEchoMode(QLineEdit::Password);
    password->setText("secret");
    auto *age = new QSpinBox(page);
    age->setRange(0, 120);
    age->setValue(30);
    age->setSuffix(" years");
    auto *color = new QComboBox(page);
    color->addItems({"Teal", "Orange", "Green", "Purple", "Yellow", "Pink"});
    auto *greeting = new QLabel("Type a name.", page);

    // A QFormLayout lines up the fields, and the labels in front of them.
    auto *form = new QFormLayout;
    form->addRow("Name", name);
    form->addRow("Password", password);
    form->addRow("Age", age);
    auto *notes = new QPlainTextEdit(page);
    notes->setPlaceholderText("Notes of many lines");
    notes->setFixedHeight(70);
    form->addRow("Notes", notes);
    form->addRow("Color", color);
    layout->addLayout(form);
    layout->addSpacing(6);
    layout->addWidget(greeting);
    layout->addStretch();

    QObject::connect(name, &QLineEdit::textChanged, [=](const QString &text) {
        greeting->setText(text.isEmpty() ? "Type a name." : "Hello, " + text + "!");
    });
    QObject::connect(name, &QLineEdit::returnPressed, [=] { status->setText("Name: " + name->text()); });
    QObject::connect(notes, &QPlainTextEdit::textChanged,
                     [=] { status->setText(QString("Notes: %1 lines.").arg(notes->blockCount())); });
    QObject::connect(age, &QSpinBox::valueChanged, [=](int value) { status->setText(QString("Age: %1").arg(value)); });
    QObject::connect(color, &QComboBox::currentIndexChanged,
                     [=](int) { status->setText("Color: " + color->currentText()); });
    return page;
}

// A slider, a vertical slider and a progress bar that show the same value.
static QWidget *valuesPage(QLabel *status) {
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    auto *left = new QVBoxLayout;

    auto *slider = new QSlider(Qt::Horizontal, page);
    slider->setRange(0, 100);
    slider->setValue(35);
    auto *bar = new QProgressBar(page);
    bar->setValue(35);
    auto *spin = new QSpinBox(page);
    spin->setRange(0, 100);
    spin->setValue(35);
    spin->setSuffix(" %");
    auto *hint = new QLabel("Drag a slider, click next to its handle, or use the arrow keys.", page);

    left->addLayout(row("Slider", slider, page));
    left->addLayout(row("Progress", bar, page));
    left->addLayout(row("Spin box", spin, page));
    left->addWidget(hint);

    // A dial and a seven-segment display for the same value.
    auto *dial = new QDial(page);
    dial->setRange(0, 100);
    dial->setValue(35);
    dial->setNotchesVisible(true);
    dial->setMaximumSize(110, 110);
    auto *lcd = new QLCDNumber(3, page);
    lcd->setSegmentStyle(QLCDNumber::Flat);
    lcd->display(35);
    lcd->setMinimumHeight(48);
    auto *meter = new QHBoxLayout;
    meter->addWidget(dial);
    meter->addWidget(lcd, 1);
    left->addLayout(meter);
    left->addStretch();
    auto *tall = new QSlider(Qt::Vertical, page);
    tall->setRange(0, 100);
    tall->setValue(35);
    layout->addLayout(left, 1);
    layout->addWidget(tall);

    // Each control sets the others; setValue() does nothing when the value is the same, so there is no loop.
    for (QSlider *s : {slider, tall})
        QObject::connect(s, &QSlider::valueChanged, [=](int value) {
            slider->setValue(value);
            tall->setValue(value);
            bar->setValue(value);
            spin->setValue(value);
            dial->setValue(value);
            lcd->display(value);
            status->setText(QString("Value: %1").arg(value));
        });
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    QObject::connect(dial, &QDial::valueChanged, slider, &QSlider::setValue);
    return page;
}

// A table of people and a form in a scroll area: the views of data, and a grid layout.
static QWidget *viewsPage(QLabel *status) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *table = new QTableWidget(12, 4, page);
    table->setHorizontalHeaderLabels({"Name", "Role", "Team", "Status"});
    table->setAlternatingRowColors(true);
    const char *names[] = {"Ada", "Grace", "Linus", "Margaret", "Dennis", "Barbara", "Ken", "Donald", "Edsger", "Guido", "Bjarne", "Tim"};
    const char *roles[] = {"Engineer", "Admiral", "Maintainer", "Director", "Researcher", "Professor", "Engineer", "Professor", "Scientist", "Designer", "Architect", "Inventor"};
    const char *teams[] = {"Compiler", "Navy", "Kernel", "Apollo", "Unix", "Systems", "Unix", "Typography", "Algorithms", "Python", "C++", "Web"};
    for (int r = 0; r < 12; ++r) {
        table->setItem(r, 0, new QTableWidgetItem(names[r]));
        table->setItem(r, 1, new QTableWidgetItem(roles[r]));
        table->setItem(r, 2, new QTableWidgetItem(teams[r]));
        auto *state = new QTableWidgetItem(r % 3 == 2 ? "Away" : "Active");
        state->setTextAlignment(Qt::AlignCenter);
        if (r % 3 == 2)
            state->setForeground(QBrush(QColor(190, 70, 20)));
        state->setToolTip(r % 3 == 2 ? "Away: not at the desk" : "Active: at the desk");
        table->setItem(r, 3, state);
    }
    table->item(0, 0)->setToolTip("Rest the mouse on a cell: its tool tip is the item's");
    table->setColumnWidth(0, 100);
    table->setColumnWidth(1, 100);
    table->setColumnWidth(2, 100);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setCurrentCell(2, 1);
    QObject::connect(table, &QTableWidget::currentCellChanged, [=](int r, int c, int, int) {
        if (r >= 0 && table->item(r, c))
            status->setText(QString("%1 (%2, %3)").arg(table->item(r, c)->text()).arg(r).arg(c));
    });
    layout->addWidget(table, 3);

    auto *form = new QWidget;
    auto *grid = new QGridLayout(form);
    for (int i = 0; i < 8; ++i) {
        grid->addWidget(new QLabel(QString("Setting %1").arg(i + 1), form), i, 0);
        auto *edit = new QLineEdit(QString("Value %1").arg(i + 1), form);
        grid->addWidget(edit, i, 1);
    }
    grid->setColumnStretch(1, 1);
    auto *area = new QScrollArea(page);
    area->setWidget(form);
    area->setWidgetResizable(true);

    // A list of the teams, which lets the user select several.
    auto *list = new QListWidget(page);
    list->addItems({"Compiler", "Kernel", "Unix", "Python", "C++", "Web", "Navy", "Apollo"});
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setCurrentRow(1);
    QObject::connect(list, &QListWidget::itemSelectionChanged,
                     [=] { status->setText(QString("%1 teams selected.").arg(list->selectedItems().size())); });
    // A splitter lets the user share the room between the list and the form.
    auto *split = new QSplitter(Qt::Horizontal, page);
    split->addWidget(list);
    split->addWidget(area);
    split->setSizes({120, 300});
    layout->addWidget(split, 2);
    return page;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Hello, widgets");
    // The menu bar goes at the top of the window, as wide as it, and the rest has the margins of a layout.
    auto *outer = new QVBoxLayout(&window);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *menuBar = new QMenuBar(&window);
    outer->addWidget(menuBar);
    auto *content = new QWidget(&window);
    outer->addWidget(content, 1);

    // The header: the logo, the name and what the example is.
    auto *title = new QLabel("Nt Widgets", &window);
    QFont font = title->font();
    font.setPointSize(16);
    font.setBold(true);
    title->setFont(font);
    auto *subtitle = new QLabel("One of every widget, live.", &window);
    auto *words = new QVBoxLayout;
    words->setSpacing(2);
    words->addStretch();
    words->addWidget(title);
    words->addWidget(subtitle);
    words->addStretch();
    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    // The logo is a resource of the program (see hello_widgets.qrc). An SVG document has no pixels of its own, so
    // the widget draws it at whatever size it has, sharp on any screen.
    auto *logo = new QSvgWidget(":/nt-logo.svg", &window);
    logo->setFixedSize(56, 56);
    header->addWidget(logo);
    header->addLayout(words, 1);
    auto *status = new QLabel("Ready.", &window);

    auto *tabs = new QTabWidget(&window);
    tabs->addTab(showcasePage(status), "Nt");
    tabs->addTab(buttonsPage(status), "Buttons");
    tabs->addTab(inputPage(status), "Input");
    tabs->addTab(valuesPage(status), "Values");
    tabs->addTab(viewsPage(status), "Views");
    for (const QString &argument : app.arguments()) {
        bool isNumber = false;
        const int index = argument.toInt(&isNumber);
        if (isNumber)
            tabs->setCurrentIndex(index);
    }

    auto *quit = new QPushButton("Quit", &window);
    QObject::connect(quit, &QPushButton::clicked, &app, &QApplication::quit);
    auto *bar = new QStatusBar(&window);
    bar->setSizeGripEnabled(false);
    bar->addWidget(status, 1);
    bar->addPermanentWidget(quit);

    auto *layout = new QVBoxLayout(content);
    layout->addLayout(header);
    layout->addWidget(tabs, 1);
    layout->addWidget(bar);

    // Actions: each menu entry is one, and a shortcut works without the menu being open.
    QMenu *file = menuBar->addMenu("&File");
    QAction *openAction = file->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    QObject::connect(openAction, &QAction::triggered, [=] { status->setText("Open..."); });
    QAction *saveAction = file->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    QObject::connect(saveAction, &QAction::triggered, [=] { status->setText("Saved."); });
    file->addSeparator();
    QAction *quitAction = file->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    QMenu *view = menuBar->addMenu("&View");
    QAction *showStatus = view->addAction("Status &bar");
    showStatus->setCheckable(true);
    showStatus->setChecked(true);
    QObject::connect(showStatus, &QAction::toggled, bar, &QStatusBar::setVisible);
    QMenu *page = view->addMenu("&Page");
    auto *pages = new QActionGroup(&window);
    for (int i = 0; i < tabs->count(); ++i) {
        QAction *go = page->addAction(tabs->tabText(i));
        go->setCheckable(true);
        go->setChecked(i == tabs->currentIndex());
        pages->addAction(go);
        QObject::connect(go, &QAction::triggered, [=] { tabs->setCurrentIndex(i); });
    }
    QObject::connect(tabs, &QTabWidget::currentChanged, [=](int index) {
        if (index >= 0 && index < pages->actions().size())
            pages->actions().at(index)->setChecked(true);
    });

    QMenu *help = menuBar->addMenu("&Help");
    QObject::connect(help->addAction("&About Nt"), &QAction::triggered,
                     [=] { status->setText("Nt: a small, MIT-licensed implementation of the Qt 6 API."); });

    window.resize(500, 760);
    window.show();
    return app.exec();
}
//! [form]
