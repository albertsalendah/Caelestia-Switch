#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace cs {

// Two-sided, timestamped backups (architecture D11, narrowed 2026-10-01).
enum class Side { Stock, Caelestia };

QString sideKey(Side side);                      // "stock" / "caelestia"
bool parseSide(const QString &text, Side *out);  // case-insensitive

// Where things live; a parameter so tests can use a fake home.
struct BackupPaths {
    QString home;          // ~
    QString configHome;    // ~/.config
    QString dataRoot;      // ~/.local/share/caelestia-switch/backups
    QString appConfigDir;  // ~/.config/caelestia-switch (install marker)
    bool lookupPlasmaVersion = true;  // reads the installed Plasma version from a file; tests switch it off
    static BackupPaths defaults();
};

// Plasma version from the text of LibKWorkspaceConfigVersion.cmake (`set(PACKAGE_VERSION "6.7.5")`); empty if absent.
QString parsePlasmaVersion(const QString &cmakeVersionFileText);

struct BackupInfo {
    Side side = Side::Stock;
    QString id;               // e.g. 20261002_071500
    QString dir;              // absolute path of the backup directory
    QDateTime created;
    QString trigger;          // "manual" / "auto-leave"
    QString caelestiaCommit;
    QString caelestiaVersion;
    QString plasmaVersion;

    QString ref() const { return sideKey(side) + QLatin1Char('/') + id; }
};

struct OpResult {
    bool ok = true;
    QString error;
    QStringList warnings;
};

// Snapshots the items of `side` into <dataRoot>/<side>/<id>/. *refOut gets "side/id".
OpResult createBackup(Side side, const QString &trigger, const BackupPaths &paths, QString *refOut = nullptr);

// Newest first.
QList<BackupInfo> listBackups(Side side, const BackupPaths &paths);
QList<BackupInfo> listBackups(const BackupPaths &paths);

// Applies a backup ("side/id") to the config files. Writes config only: the caller
// must have stopped the outgoing shell first (switch order, spec step 6).
OpResult restoreBackup(const QString &ref, const BackupPaths &paths);

} // namespace cs
