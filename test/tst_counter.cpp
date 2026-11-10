// MIT License - see LICENSE
//! [test]
#include <QSignalSpy>
#include <QTest>

class Counter : public QObject {
    Q_OBJECT
public:
    int value() const { return m_value; }
    void setValue(int value) {
        if (value == m_value)
            return;
        m_value = value;
        emit valueChanged(value);
    }

signals:
    void valueChanged(int value);

private:
    int m_value = 0;
};

class TestCounter : public QObject {
    Q_OBJECT

private slots:
    void init() { m_counter.setValue(0); }

    void startsAtZero() { QCOMPARE(Counter().value(), 0); }

    void emitsOnChange() {
        QSignalSpy spy(&m_counter, &Counter::valueChanged);
        m_counter.setValue(5);
        m_counter.setValue(5);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 5);
    }

    void toHex_data() {
        QTest::addColumn<int>("value");
        QTest::addColumn<QByteArray>("hex");

        QTest::newRow("zero") << 0 << QByteArray("0");
        QTest::newRow("byte") << 255 << QByteArray("ff");
    }

    void toHex() {
        QFETCH(int, value);
        QFETCH(QByteArray, hex);
        QCOMPARE(QByteArray::number(value, 16), hex);
    }

private:
    Counter m_counter;
};

QTEST_MAIN(TestCounter)
#include "tst_counter.moc"
//! [test]
