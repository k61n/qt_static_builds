#pragma once

#include <QString>

// Minimal STDOUT reporting helpers shared by all test groups.
namespace report {

void section(const QString &title);

void info(const QString &key, const QString &value);
void info(const QString &key, bool value);
void note(const QString &text);
void listItem(const QString &text);

// Hard requirement: a failure makes the whole run fail.
bool check(const QString &name, bool ok, const QString &detail = QString());
// Soft requirement: reported as a warning, does not fail the run.
bool soft(const QString &name, bool ok, const QString &detail = QString());
void skip(const QString &name, const QString &reason);

QString yesNo(bool value);
QString ellipsize(const QString &text, int maxLength = 72);

int failedCount();
void summary();

} // namespace report