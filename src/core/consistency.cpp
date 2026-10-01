#include "consistency.h"

#include "plasmaconfig.h"

namespace cs {

bool caelestiaRunning(const Readings &r)
{
    return r.caelestiaUnit.active() || r.quickshellRunning;
}

bool plasmaDrawing(const Readings &r)
{
    return r.plasmashellRunning && isStockShellPackage(r.shellPackage);
}

Provider computeProvider(const Readings &r)
{
    const bool c = caelestiaRunning(r);
    const bool p = plasmaDrawing(r);
    if (c && p) {
        return Provider::Both;
    }
    if (c) {
        return Provider::Caelestia;
    }
    if (p) {
        return Provider::Plasma;
    }
    return Provider::None;
}

QStringList findInconsistencies(const Readings &r)
{
    QStringList reasons;
    if (!r.systemdReachable) {
        reasons << QStringLiteral("cannot query the systemd user manager: %1").arg(r.systemdError);
        return reasons;
    }
    if (r.plasmashellUnit.masked() && r.plasmashellRunning) {
        reasons << QStringLiteral("plasmashell is masked but still running");
    }
    if (caelestiaRunning(r) && plasmaDrawing(r)) {
        reasons << QStringLiteral("Caelestia and the stock Plasma shell are both providing panels");
    }
    if (!caelestiaRunning(r) && !r.plasmashellRunning) {
        reasons << QStringLiteral("neither shell is running");
    }
    if (r.switchStateMode == QLatin1String("transitioning") || r.switchStateMode == QLatin1String("pending-logout")) {
        reasons << QStringLiteral("the state file says a switch is in progress (mode=%1)").arg(r.switchStateMode);
    }
    return reasons;
}

} // namespace cs
