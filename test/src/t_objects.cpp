#include "report.h"
#include "tests.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariant>

// Kept at file scope (not in an anonymous namespace) so that the registered
// meta type name is simply "Point3D".
struct Point3D
{
    double x = 0;
    double y = 0;
    double z = 0;

    bool operator==(const Point3D &other) const
    {
        return qFuzzyCompare(x, other.x) && qFuzzyCompare(y, other.y)
                && qFuzzyCompare(z, other.z);
    }
};

Q_DECLARE_METATYPE(Point3D)

namespace {

class TestObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QString label MEMBER m_label)

public:
    enum Color { Red, Green, Blue };
    Q_ENUM(Color)

    using QObject::QObject;

    int value() const { return m_value; }
    void setValue(int v)
    {
        if (v == m_value)
            return;
        m_value = v;
        emit valueChanged(v);
    }

    Q_INVOKABLE int tripled(int v) const { return v * 3; }

public slots:
    void increment() { setValue(m_value + 1); }

signals:
    void valueChanged(int value);
    void forwarded(int value);

private:
    int m_value = 0;
    QString m_label;
};

QtMsgType g_capturedType = QtDebugMsg;
QString g_capturedMessage;

void captureHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    g_capturedType = type;
    g_capturedMessage = message;
}

} // namespace

