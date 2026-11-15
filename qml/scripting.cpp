// MIT License - see LICENSE
//
// Scripting an application with QJSEngine: evaluating code, exchanging values, and letting scripts use a
// QObject's properties, methods and signals.
//
//   js_scripting                 runs the examples and prints what they give
//   js_scripting script.js       runs a file, with the "counter" object available to it
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>

#include <cstdio>

// A counter that scripts drive.
class Counter : public QObject {
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
public:
    int value() const { return m_value; }
    void setValue(int v) {
        if (v == m_value)
            return;
        m_value = v;
        emit valueChanged(v);
    }
    Q_INVOKABLE void add(int amount) { setValue(m_value + amount); }

signals:
    void valueChanged(int value) { emitSignal(&Counter::valueChanged, value); }

private:
    int m_value = 0;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    QJSEngine engine;
    engine.installExtensions(QJSEngine::ConsoleExtension);

    QJSValue result = engine.evaluate("[1, 2, 3].map(x => x * x).join(' + ')");
    qDebug() << result.toString(); // "1 + 4 + 9"

    QJSValue square = engine.evaluate("(function(x) { return x * x; })");
    qDebug() << square.call({12}).toInt(); // 144

    QJSValue error = engine.evaluate("undefinedFunction()", "inline.js");
    if (error.isError())
        qDebug() << "line" << error.property("lineNumber").toInt() << error.toString();

    Counter counter;
    engine.globalObject().setProperty("counter", engine.newQObject(&counter));
    engine.evaluate(R"(
        counter.valueChanged.connect(v => console.log("counter is now", v));
        counter.value = 10;
        counter.add(5);
    )");
    qDebug() << counter.value(); // 15

    if (argc > 1) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "cannot read %s\n", argv[1]);
            return 1;
        }
        const QJSValue r = engine.evaluate(QString::fromUtf8(file.readAll()), file.fileName());
        if (r.isError()) {
            std::fprintf(stderr, "%s\n", r.toString().toUtf8().constData());
            return 1;
        }
        if (!r.isUndefined())
            std::printf("%s\n", r.toString().toUtf8().constData());
    }
    return 0;
}

#include "scripting.moc"
