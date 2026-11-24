// MIT License - see LICENSE
//
// A Qt 6 application with no Android-specific code: a counter, a text field and a list. The same source runs on the
// desktop and, built with Nt, on an Android device.
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("Hello from Nt"));
    auto *layout = new QVBoxLayout(&window);

    auto *title = new QLabel(QStringLiteral("Qt 6 on Android, with Nt"));
    QFont font = title->font();
    font.setPointSize(font.pointSize() + 6);
    title->setFont(font);
    layout->addWidget(title);

    auto *counter = new QLabel(QStringLiteral("Taps: 0"));
    layout->addWidget(counter);
    auto *button = new QPushButton(QStringLiteral("Tap me"));
    button->setMinimumHeight(96);
    layout->addWidget(button);
    int taps = 0;
    QObject::connect(button, &QPushButton::clicked, [&] { counter->setText(QStringLiteral("Taps: %1").arg(++taps)); });

    auto *edit = new QLineEdit;
    edit->setPlaceholderText(QStringLiteral("Type a note and press Enter"));
    layout->addWidget(edit);
    auto *row = new QHBoxLayout;
    auto *copy = new QPushButton(QStringLiteral("Copy"));
    auto *paste = new QPushButton(QStringLiteral("Paste"));
    row->addWidget(copy);
    row->addWidget(paste);
    layout->addLayout(row);
    QObject::connect(copy, &QPushButton::clicked, [&] { QApplication::clipboard()->setText(edit->text()); });
    QObject::connect(paste, &QPushButton::clicked, [&] { edit->insert(QApplication::clipboard()->text()); });
    auto *notes = new QListWidget;
    layout->addWidget(notes, 1);
    QObject::connect(edit, &QLineEdit::returnPressed, [&] {
        if (!edit->text().isEmpty())
            notes->addItem(edit->text());
        edit->clear();
    });

    window.show();
    return app.exec();
}
