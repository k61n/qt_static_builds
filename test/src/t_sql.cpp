#include "report.h"
#include "tests.h"

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlField>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QVariant>

void testSql()
{
    report::section(QStringLiteral("SQL drivers (statically linked plugins)"));

    const QStringList drivers = QSqlDatabase::drivers();
    report::info(QStringLiteral("available SQL drivers"),
                 drivers.isEmpty() ? QStringLiteral("(none)") : drivers.join(QStringLiteral(", ")));
    report::check(QStringLiteral("at least one SQL driver plugin is linked in"), !drivers.isEmpty(),
                  QStringLiteral("no drivers registered - static plugin import failed"));

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        report::skip(QStringLiteral("SQLite tests"),
                     QStringLiteral("QSQLITE driver is not available"));
        return;
    }

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("test_connection"));
        db.setDatabaseName(QStringLiteral(":memory:"));
        if (report::check(QStringLiteral("in-memory SQLite database opens"), db.open(),
                          db.lastError().text())) {
            report::info(QStringLiteral("driver name"), db.driverName());
            report::info(QStringLiteral("transactions supported"),
                         db.driver()->hasFeature(QSqlDriver::Transactions));
            report::info(QStringLiteral("prepared queries supported"),
                         db.driver()->hasFeature(QSqlDriver::PreparedQueries));
            report::info(QStringLiteral("Unicode supported"),
                         db.driver()->hasFeature(QSqlDriver::Unicode));

            QSqlQuery query(db);
            report::check(QStringLiteral("CREATE TABLE"),
                          query.exec(QStringLiteral("CREATE TABLE person ("
                                                    "id INTEGER PRIMARY KEY, "
                                                    "name TEXT NOT NULL, "
                                                    "score REAL)")),
                          query.lastError().text());

            report::check(QStringLiteral("prepared statement preparation"),
                          query.prepare(QStringLiteral("INSERT INTO person (name, score) "
                                                       "VALUES (:name, :score)")),
                          query.lastError().text());
            bool insertsOk = true;
            const QList<QPair<QString, double>> rows = {
                { QString::fromUtf8("Grüße"), 1.5 },
                { QStringLiteral("Bob"), 2.5 },
                { QString::fromUtf8("日本語"), 3.5 },
            };
            for (const auto &row : rows) {
                query.bindValue(QStringLiteral(":name"), row.first);
                query.bindValue(QStringLiteral(":score"), row.second);
                insertsOk = query.exec() && insertsOk;
            }
            report::check(QStringLiteral("named placeholder binding and INSERT"), insertsOk,
                          query.lastError().text());

            report::check(QStringLiteral("SELECT with aggregate"),
                          query.exec(QStringLiteral("SELECT COUNT(*), SUM(score) FROM person"))
                                  && query.next() && query.value(0).toInt() == 3
                                  && qFuzzyCompare(query.value(1).toDouble(), 7.5),
                          query.lastError().text());

            report::check(QStringLiteral("ordered SELECT and QSqlRecord metadata"),
                          query.exec(QStringLiteral("SELECT id, name, score FROM person "
                                                    "ORDER BY score DESC")),
                          query.lastError().text());
            const QSqlRecord record = query.record();
            report::info(QStringLiteral("result columns"),
                         QStringLiteral("%1: %2, %3, %4").arg(record.count())
                                 .arg(record.fieldName(0), record.fieldName(1),
                                      record.fieldName(2)));
            QStringList names;
            while (query.next())
                names << query.value(QStringLiteral("name")).toString();
            report::check(QStringLiteral("non-ASCII text survives the round trip"),
                          names.size() == 3 && names.first() == QString::fromUtf8("日本語"),
                          names.join(QStringLiteral(", ")));

            // transactions
            if (db.driver()->hasFeature(QSqlDriver::Transactions)) {
                report::check(QStringLiteral("BEGIN transaction"), db.transaction());
                query.exec(QStringLiteral("INSERT INTO person (name, score) VALUES ('Temp', 9)"));
                report::check(QStringLiteral("ROLLBACK transaction"), db.rollback());
                query.exec(QStringLiteral("SELECT COUNT(*) FROM person"));
                query.next();
                report::check(QStringLiteral("rolled back row is gone"),
                              query.value(0).toInt() == 3,
                              QString::number(query.value(0).toInt()));
            }

            // error reporting
            QSqlQuery bad(db);
            const bool failed = !bad.exec(QStringLiteral("SELECT * FROM does_not_exist"));
            report::check(QStringLiteral("SQL errors are reported"),
                          failed && bad.lastError().type() != QSqlError::NoError);
            report::info(QStringLiteral("sample SQL error"),
                         report::ellipsize(bad.lastError().text()));
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("test_connection"));
}