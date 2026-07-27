#include "report.h"
#include "tests.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QOperatingSystemVersion>
#include <QPluginLoader>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVersionNumber>

namespace {

QString compilerDescription()
{
#if defined(Q_CC_CLANG)
    return QStringLiteral("Clang %1.%2.%3")
            .arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(Q_CC_MSVC)
    return QStringLiteral("MSVC _MSC_VER=%1").arg(_MSC_VER);
#elif defined(Q_CC_GNU)
    return QStringLiteral("GCC %1.%2.%3")
            .arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    return QStringLiteral("unknown");
#endif
}

void reportFeature(const QString &name, int value)
{
    report::info(QStringLiteral("feature: ") + name,
                 value == 1 ? QStringLiteral("enabled") : QStringLiteral("disabled"));
}

} // namespace

void testBuildInfo()
{
    report::section(QStringLiteral("Build and runtime information"));

    report::info(QStringLiteral("Qt version (compile time)"), QStringLiteral(QT_VERSION_STR));
    report::info(QStringLiteral("Qt version (runtime)"), QString::fromLatin1(qVersion()));
    report::info(QStringLiteral("Qt build string"), report::ellipsize(QLibraryInfo::build(), 120));
    report::info(QStringLiteral("C++ standard (__cplusplus)"), QString::number(__cplusplus));
    report::info(QStringLiteral("Compiler"), compilerDescription());
    report::info(QStringLiteral("Application file"), QCoreApplication::applicationFilePath());

    report::check(QStringLiteral("runtime Qt version matches compile-time headers"),
                  QVersionNumber::fromString(QString::fromLatin1(qVersion()))
                          == QVersionNumber(QT_VERSION_MAJOR, QT_VERSION_MINOR, QT_VERSION_PATCH),
                  QString::fromLatin1(qVersion()));

    // --- static linkage -----------------------------------------------------
#if defined(QT_STATIC)
    const bool compiledAgainstStatic = true;
#else
    const bool compiledAgainstStatic = false;
#endif
    report::info(QStringLiteral("QT_STATIC defined"), compiledAgainstStatic);
    report::info(QStringLiteral("QLibraryInfo::isSharedBuild()"), QLibraryInfo::isSharedBuild());
    report::soft(QStringLiteral("Qt is a static build"),
                 compiledAgainstStatic && !QLibraryInfo::isSharedBuild(),
                 QStringLiteral("QT_STATIC=%1, isSharedBuild=%2")
                         .arg(report::yesNo(compiledAgainstStatic),
                              report::yesNo(QLibraryInfo::isSharedBuild())));

    // --- host ---------------------------------------------------------------
    report::info(QStringLiteral("Build ABI"), QSysInfo::buildAbi());
    report::info(QStringLiteral("Build CPU architecture"), QSysInfo::buildCpuArchitecture());
    report::info(QStringLiteral("Current CPU architecture"), QSysInfo::currentCpuArchitecture());
    report::info(QStringLiteral("Kernel"), QSysInfo::kernelType() + QLatin1Char(' ')
                                                   + QSysInfo::kernelVersion());
    report::info(QStringLiteral("Product"), QSysInfo::prettyProductName());
    report::info(QStringLiteral("Byte order"),
                 QSysInfo::ByteOrder == QSysInfo::LittleEndian ? QStringLiteral("little endian")
                                                               : QStringLiteral("big endian"));
    const QOperatingSystemVersion osv = QOperatingSystemVersion::current();
    report::info(QStringLiteral("Operating system version"),
                 QStringLiteral("%1 %2.%3.%4").arg(osv.name()).arg(osv.majorVersion())
                         .arg(osv.minorVersion()).arg(osv.microVersion()));

    // --- selected Qt configure features ------------------------------------
#ifdef QT_FEATURE_thread
    reportFeature(QStringLiteral("thread"), QT_FEATURE_thread);
#endif
#ifdef QT_FEATURE_process
    reportFeature(QStringLiteral("process"), QT_FEATURE_process);
#endif
#ifdef QT_FEATURE_icu
    reportFeature(QStringLiteral("icu"), QT_FEATURE_icu);
#endif
#ifdef QT_FEATURE_timezone
    reportFeature(QStringLiteral("timezone"), QT_FEATURE_timezone);
#endif
#ifdef QT_FEATURE_zstd
    reportFeature(QStringLiteral("zstd"), QT_FEATURE_zstd);
#endif
#ifdef QT_FEATURE_system_zlib
    reportFeature(QStringLiteral("system_zlib"), QT_FEATURE_system_zlib);
#endif
#ifdef QT_FEATURE_library
    reportFeature(QStringLiteral("library (dynamic loading)"), QT_FEATURE_library);
#endif
#ifdef QT_FEATURE_dbus
    reportFeature(QStringLiteral("dbus"), QT_FEATURE_dbus);
#endif
#ifdef QT_FEATURE_openssl
    reportFeature(QStringLiteral("openssl"), QT_FEATURE_openssl);
#endif
#ifdef QT_FEATURE_openssl_linked
    reportFeature(QStringLiteral("openssl_linked"), QT_FEATURE_openssl_linked);
#endif
#ifdef QT_FEATURE_securetransport
    reportFeature(QStringLiteral("securetransport"), QT_FEATURE_securetransport);
#endif
#ifdef QT_FEATURE_schannel
    reportFeature(QStringLiteral("schannel"), QT_FEATURE_schannel);
#endif

    // --- paths and plugins --------------------------------------------------
    report::info(QStringLiteral("Prefix path"), QLibraryInfo::path(QLibraryInfo::PrefixPath));
    report::info(QStringLiteral("Plugins path"), QLibraryInfo::path(QLibraryInfo::PluginsPath));
    report::info(QStringLiteral("Translations path"),
                 QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    report::info(QStringLiteral("Writable AppDataLocation"),
                 QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    report::note(QStringLiteral("Library paths:"));
    const QStringList libraryPaths = QCoreApplication::libraryPaths();
    for (const QString &path : libraryPaths)
        report::listItem(path);
    if (libraryPaths.isEmpty())
        report::listItem(QStringLiteral("(none)"));

    const QList<QStaticPlugin> staticPlugins = QPluginLoader::staticPlugins();
    report::info(QStringLiteral("Statically linked plugins"), QString::number(staticPlugins.size()));
    for (const QStaticPlugin &plugin : staticPlugins) {
        const QJsonObject metaData = plugin.metaData();
        report::listItem(QStringLiteral("%1  [%2]")
                                 .arg(metaData.value(QStringLiteral("className")).toString(),
                                      metaData.value(QStringLiteral("IID")).toString()));
    }
    report::soft(QStringLiteral("at least one plugin is linked into the binary"),
                 !staticPlugins.isEmpty(),
                 QStringLiteral("no static plugins registered"));
}