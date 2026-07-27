#include "report.h"
#include "tests.h"

#include <QCoreApplication>

#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    // Helper mode: the QProcess test re-executes this very binary.
    if (argc > 1 && std::strcmp(argv[1], "--child-echo") == 0) {
        std::fputs("hello from child\n", stdout);
        std::fflush(stdout);
        return 42;
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QtStaticTest"));
    QCoreApplication::setApplicationName(QStringLiteral("qt_static_test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    testBuildInfo();
    testText();
    testDateTime();
    testSerialization();
    testFiles();
    testObjects();
    testConcurrency();
    testSql();
    testNetwork();

    report::summary();
    return report::failedCount() == 0 ? 0 : 1;
}