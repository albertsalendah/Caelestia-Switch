#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace cs {

// One systemd user unit as seen over D-Bus.
struct UnitState {
    bool queried = false;   // the D-Bus query succeeded
    QString loadState;      // loaded / masked / not-found
    QString activeState;    // active / inactive / failed / ...
    QString subState;       // running / dead / ...
    QString unitFileState;  // enabled / disabled / masked / static / ...

    bool masked() const
    {
        return loadState == QLatin1String("masked") || unitFileState.startsWith(QLatin1String("masked"));
    }
    bool active() const { return activeState == QLatin1String("active"); }
    bool enabled() const { return unitFileState.startsWith(QLatin1String("enabled")); }
    bool notFound() const { return loadState == QLatin1String("not-found"); }
};

// What is known about the installed Caelestia (spec: Source and version detection).
struct InstallInfo {
    bool installed = false;
    QString commit;                        // contents of .current_commit
    QString source = QStringLiteral("unknown");   // ladybug-me / fork / unknown
    QString version = QStringLiteral("unknown");  // e.g. v2.5.0 / unknown
    QString origin;                        // normalized origin URL, if found
    QString checkout;                      // checkout the answer came from, if any
    QString sourceFrom = QStringLiteral("none");  // marker / checkout / none
    QString versionFrom = QStringLiteral("none"); // marker / .current_version / checkout / none
};

enum class Provider { None, Caelestia, Plasma, Both };

struct Readings {
    bool systemdReachable = false;
    QString systemdError;

    UnitState plasmashellUnit;   // plasma-plasmashell.service
    UnitState caelestiaUnit;     // caelestia-shell.service
    bool plasmashellRunning = false;
    bool quickshellRunning = false;
    QString shellPackage;        // ShellPackage from plasmashellrc; empty = unset (stock default)

    QString switchStateMode;     // mode from the app's state file; empty = no state file
    InstallInfo install;

    Provider provider = Provider::None;
    QStringList inconsistentReasons;

    bool inconsistent() const { return !inconsistentReasons.isEmpty(); }
};

// Collects every reading. Read-only; local only (no network).
Readings gatherReadings();

// First word of the state file, lowercased, with an optional "mode=" prefix removed.
// The real state file format is defined in Phase A3; this reads it leniently.
QString parseStateMode(const QString &text);

QString providerKey(Provider p);   // caelestia / plasma / none / both
QString toText(const Readings &r);
QJsonObject toJson(const Readings &r);

} // namespace cs
