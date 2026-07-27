#include "report.h"
#include "tests.h"

#include <QBuffer>
#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

void testSerialization()
{
    report::section(QStringLiteral("Serialization: JSON, CBOR, XML, QDataStream, QSettings"));

    // --- JSON ---------------------------------------------------------------
    {
        const QByteArray json = R"({
            "name": "static build",
            "version": 6,
            "features": ["core", "network", "sql"],
            "nested": { "enabled": true, "ratio": 0.25 },
            "unicode": "Grüße 日本語"
        })";

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
        report::check(QStringLiteral("QJsonDocument parses UTF-8 input"),
                      error.error == QJsonParseError::NoError && doc.isObject(),
                      error.errorString());

        const QJsonObject obj = doc.object();
        report::check(QStringLiteral("JSON value access and types"),
                      obj.value(QStringLiteral("name")).toString() == QStringLiteral("static build")
                              && obj.value(QStringLiteral("version")).toInt() == 6
                              && obj.value(QStringLiteral("features")).toArray().size() == 3
                              && obj.value(QStringLiteral("nested")).toObject()
                                         .value(QStringLiteral("enabled")).toBool());
        report::check(QStringLiteral("JSON keeps non-ASCII text intact"),
                      obj.value(QStringLiteral("unicode")).toString()
                              == QString::fromUtf8("Grüße 日本語"));
        report::check(QStringLiteral("JSON compact/indented round trip"),
                      QJsonDocument::fromJson(doc.toJson(QJsonDocument::Compact)) == doc
                              && QJsonDocument::fromJson(doc.toJson(QJsonDocument::Indented))
                                         == doc);

        QJsonParseError badError;
        QJsonDocument::fromJson(QByteArrayLiteral("{ broken: }"), &badError);
        report::check(QStringLiteral("malformed JSON is reported"),
                      badError.error != QJsonParseError::NoError, badError.errorString());
        report::info(QStringLiteral("JSON error message"), badError.errorString());
    }

    // --- CBOR ---------------------------------------------------------------
    {
        QCborMap map;
        map.insert(QStringLiteral("id"), 1234);
        map.insert(QStringLiteral("tags"), QCborArray{ QStringLiteral("a"), QStringLiteral("b") });
        map.insert(QStringLiteral("blob"), QCborValue(QByteArrayLiteral("\x00\x01\x02")));

        const QByteArray encoded = QCborValue(map).toCbor();
        const QCborValue decoded = QCborValue::fromCbor(encoded);
        report::check(QStringLiteral("QCborValue binary round trip"),
                      decoded.isMap() && decoded.toMap() == map);
        report::check(QStringLiteral("CBOR is more compact than JSON"),
                      encoded.size() < QJsonDocument(map.toJsonObject())
                                               .toJson(QJsonDocument::Compact).size(),
                      QStringLiteral("cbor=%1 bytes").arg(encoded.size()));
        report::check(QStringLiteral("CBOR to JSON conversion"),
                      map.toJsonObject().value(QStringLiteral("id")).toInt() == 1234);
    }

    // --- XML stream reader/writer ------------------------------------------
    {
        QByteArray xml;
        QXmlStreamWriter writer(&xml);
        writer.setAutoFormatting(true);
        writer.writeStartDocument();
        writer.writeStartElement(QStringLiteral("library"));
        writer.writeAttribute(QStringLiteral("count"), QStringLiteral("2"));
        for (int i = 1; i <= 2; ++i) {
            writer.writeStartElement(QStringLiteral("book"));
            writer.writeAttribute(QStringLiteral("id"), QString::number(i));
            writer.writeTextElement(QStringLiteral("title"),
                                    QString::fromUtf8("Bücher %1").arg(i));
            writer.writeEndElement();
        }
        writer.writeEndElement();
        writer.writeEndDocument();
        report::check(QStringLiteral("QXmlStreamWriter produced output"), !xml.isEmpty());

        QXmlStreamReader reader(xml);
        int books = 0;
        QString firstTitle;
        while (!reader.atEnd()) {
            if (reader.readNextStartElement()) {
                if (reader.name() == QStringLiteral("book"))
                    ++books;
                else if (reader.name() == QStringLiteral("title") && firstTitle.isEmpty())
                    firstTitle = reader.readElementText();
            }
        }
        report::check(QStringLiteral("QXmlStreamReader round trip"),
                      !reader.hasError() && books == 2
                              && firstTitle == QString::fromUtf8("Bücher 1"),
                      reader.errorString());

        QXmlStreamReader broken(QByteArrayLiteral("<a><b></a>"));
        while (!broken.atEnd())
            broken.readNext();
        report::check(QStringLiteral("malformed XML is reported"), broken.hasError(),
                      broken.errorString());
    }

    // --- QDataStream --------------------------------------------------------
    {
        QByteArray buffer;
        const QDateTime now = QDateTime::currentDateTime();
        const QStringList list = { QStringLiteral("one"), QString::fromUtf8("zwö") };
        const QVariantMap variantMap = { { QStringLiteral("k"), QVariant(42) },
                                         { QStringLiteral("d"), QVariant(1.5) } };
        {
            QDataStream out(&buffer, QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_0);
            out << QStringLiteral("header") << 42 << 3.5 << list << now << variantMap;
            report::check(QStringLiteral("QDataStream write status"),
                          out.status() == QDataStream::Ok);
        }
        {
            QDataStream in(buffer);
            in.setVersion(QDataStream::Qt_6_0);
            QString header;
            int number = 0;
            double real = 0;
            QStringList readList;
            QDateTime readTime;
            QVariantMap readMap;
            in >> header >> number >> real >> readList >> readTime >> readMap;
            report::check(QStringLiteral("QDataStream round trip (incl. QVariant)"),
                          in.status() == QDataStream::Ok && header == QStringLiteral("header")
                                  && number == 42 && qFuzzyCompare(real, 3.5) && readList == list
                                  && readTime == now && readMap == variantMap);
        }
        {
            QDataStream truncated(buffer.left(3));
            QString header;
            int number = 0;
            truncated >> header >> number;
            report::check(QStringLiteral("truncated stream sets an error status"),
                          truncated.status() != QDataStream::Ok);
        }
    }

    // --- QBuffer / QIODevice ------------------------------------------------
    {
        QByteArray storage;
        QBuffer buffer(&storage);
        report::check(QStringLiteral("QBuffer open for read/write"),
                      buffer.open(QIODevice::ReadWrite));
        buffer.write(QByteArrayLiteral("line one\nline two\n"));
        buffer.seek(0);
        const QByteArray firstLine = buffer.readLine();
        report::check(QStringLiteral("QIODevice line based reading"),
                      firstLine == QByteArrayLiteral("line one\n") && buffer.pos() == 9,
                      QString::fromLatin1(firstLine));
    }

    // --- QSettings ----------------------------------------------------------
    {
        QTemporaryDir dir;
        if (report::check(QStringLiteral("QTemporaryDir for settings"), dir.isValid(),
                          dir.errorString())) {
            const QString path = dir.filePath(QStringLiteral("config.ini"));
            {
                QSettings settings(path, QSettings::IniFormat);
                settings.setValue(QStringLiteral("general/name"), QString::fromUtf8("Grüße"));
                settings.setValue(QStringLiteral("general/count"), 7);
                settings.beginGroup(QStringLiteral("list"));
                settings.setValue(QStringLiteral("items"),
                                  QStringList{ QStringLiteral("a"), QStringLiteral("b") });
                settings.endGroup();
                settings.sync();
                report::check(QStringLiteral("QSettings sync status"),
                              settings.status() == QSettings::NoError);
            }
            QSettings reopened(path, QSettings::IniFormat);
            report::check(QStringLiteral("QSettings INI round trip"),
                          reopened.value(QStringLiteral("general/name")).toString()
                                          == QString::fromUtf8("Grüße")
                                  && reopened.value(QStringLiteral("general/count")).toInt() == 7
                                  && reopened.value(QStringLiteral("list/items")).toStringList()
                                             .size() == 2);
            report::check(QStringLiteral("QSettings file written to disk"),
                          QFileInfo::exists(path));
            report::info(QStringLiteral("QSettings groups"),
                         reopened.childGroups().join(QStringLiteral(", ")));
        }
    }

    // --- misc value types ---------------------------------------------------
    {
        const QUuid uuid = QUuid::createUuid();
        report::info(QStringLiteral("generated UUID"), uuid.toString());
        report::check(QStringLiteral("QUuid round trip"),
                      QUuid(uuid.toString()) == uuid && !uuid.isNull());

        const quint32 bounded = QRandomGenerator::global()->bounded(10u, 20u);
        report::check(QStringLiteral("QRandomGenerator bounded range"),
                      bounded >= 10 && bounded < 20, QString::number(bounded));
        QRandomGenerator secure = QRandomGenerator::securelySeeded();
        report::check(QStringLiteral("QRandomGenerator::securelySeeded produces values"),
                      secure.generate() != secure.generate());

        const QVersionNumber version = QVersionNumber::fromString(QString::fromLatin1(qVersion()));
        report::check(QStringLiteral("QVersionNumber parsing/comparison"),
                      version >= QVersionNumber(6, 0, 0), version.toString());
    }
}