void testObjects()
{
    report::section(QStringLiteral("QObject, meta-object system, event loop and QProcess"));

    // --- meta-object introspection -----------------------------------------
    {
        TestObject object;
        const QMetaObject *meta = object.metaObject();
        report::info(QStringLiteral("class name"), QString::fromLatin1(meta->className()));
        report::info(QStringLiteral("property count (incl. inherited)"),
                     QString::number(meta->propertyCount()));
        report::check(QStringLiteral("moc generated a working meta-object"),
                      QLatin1StringView(meta->className()) == QLatin1StringView("TestObject")
                              && meta->propertyCount() > 2);
        report::check(QStringLiteral("superclass chain is intact"),
                      meta->superClass() != nullptr
                              && QLatin1StringView(meta->superClass()->className())
                                      == QLatin1StringView("QObject"));
        report::check(QStringLiteral("QObject::inherits works"),
                      object.inherits("QObject"));

        // Q_PROPERTY read/write through the meta-object
        object.setProperty("value", 12);
        report::check(QStringLiteral("Q_PROPERTY read/write via QObject::property"),
                      object.property("value").toInt() == 12 && object.value() == 12);
        object.setProperty("label", QStringLiteral("member-property"));
        report::check(QStringLiteral("MEMBER based Q_PROPERTY"),
                      object.property("label").toString() == QStringLiteral("member-property"));

        const int index = meta->indexOfProperty("value");
        const QMetaProperty property = meta->property(index);
        report::check(QStringLiteral("QMetaProperty exposes the NOTIFY signal"),
                      property.hasNotifySignal()
                              && QLatin1StringView(property.notifySignal().name())
                                      == QLatin1StringView("valueChanged"),
                      QString::fromLatin1(property.notifySignal().name()));
        report::check(QStringLiteral("QMetaProperty type information"),
                      property.metaType().id() == QMetaType::Int,
                      QString::fromLatin1(property.typeName()));

        object.setProperty("dynamicProp", QStringLiteral("dyn"));
        report::check(QStringLiteral("dynamic properties"),
                      object.dynamicPropertyNames().contains(QByteArrayLiteral("dynamicProp"))
                              && object.property("dynamicProp").toString()
                                      == QStringLiteral("dyn"));

        // Q_INVOKABLE + Q_ENUM
        int result = 0;
        report::check(QStringLiteral("QMetaObject::invokeMethod with return value"),
                      QMetaObject::invokeMethod(&object, "tripled", Qt::DirectConnection,
                                                Q_RETURN_ARG(int, result), Q_ARG(int, 14))
                              && result == 42,
                      QString::number(result));

        const QMetaEnum colorEnum = QMetaEnum::fromType<TestObject::Color>();
        report::check(QStringLiteral("Q_ENUM registration"),
                      colorEnum.isValid() && colorEnum.keyCount() == 3
                              && QLatin1StringView(colorEnum.valueToKey(TestObject::Green))
                                      == QLatin1StringView("Green")
                              && colorEnum.keyToValue("Blue") == TestObject::Blue);
    }

    // --- signals and slots --------------------------------------------------
    {
        TestObject sender;
        TestObject receiver;
        int lambdaCalls = 0;
        int lastValue = -1;

        QObject::connect(&sender, &TestObject::valueChanged, &receiver,
                         [&](int v) { ++lambdaCalls; lastValue = v; });
        const auto memberConnection =
                QObject::connect(&sender, &TestObject::valueChanged, &receiver,
                                 &TestObject::setValue);
        QObject::connect(&sender, &TestObject::valueChanged, &sender, &TestObject::forwarded);

        int forwardedCalls = 0;
        QObject::connect(&sender, &TestObject::forwarded, &sender,
                         [&forwardedCalls](int) { ++forwardedCalls; });

        sender.setValue(5);
        report::check(QStringLiteral("connect to lambda"), lambdaCalls == 1 && lastValue == 5);
        report::check(QStringLiteral("connect to member slot"), receiver.value() == 5);
        report::check(QStringLiteral("signal-to-signal connection"), forwardedCalls == 1);

        sender.setValue(5); // no change -> no signal
        report::check(QStringLiteral("no signal when the value does not change"),
                      lambdaCalls == 1);

        QObject::disconnect(memberConnection);
        sender.setValue(9);
        report::check(QStringLiteral("disconnect stops delivery"),
                      receiver.value() == 5 && lambdaCalls == 2);

        // old-style string based connection
        TestObject stringSender;
        TestObject stringReceiver;
        report::check(QStringLiteral("string based connect (SIGNAL/SLOT macros)"),
                      static_cast<bool>(QObject::connect(&stringSender, SIGNAL(valueChanged(int)),
                                                         &stringReceiver, SLOT(increment()))));
        stringSender.setValue(1);
        report::check(QStringLiteral("string based connection delivers"),
                      stringReceiver.value() == 1);
    }

    // --- object trees and lifetime -----------------------------------------
    {
        auto *root = new TestObject;
        root->setObjectName(QStringLiteral("root"));
        auto *childA = new TestObject(root);
        childA->setObjectName(QStringLiteral("childA"));
        auto *childB = new TestObject(root);
        childB->setObjectName(QStringLiteral("childB"));

        report::check(QStringLiteral("parent/child relationship"),
                      root->children().size() == 2 && childA->parent() == root);
        report::check(QStringLiteral("findChild by name"),
                      root->findChild<TestObject *>(QStringLiteral("childB")) == childB);
        report::check(QStringLiteral("findChildren"),
                      root->findChildren<TestObject *>().size() == 2);

        QPointer<TestObject> guard(childA);
        delete root;
        report::check(QStringLiteral("children are deleted with the parent (QPointer cleared)"),
                      guard.isNull());
    }

    // --- QVariant and QMetaType ---------------------------------------------
    {
        const int pointTypeId = qRegisterMetaType<Point3D>();
        report::info(QStringLiteral("custom meta type id"), QString::number(pointTypeId));
        report::info(QStringLiteral("meta type name"),
                     QString::fromLatin1(QMetaType::fromType<Point3D>().name()));
        report::check(QStringLiteral("custom type registration"),
                      QMetaType::fromType<Point3D>().isValid() && pointTypeId > 0);
        report::soft(QStringLiteral("custom type is findable by name"),
                     QMetaType::fromName("Point3D").isValid(),
                     QString::fromLatin1(QMetaType::fromType<Point3D>().name()));

        const Point3D point{ 1.0, 2.0, 3.0 };
        const QVariant variant = QVariant::fromValue(point);
        report::check(QStringLiteral("QVariant stores and returns custom types"),
                      variant.value<Point3D>() == point);

        const QVariant number(42);
        report::check(QStringLiteral("QVariant conversions"),
                      number.canConvert<QString>() && number.toString() == QStringLiteral("42")
                              && QVariant(QStringLiteral("3.5")).toDouble() == 3.5);
        report::check(QStringLiteral("QVariant of containers"),
                      QVariant::fromValue(QStringList{ QStringLiteral("a") })
                              .toStringList().size() == 1);
        report::check(QStringLiteral("QMetaType names for template types"),
                      QLatin1StringView(QMetaType::fromType<QList<int>>().name())
                              == QLatin1StringView("QList<int>"),
                      QString::fromLatin1(QMetaType::fromType<QList<int>>().name()));
    }

    // --- event loop, timers, queued invocation ------------------------------
    {
        QEventLoop loop;
        int timerTicks = 0;
        QTimer repeating;
        repeating.setInterval(10);
        QObject::connect(&repeating, &QTimer::timeout, &loop, [&] {
            if (++timerTicks == 3) {
                repeating.stop();
                loop.quit();
            }
        });
        repeating.start();
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        report::check(QStringLiteral("QTimer fires repeatedly in an event loop"), timerTicks == 3,
                      QStringLiteral("%1 ticks").arg(timerTicks));

        bool singleShotFired = false;
        QEventLoop loop2;
        QTimer::singleShot(1, &loop2, [&] { singleShotFired = true; loop2.quit(); });
        loop2.exec();
        report::check(QStringLiteral("QTimer::singleShot with a lambda"), singleShotFired);

        TestObject target;
        QEventLoop loop3;
        QMetaObject::invokeMethod(&target, "increment", Qt::QueuedConnection);
        QMetaObject::invokeMethod(&loop3, "quit", Qt::QueuedConnection);
        loop3.exec();
        report::check(QStringLiteral("queued invokeMethod is dispatched by the event loop"),
                      target.value() == 1);

        auto *deferred = new TestObject;
        QPointer<TestObject> guard(deferred);
        deferred->deleteLater();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        report::check(QStringLiteral("deleteLater is processed by the event loop"),
                      guard.isNull());
    }

    // --- logging ------------------------------------------------------------
    {
        QtMessageHandler previous = qInstallMessageHandler(captureHandler);
        qWarning("static build warning %d", 7);
        qInstallMessageHandler(previous);
        report::check(QStringLiteral("custom message handler receives messages"),
                      g_capturedType == QtWarningMsg
                              && g_capturedMessage == QStringLiteral("static build warning 7"),
                      g_capturedMessage);

        QLoggingCategory category("test.category");
        QLoggingCategory::setFilterRules(QStringLiteral("test.category.debug=false\n"
                                                        "test.category.warning=true"));
        report::check(QStringLiteral("QLoggingCategory filter rules"),
                      !category.isDebugEnabled() && category.isWarningEnabled());
        QLoggingCategory::setFilterRules(QString());
    }

    // --- QCommandLineParser -------------------------------------------------
    {
        QCommandLineParser parser;
        parser.addHelpOption();
        QCommandLineOption verbose(QStringList{ QStringLiteral("v"), QStringLiteral("verbose") },
                                   QStringLiteral("Verbose output."));
        QCommandLineOption count(QStringList{ QStringLiteral("n"), QStringLiteral("count") },
                                 QStringLiteral("Number of runs."), QStringLiteral("count"),
                                 QStringLiteral("1"));
        parser.addOption(verbose);
        parser.addOption(count);
        parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Input file."));

        const QStringList args{ QStringLiteral("app"), QStringLiteral("--verbose"),
                                QStringLiteral("-n"), QStringLiteral("5"),
                                QStringLiteral("input.txt") };
        report::check(QStringLiteral("QCommandLineParser parses arguments"), parser.parse(args),
                      parser.errorText());
        report::check(QStringLiteral("options and positional arguments"),
                      parser.isSet(verbose) && parser.value(count) == QStringLiteral("5")
                              && parser.positionalArguments()
                                      == QStringList{ QStringLiteral("input.txt") });
    }

    // --- QProcess (re-executes this binary in helper mode) ------------------
    {
#ifdef QT_NO_PROCESS
        report::skip(QStringLiteral("QProcess"), QStringLiteral("built without process support"));
#else
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(),
                      QStringList{ QStringLiteral("--child-echo") });
        if (report::check(QStringLiteral("QProcess starts a child process"),
                          process.waitForStarted(10000), process.errorString())) {
            const bool finished = process.waitForFinished(15000);
            const QByteArray output = process.readAllStandardOutput();
            report::info(QStringLiteral("child stdout"),
                         report::ellipsize(QString::fromUtf8(output)));
            report::check(QStringLiteral("child process finished"), finished,
                          process.errorString());
            report::check(QStringLiteral("child stdout is captured"),
                          output.contains(QByteArrayLiteral("hello from child")));
            report::check(QStringLiteral("child exit code is propagated"),
                          process.exitStatus() == QProcess::NormalExit && process.exitCode() == 42,
                          QStringLiteral("exit code %1").arg(process.exitCode()));
        }
#endif
    }
}

#include "t_objects.moc"