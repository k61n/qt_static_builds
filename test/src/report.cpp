#include "report.h"

#include <QStringList>
#include <QTextStream>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

int g_passed = 0;
int g_failed = 0;
int g_warned = 0;
int g_skipped = 0;
QStringList g_failures;
QStringList g_warnings;

QString line(const QString &tag, const QString &name, const QString &detail)
{
    QString s = QStringLiteral("  [%1] %2").arg(tag, name);
    if (!detail.isEmpty())
        s += QStringLiteral("  -- ") + detail;
    return s;
}

} // namespace

namespace report {

void section(const QString &title)
{
    out() << Qt::endl
          << QStringLiteral("==== ") << title << QStringLiteral(" ")
          << QString(qMax(4, 68 - title.size()), QLatin1Char('='))
          << Qt::endl;
    out().flush();
}

void info(const QString &key, const QString &value)
{
    out() << QStringLiteral("  ") << key.leftJustified(32, QLatin1Char('.'))
          << QStringLiteral(": ") << value << Qt::endl;
    out().flush();
}

void info(const QString &key, bool value)
{
    info(key, yesNo(value));
}

void note(const QString &text)
{
    out() << QStringLiteral("  ") << text << Qt::endl;
    out().flush();
}

void listItem(const QString &text)
{
    out() << QStringLiteral("      - ") << text << Qt::endl;
    out().flush();
}

bool check(const QString &name, bool ok, const QString &detail)
{
    if (ok) {
        ++g_passed;
        out() << line(QStringLiteral(" OK "), name, QString()) << Qt::endl;
    } else {
        ++g_failed;
        g_failures << name;
        out() << line(QStringLiteral("FAIL"), name, detail) << Qt::endl;
    }
    out().flush();
    return ok;
}

bool soft(const QString &name, bool ok, const QString &detail)
{
    if (ok) {
        ++g_passed;
        out() << line(QStringLiteral(" OK "), name, QString()) << Qt::endl;
    } else {
        ++g_warned;
        g_warnings << name;
        out() << line(QStringLiteral("WARN"), name, detail) << Qt::endl;
    }
    out().flush();
    return ok;
}

void skip(const QString &name, const QString &reason)
{
    ++g_skipped;
    out() << line(QStringLiteral("SKIP"), name, reason) << Qt::endl;
    out().flush();
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

QString ellipsize(const QString &text, int maxLength)
{
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (flat.size() <= maxLength)
        return flat;
    return flat.left(maxLength - 3) + QStringLiteral("...");
}

int failedCount()
{
    return g_failed;
}

void summary()
{
    section(QStringLiteral("Summary"));
    info(QStringLiteral("passed"), QString::number(g_passed));
    info(QStringLiteral("failed"), QString::number(g_failed));
    info(QStringLiteral("warnings"), QString::number(g_warned));
    info(QStringLiteral("skipped"), QString::number(g_skipped));

    if (!g_warnings.isEmpty()) {
        note(QStringLiteral("Warnings:"));
        for (const QString &w : std::as_const(g_warnings))
            listItem(w);
    }
    if (!g_failures.isEmpty()) {
        note(QStringLiteral("Failures:"));
        for (const QString &f : std::as_const(g_failures))
            listItem(f);
    }

    out() << Qt::endl
          << (g_failed == 0 ? QStringLiteral("RESULT: PASS")
                            : QStringLiteral("RESULT: FAIL"))
          << Qt::endl;
    out().flush();
}

} // namespace report