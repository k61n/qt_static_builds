#include "report.h"
#include "tests.h"

#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QLocale>
#include <QTime>
#include <QTimeZone>

void testDateTime()
{
    report::section(QStringLiteral("Date, time, time zones and locales"));

    // --- plain date/time arithmetic ----------------------------------------
    const QDate date(2026, 7, 27);
    const QTime time(14, 30, 15, 250);
    const QDateTime utc(date, time, QTimeZone::UTC);

    report::info(QStringLiteral("reference date/time (UTC)"),
                 utc.toString(Qt::ISODateWithMs));
    report::info(QStringLiteral("current date/time (local)"),
                 QDateTime::currentDateTime().toString(Qt::ISODate));
    report::info(QStringLiteral("seconds since epoch"), QString::number(utc.toSecsSinceEpoch()));

    report::check(QStringLiteral("QDate arithmetic"),
                  date.addDays(5) == QDate(2026, 8, 1) && date.addMonths(6) == QDate(2027, 1, 27));
    report::check(QStringLiteral("QDate day-of-week / leap year"),
                  date.dayOfWeek() == 1 && QDate(2024, 2, 29).isValid()
                          && !QDate(2026, 2, 29).isValid());
    report::check(QStringLiteral("ISO 8601 round trip"),
                  QDateTime::fromString(utc.toString(Qt::ISODateWithMs), Qt::ISODateWithMs) == utc);
    report::check(QStringLiteral("epoch conversion round trip (ms precision)"),
                  QDateTime::fromMSecsSinceEpoch(utc.toMSecsSinceEpoch(), QTimeZone::UTC) == utc);
    report::check(QStringLiteral("epoch conversion round trip (second precision)"),
                  QDateTime::fromSecsSinceEpoch(utc.toSecsSinceEpoch(), QTimeZone::UTC)
                          == utc.addMSecs(-utc.time().msec()));
    report::check(QStringLiteral("QDateTime::secsTo"),
                  utc.secsTo(utc.addSecs(3600)) == 3600);
    report::check(QStringLiteral("custom format string"),
                  utc.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
                          == QStringLiteral("2026-07-27 14:30:15"));

    // --- time zone database -------------------------------------------------
    const QList<QByteArray> zoneIds = QTimeZone::availableTimeZoneIds();
    report::info(QStringLiteral("available time zone IDs"), QString::number(zoneIds.size()));
    report::info(QStringLiteral("system time zone"),
                 QString::fromLatin1(QTimeZone::systemTimeZoneId()));
    report::check(QStringLiteral("time zone database is populated"), zoneIds.size() > 100,
                  QStringLiteral("only %1 IDs available").arg(zoneIds.size()));

    QTimeZone berlin(QByteArrayLiteral("Europe/Berlin"));
    if (report::check(QStringLiteral("named IANA zone Europe/Berlin resolves"), berlin.isValid())) {
        const QDateTime winter(QDate(2026, 1, 15), QTime(12, 0), QTimeZone::UTC);
        const QDateTime summer(QDate(2026, 7, 15), QTime(12, 0), QTimeZone::UTC);
        const int winterOffset = berlin.offsetFromUtc(winter);
        const int summerOffset = berlin.offsetFromUtc(summer);
        report::info(QStringLiteral("Europe/Berlin offsets"),
                     QStringLiteral("January %1 s, July %2 s").arg(winterOffset).arg(summerOffset));
        report::info(QStringLiteral("Europe/Berlin display name"),
                     berlin.displayName(winter, QTimeZone::LongName, QLocale(QLocale::English)));
        report::check(QStringLiteral("DST transitions (CET/CEST offsets)"),
                      winterOffset == 3600 && summerOffset == 7200,
                      QStringLiteral("got %1 / %2").arg(winterOffset).arg(summerOffset));
        report::check(QStringLiteral("DST flag is reported"),
                      !berlin.isDaylightTime(winter) && berlin.isDaylightTime(summer));

        const QDateTime local = utc.toTimeZone(berlin);
        report::check(QStringLiteral("QDateTime zone conversion"),
                      local.toUTC() == utc && local.timeSpec() == Qt::TimeZone);
        report::info(QStringLiteral("reference time in Berlin"), local.toString(Qt::ISODate));
    }

    QTimeZone offsetZone = QTimeZone::fromSecondsAheadOfUtc(5 * 3600 + 1800);
    report::check(QStringLiteral("fixed offset time zone (+05:30)"),
                  offsetZone.isValid() && offsetZone.offsetFromUtc(utc) == 19800);

    // --- locales ------------------------------------------------------------
    const QLocale german(QLocale::German, QLocale::Germany);
    const QLocale cLocale = QLocale::c();
    report::info(QStringLiteral("system locale"), QLocale::system().name());
    report::info(QStringLiteral("system UI languages"),
                 QLocale::system().uiLanguages().join(QStringLiteral(", ")));
    report::info(QStringLiteral("de_DE number"), german.toString(1234567.891, 'f', 2));
    report::info(QStringLiteral("de_DE currency"), german.toCurrencyString(1234.5));
    report::info(QStringLiteral("de_DE long date"),
                 german.toString(date, QLocale::LongFormat));
    report::info(QStringLiteral("de_DE month name"), german.monthName(1));

    report::check(QStringLiteral("locale name round trip"),
                  german.name() == QStringLiteral("de_DE"), german.name());
    report::check(QStringLiteral("locale specific decimal separator"),
                  german.decimalPoint() == QStringLiteral(",")
                          && cLocale.decimalPoint() == QStringLiteral("."),
                  german.decimalPoint());
    report::check(QStringLiteral("locale aware number parsing"),
                  qFuzzyCompare(german.toDouble(QStringLiteral("1.234,50")), 1234.5));
    report::check(QStringLiteral("localised month names"),
                  german.monthName(1) == QStringLiteral("Januar"), german.monthName(1));
    report::check(QStringLiteral("localised date parsing (long format)"),
                  german.toDate(german.toString(date, QLocale::LongFormat),
                                QLocale::LongFormat) == date,
                  german.toString(date, QLocale::LongFormat));
    // Short format uses a two digit year, which Qt maps to the 20th century.
    const QString shortDate = german.toString(date, QLocale::ShortFormat);
    const QDate parsedShort = german.toDate(shortDate, QLocale::ShortFormat);
    report::info(QStringLiteral("de_DE short date"),
                 QStringLiteral("%1 -> %2").arg(shortDate, parsedShort.toString(Qt::ISODate)));
    report::check(QStringLiteral("localised date parsing (short format, 2-digit year)"),
                  parsedShort.day() == date.day() && parsedShort.month() == date.month(),
                  parsedShort.toString(Qt::ISODate));
    report::soft(QStringLiteral("CLDR data for a less common locale (ja_JP)"),
                 QLocale(QLocale::Japanese, QLocale::Japan).monthName(1)
                         == QString::fromUtf8("1月"),
                 QLocale(QLocale::Japanese, QLocale::Japan).monthName(1));

    // --- monotonic clocks ---------------------------------------------------
    QElapsedTimer elapsed;
    elapsed.start();
    volatile double sink = 0;
    for (int i = 0; i < 200000; ++i)
        sink += i * 0.5;
    Q_UNUSED(sink)
    report::info(QStringLiteral("QElapsedTimer clock type"), QString::number(int(elapsed.clockType())));
    report::check(QStringLiteral("QElapsedTimer measures elapsed time"),
                  elapsed.isValid() && elapsed.nsecsElapsed() > 0,
                  QStringLiteral("%1 ns").arg(elapsed.nsecsElapsed()));
}