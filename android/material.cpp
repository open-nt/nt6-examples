// MIT License - see LICENSE
//
// The Material 3 Expressive style of Nt (QMaterialStyle): buttons, a text field, check boxes, radio buttons and
// switches, in a scheme made from a seed color, light or dark. With Qt, which has no such style, it builds and runs
// in the default style.
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
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
    apply(seed, systemDark);

    QWidget window;
    auto *layout = new QVBoxLayout(&window);
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
    layout->addLayout(seeds);
    layout->addStretch(1);

    window.show();
    return app.exec();
}
