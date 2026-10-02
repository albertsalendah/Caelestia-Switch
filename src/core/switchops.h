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
    // Non-empty "Property=value" entries among `properties` (RequiredBy, WantedBy, BoundBy).
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

} // namespace cs
