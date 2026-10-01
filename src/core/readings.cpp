#include "readings.h"

#include "consistency.h"
#include "fsutil.h"
#include "install.h"
#include "plasmaconfig.h"
#include "systemd.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace cs {

namespace {

QJsonObject unitToJson(const UnitState &u)
{
    return QJsonObject{
        {QStringLiteral("queried"), u.queried},
        {QStringLiteral("loadState"), u.loadState},
        {QStringLiteral("activeState"), u.activeState},
        {QStringLiteral("subState"), u.subState},
        {QStringLiteral("unitFileState"), u.unitFileState},
        {QStringLiteral("masked"), u.masked()},
    };
}

QString providerLabel(Provider p)
{
    switch (p) {
    case Provider::Caelestia:
        return QStringLiteral("Caelestia");
    case Provider::Plasma:
        return QStringLiteral("Plasma");
    case Provider::Both:
        return QStringLiteral("both (inconsistent)");
    case Provider::None:
        break;
    }
    return QStringLiteral("none");
}

QString yesNo(bool v)
{
    return v ? QStringLiteral("yes") : QStringLiteral("no");
}

QString describePlasmashellUnit(const UnitState &u)
{
    if (!u.queried) {
        return QStringLiteral("unknown");
    }
    return QStringLiteral("%1 (%2/%3)")
        .arg(u.masked() ? QStringLiteral("masked") : QStringLiteral("not masked"), u.activeState, u.subState);
}

QString describeCaelestiaUnit(const UnitState &u)
{
    if (!u.queried) {
        return QStringLiteral("unknown");
    }
    if (u.notFound()) {
        return QStringLiteral("unit not found");
    }
    return QStringLiteral("%1 (%2), %3").arg(u.active() ? QStringLiteral("active") : QStringLiteral("inactive"),
                                              u.activeState + QLatin1Char('/') + u.subState,
                                              u.unitFileState.isEmpty() ? QStringLiteral("unit file state unknown") : u.unitFileState);
}

QString line(const QString &label, const QString &value)
{
    return label.leftJustified(21) + QStringLiteral(": ") + value + QLatin1Char('\n');
}

QString withOrigin(const QString &value, const QString &from)
{
    return from == QLatin1String("none") ? value : QStringLiteral("%1 (from %2)").arg(value, from);
}

} // namespace

QString parseStateMode(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[\\s,;]"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString l : lines) {
        l = l.trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (l.startsWith(QLatin1String("mode="))) {
            l = l.mid(5).trimmed();
        }
        return l.section(separators, 0, 0).toLower();
    }
    return {};
}

QString providerKey(Provider p)
{
    switch (p) {
    case Provider::Caelestia:
        return QStringLiteral("caelestia");
    case Provider::Plasma:
        return QStringLiteral("plasma");
    case Provider::Both:
        return QStringLiteral("both");
    case Provider::None:
        break;
    }
    return QStringLiteral("none");
}

Readings gatherReadings()
{
    Readings r;

    QString err1;
    QString err2;
    const bool a = queryUnit(QStringLiteral("plasma-plasmashell.service"), r.plasmashellUnit, &err1);
    const bool b = a && queryUnit(QStringLiteral("caelestia-shell.service"), r.caelestiaUnit, &err2);
    r.systemdReachable = a && b;
    if (!r.systemdReachable) {
        r.systemdError = !a ? err1 : err2;
    }

    r.plasmashellRunning = isProcessRunning(QStringLiteral("plasmashell"));
    r.quickshellRunning = isProcessRunning(QStringLiteral("quickshell"));
    r.shellPackage = readShellPackage();
    r.switchStateMode = parseStateMode(readTextFile(appConfigDir() + QStringLiteral("/state")));
    r.install = detectInstall();

    r.provider = computeProvider(r);
    r.inconsistentReasons = findInconsistencies(r);
    return r;
}

QString toText(const Readings &r)
{
    QString out;
    out += line(QStringLiteral("Bar/panel provider"), providerLabel(r.provider));
    out += line(QStringLiteral("plasmashell process"), r.plasmashellRunning ? QStringLiteral("running") : QStringLiteral("stopped"));
    out += line(QStringLiteral("plasmashell unit"), describePlasmashellUnit(r.plasmashellUnit));
    out += line(QStringLiteral("ShellPackage"), r.shellPackage.isEmpty() ? QStringLiteral("(unset, stock default)") : r.shellPackage);
    out += line(QStringLiteral("Caelestia service"), describeCaelestiaUnit(r.caelestiaUnit));
    out += line(QStringLiteral("quickshell process"), r.quickshellRunning ? QStringLiteral("running") : QStringLiteral("stopped"));
    out += line(QStringLiteral("Caelestia installed"), yesNo(r.install.installed));
    out += line(QStringLiteral("Source"), withOrigin(r.install.source, r.install.sourceFrom));
    out += line(QStringLiteral("Version"), withOrigin(r.install.version, r.install.versionFrom));
    out += line(QStringLiteral("Commit"), r.install.commit.isEmpty() ? QStringLiteral("unknown") : r.install.commit);
    if (!r.install.checkout.isEmpty()) {
        out += line(QStringLiteral("Checkout"), r.install.checkout);
    }
    if (!r.install.origin.isEmpty()) {
        out += line(QStringLiteral("Origin"), r.install.origin);
    }
    out += line(QStringLiteral("Switch state file"), r.switchStateMode.isEmpty() ? QStringLiteral("none") : r.switchStateMode);
    out += line(QStringLiteral("Consistency"), r.inconsistent() ? QStringLiteral("INCONSISTENT") : QStringLiteral("ok"));
    for (const QString &reason : r.inconsistentReasons) {
        out += QStringLiteral("  - %1\n").arg(reason);
    }
    return out;
}

QJsonObject toJson(const Readings &r)
{
    const QJsonObject install{
        {QStringLiteral("installed"), r.install.installed},
        {QStringLiteral("commit"), r.install.commit},
        {QStringLiteral("source"), r.install.source},
        {QStringLiteral("sourceFrom"), r.install.sourceFrom},
        {QStringLiteral("version"), r.install.version},
        {QStringLiteral("versionFrom"), r.install.versionFrom},
        {QStringLiteral("origin"), r.install.origin},
        {QStringLiteral("checkout"), r.install.checkout},
    };
    return QJsonObject{
        {QStringLiteral("provider"), providerKey(r.provider)},
        {QStringLiteral("systemd"), QJsonObject{{QStringLiteral("reachable"), r.systemdReachable},
                                                 {QStringLiteral("error"), r.systemdError}}},
        {QStringLiteral("plasmashell"), QJsonObject{{QStringLiteral("running"), r.plasmashellRunning},
                                                     {QStringLiteral("unit"), unitToJson(r.plasmashellUnit)},
                                                     {QStringLiteral("shellPackage"), r.shellPackage}}},
        {QStringLiteral("caelestia"), QJsonObject{{QStringLiteral("service"), unitToJson(r.caelestiaUnit)},
                                                   {QStringLiteral("quickshellRunning"), r.quickshellRunning},
                                                   {QStringLiteral("install"), install}}},
        {QStringLiteral("switchStateMode"), r.switchStateMode},
        {QStringLiteral("consistent"), !r.inconsistent()},
        {QStringLiteral("inconsistentReasons"), QJsonArray::fromStringList(r.inconsistentReasons)},
    };
}

} // namespace cs
