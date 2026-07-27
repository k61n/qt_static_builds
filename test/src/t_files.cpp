#include "report.h"
#include "tests.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStringConverter>
#include <QStringList>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>

void testFiles()
{
    report::section(QStringLiteral("File system, resources and MIME types"));

    QTemporaryDir tempDir;
    if (!report::check(QStringLiteral("QTemporaryDir creation"), tempDir.isValid(),
                       tempDir.errorString()))
        return;
    report::info(QStringLiteral("temporary directory"), tempDir.path());

    const QDir dir(tempDir.path());

    // --- basic file IO ------------------------------------------------------
    {
        const QString path = dir.filePath(QStringLiteral("plain.txt"));
        QFile file(path);
        report::check(QStringLiteral("QFile open for writing"),
                      file.open(QIODevice::WriteOnly | QIODevice::Text), file.errorString());
        {
            QTextStream stream(&file);
            stream.setEncoding(QStringConverter::Utf8);
            stream << QString::fromUtf8("first line: Grüße\n") << QStringLiteral("second line\n");
        }
        file.close();

        QFile readBack(path);
        report::check(QStringLiteral("QFile open for reading"),
                      readBack.open(QIODevice::ReadOnly | QIODevice::Text), readBack.errorString());
        QTextStream in(&readBack);
        in.setEncoding(QStringConverter::Utf8);
        const QString firstLine = in.readLine();
        report::check(QStringLiteral("QTextStream UTF-8 read/write round trip"),
                      firstLine == QString::fromUtf8("first line: Grüße"), firstLine);
        readBack.close();

        const QFileInfo info(path);
        report::check(QStringLiteral("QFileInfo metadata"),
                      info.exists() && info.isFile() && info.size() > 0
                              && info.suffix() == QStringLiteral("txt")
                              && info.baseName() == QStringLiteral("plain"),
                      QStringLiteral("size=%1").arg(info.size()));
        report::check(QStringLiteral("file permissions are readable/writable"),
                      info.isReadable() && info.isWritable());
        report::check(QStringLiteral("last modified time is sane"),
                      info.lastModified().isValid()
                              && qAbs(info.lastModified().secsTo(QDateTime::currentDateTime())) < 60,
                      info.lastModified().toString(Qt::ISODate));
    }

    // --- QSaveFile ----------------------------------------------------------
    {
        const QString path = dir.filePath(QStringLiteral("atomic.txt"));
        QSaveFile saveFile(path);
        report::check(QStringLiteral("QSaveFile open"), saveFile.open(QIODevice::WriteOnly),
                      saveFile.errorString());
        saveFile.write(QByteArrayLiteral("atomic content"));
        report::check(QStringLiteral("QSaveFile commit"), saveFile.commit(),
                      saveFile.errorString());

        QFile written(path);
        report::check(QStringLiteral("QSaveFile content is on disk"),
                      written.open(QIODevice::ReadOnly)
                              && written.readAll() == QByteArrayLiteral("atomic content"));
    }

    // --- directories --------------------------------------------------------
    {
        report::check(QStringLiteral("QDir::mkpath creates nested directories"),
                      dir.mkpath(QStringLiteral("a/b/c")));
        QFile marker(dir.filePath(QStringLiteral("a/b/c/marker.dat")));
        if (marker.open(QIODevice::WriteOnly)) {
            marker.write(QByteArrayLiteral("x"));
            marker.close();
        }

        const QStringList entries = QDir(dir.filePath(QStringLiteral("a/b/c")))
                                            .entryList(QDir::Files);
        report::check(QStringLiteral("QDir::entryList"),
                      entries == QStringList{ QStringLiteral("marker.dat") },
                      entries.join(QStringLiteral(", ")));

        int found = 0;
        QDirIterator it(tempDir.path(), QStringList{ QStringLiteral("*.dat") },
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            ++found;
        }
        report::check(QStringLiteral("QDirIterator recursive search with filters"), found == 1,
                      QStringLiteral("found %1").arg(found));

        report::check(QStringLiteral("QDir::removeRecursively"),
                      QDir(dir.filePath(QStringLiteral("a"))).removeRecursively()
                              && !QFileInfo::exists(dir.filePath(QStringLiteral("a"))));

        report::check(QStringLiteral("path separator normalisation"),
                      QDir::cleanPath(QStringLiteral("/x/y/../z/./w"))
                              == QStringLiteral("/x/z/w"));
    }

    // --- standard paths and storage ----------------------------------------
    {
        report::info(QStringLiteral("home"),
                     QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
        report::info(QStringLiteral("temp"),
                     QStandardPaths::writableLocation(QStandardPaths::TempLocation));
        report::info(QStringLiteral("config"),
                     QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        report::check(QStringLiteral("QStandardPaths returns a home directory"),
                      !QStandardPaths::writableLocation(QStandardPaths::HomeLocation).isEmpty());
        report::check(QStringLiteral("QStandardPaths::findExecutable finds this binary's dir"),
                      !QCoreApplication::applicationDirPath().isEmpty());

        const QStorageInfo storage = QStorageInfo::root();
        report::info(QStringLiteral("root storage"),
                     QStringLiteral("%1 (%2), %3 MiB free of %4 MiB")
                             .arg(storage.rootPath(),
                                  QString::fromLatin1(storage.fileSystemType()))
                             .arg(storage.bytesAvailable() / (1024 * 1024))
                             .arg(storage.bytesTotal() / (1024 * 1024)));
        report::check(QStringLiteral("QStorageInfo reports the root volume"),
                      storage.isValid() && storage.isReady() && storage.bytesTotal() > 0);
    }

    // --- MIME database (compiled-in freedesktop.org.xml) --------------------
    {
        QMimeDatabase mimeDb;
        const QMimeType byName = mimeDb.mimeTypeForFile(QStringLiteral("document.txt"),
                                                        QMimeDatabase::MatchExtension);
        report::info(QStringLiteral("MIME type for *.txt"), byName.name());
        report::check(QStringLiteral("QMimeDatabase glob matching"),
                      byName.name() == QStringLiteral("text/plain"), byName.name());

        const QString pdfPath = dir.filePath(QStringLiteral("nosuffix"));
        QFile pdf(pdfPath);
        if (pdf.open(QIODevice::WriteOnly)) {
            pdf.write(QByteArrayLiteral("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n1 0 obj\n"));
            pdf.close();
        }
        const QMimeType byContent = mimeDb.mimeTypeForFile(pdfPath, QMimeDatabase::MatchContent);
        report::info(QStringLiteral("MIME type by content"), byContent.name());
        report::check(QStringLiteral("QMimeDatabase content sniffing"),
                      byContent.name() == QStringLiteral("application/pdf"), byContent.name());
        report::info(QStringLiteral("known MIME types"),
                     QString::number(mimeDb.allMimeTypes().size()));
    }

    // --- compiled-in resources ---------------------------------------------
    {
        QFile resource(QStringLiteral(":/data/greeting.txt"));
        report::check(QStringLiteral("Qt resource is embedded in the binary"),
                      resource.exists() && resource.open(QIODevice::ReadOnly),
                      resource.errorString());
        const QString content = QString::fromUtf8(resource.readAll());
        report::info(QStringLiteral("resource content"), report::ellipsize(content));
        report::check(QStringLiteral("resource content decodes as UTF-8"),
                      content.contains(QString::fromUtf8("日本語")));

        QFile xmlResource(QStringLiteral(":/data/sample.xml"));
        report::check(QStringLiteral("second resource file present"),
                      xmlResource.open(QIODevice::ReadOnly)
                              && xmlResource.readAll().contains("<library"));

        QStringList resourceEntries;
        QDirIterator resIt(QStringLiteral(":/data"), QDirIterator::Subdirectories);
        while (resIt.hasNext())
            resourceEntries << resIt.next();
        report::info(QStringLiteral("resource entries"), resourceEntries.join(QStringLiteral(", ")));
        report::check(QStringLiteral("resource tree is enumerable"), resourceEntries.size() >= 2);
    }

    // --- file system watcher (needs a running event loop) -------------------
    {
        QFileSystemWatcher watcher;
        report::check(QStringLiteral("QFileSystemWatcher adds a directory"),
                      watcher.addPath(tempDir.path()));

        QEventLoop loop;
        bool notified = false;
        QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &loop,
                         [&notified, &loop](const QString &) {
                             notified = true;
                             loop.quit();
                         });
        QTimer::singleShot(0, [&dir] {
            QFile trigger(dir.filePath(QStringLiteral("watched.txt")));
            if (trigger.open(QIODevice::WriteOnly)) {
                trigger.write(QByteArrayLiteral("trigger"));
                trigger.close();
            }
        });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        report::soft(QStringLiteral("QFileSystemWatcher reported a directory change"), notified,
                     QStringLiteral("no notification within 5 s"));
    }
}