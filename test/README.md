# qt_static_test

A console application that exercises the non-GUI parts of Qt and prints what it
finds to STDOUT. It is meant as a smoke test for statically linked Qt builds:
besides the plain API checks it reports how the Qt in use was configured
(static/shared, enabled features, statically imported plugins, TLS backend,
SQL drivers, bundled CLDR/tz/MIME data).

## Building

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/static-qt -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/qt_static_test
```

Required Qt modules: `Core`, `Concurrent`, `Network`, `Sql`.

## Output

Each line is either an information line or a result:

| Marker   | Meaning                                                        |
| -------- | -------------------------------------------------------------- |
| `[ OK ]` | check passed                                                     |
| `[FAIL]` | check failed — the process exits with 1                          |
| `[WARN]` | optional/environment dependent check failed — exit code unchanged |
| `[SKIP]` | check not applicable to this build                               |

The process exits with `0` when there are no failures, `1` otherwise, so it can
be used directly as a CI step. The last line is `RESULT: PASS` or `RESULT: FAIL`.

## What is covered

| Source file           | Area                                                                                       |
| --------------------- | ------------------------------------------------------------------------------------------ |
| `t_buildinfo.cpp`     | Qt version, `QT_STATIC` / `QLibraryInfo::isSharedBuild()`, configure features, library paths, statically linked plugins |
| `t_text.cpp`          | QString/QByteArray, Unicode tables, QStringConverter, PCRE2 regular expressions, QTextBoundaryFinder, QCollator, hashes, zlib, QUrl/IDN |
| `t_datetime.cpp`      | QDate/QTime/QDateTime, IANA time zone database and DST, QLocale/CLDR data, QElapsedTimer      |
| `t_serialization.cpp` | JSON, CBOR, QXmlStream, QDataStream, QBuffer, QSettings, QUuid, QRandomGenerator              |
| `t_files.cpp`         | QFile/QSaveFile/QDir/QDirIterator, QStandardPaths, QStorageInfo, QMimeDatabase, compiled-in `.qrc` resources, QFileSystemWatcher |
| `t_objects.cpp`       | moc/meta-object system, properties, signals & slots, QVariant/QMetaType, event loop and timers, logging, QCommandLineParser, QProcess |
| `t_concurrency.cpp`   | QThread, cross-thread connections, QThreadPool, mutexes/semaphores/wait conditions, QtConcurrent, QFuture |
| `t_sql.cpp`           | Available SQL drivers and a full SQLite round trip (in-memory database)                      |
| `t_network.cpp`       | Host addresses and interfaces, TCP/UDP over loopback, TLS backend discovery, QNetworkAccessManager against a local HTTP server, async DNS |

The QProcess test re-executes the binary with `--child-echo`; in that mode it
prints one line and exits with code 42 instead of running the tests.

No test needs internet access — the HTTP test talks to a `QTcpServer` started
inside the process.