#pragma once

#include <QString>
#include <QStringList>

#include "readings.h"

namespace cs {

// Everything the switch does to the running system, behind an interface so the step
// logic can be tested with a fake.
class SwitchOps
{
public:
    virtual ~SwitchOps() = default;

    virtual Readings readings() = 0;
    virtual UnitState unitState(const QString &unit) = 0;
    virtual QString shellPackage() = 0;

    // `systemctl --user <args>`; false (and *error) on a non-zero exit.
    virtual bool systemctl(const QStringList &args, QString *error) = 0;
    // Non-empty "Property=value" entries among `properties` (e.g. RequiredBy, RequisiteOf, BoundBy, WantedBy).
    virtual QStringList dependents(const QString &unit, const QStringList &properties) = 0;
    virtual bool waitInactive(const QString &unit, int timeoutMs) = 0;

    // Quits Caelestia so it releases its stolen shortcuts and screen corner (architecture D18).
    virtual bool stopCaelestiaGracefully(QStringList *warnings, QString *error) = 0;
    // Safety net after the quit: makes KWin reload the Overview effect (corner).
    virtual void reloadOverviewEffect(QStringList *warnings) = 0;

    virtual bool logout(QString *error) = 0;
};

// The real implementation: systemd user manager, quickshell CLI, KWin and logout over D-Bus.
class RealOps : public SwitchOps
{
public:
    Readings readings() override;
    UnitState unitState(const QString &unit) override;
    QString shellPackage() override;
    bool systemctl(const QStringList &args, QString *error) override;
    QStringList dependents(const QString &unit, const QStringList &properties) override;
    bool waitInactive(const QString &unit, int timeoutMs) override;
    bool stopCaelestiaGracefully(QStringList *warnings, QString *error) override;
    void reloadOverviewEffect(QStringList *warnings) override;
    bool logout(QString *error) override;
};

// True for the D-Bus errors that mean "no notification server yet" (the call never reached one), so sending
// again is safe. A timeout is NOT in this list: the server may be slow but has probably taken the message,
// and sending again shows it twice (seen live 2026-10-05: three popups under Caelestia).
bool notificationErrorIsRetryable(const QString &dbusErrorName);

// Desktop notification over D-Bus (org.freedesktop.Notifications), served by Caelestia or by plasmashell.
// Right after a login the server may not be up yet, so "no server" is retried about once a second for up to
// `waitSeconds`; any other failure, a timeout included, is reported and never resent.
bool sendNotification(const QString &summary, const QString &body, int waitSeconds, QString *error);

} // namespace cs
