#include "screens.h"

namespace cs {

QString targetLabel(Direction direction)
{
    return direction == Direction::ToStock ? QStringLiteral("stock Plasma") : QStringLiteral("Caelestia");
}

ScreenModel buildScreenModel(const Readings &r, const BackupPaths &paths, SwitchOps *ops)
{
    ScreenModel m;
    m.readings = r;

    if (!r.systemdReachable || r.inconsistent()) {
        QString why = r.inconsistentReasons.join(QStringLiteral("; "));
        if (why.isEmpty()) {
            why = r.systemdError.isEmpty() ? QStringLiteral("the state could not be read") : r.systemdError;
        }
        m.kind = ScreenModel::Kind::Blocked;
        m.message = QStringLiteral("Switching is not available right now: %1. If a switch failed or was interrupted, "
                                   "run 'caelestia-switch repair' in a terminal.").arg(why);
        return m;
    }
    if (!r.install.installed) {
        m.kind = ScreenModel::Kind::Blocked;
        m.message = QStringLiteral("Caelestia is not installed. Installing it from this app comes in a later version.");
        return m;
    }
    if (r.provider == Provider::None) {
        m.kind = ScreenModel::Kind::Blocked;
        m.message = QStringLiteral("Neither Caelestia nor stock Plasma is providing the panels, so the direction of a switch is unclear. "
                                   "Use 'caelestia-switch on' or 'caelestia-switch off' in a terminal.");
        return m;
    }

    const bool inCaelestia = r.provider == Provider::Caelestia;

    // Screen A: the spec wants a Caelestia-side backup before any switching.
    if (listBackups(Side::Caelestia, paths).isEmpty()) {
        m.kind = ScreenModel::Kind::NeedBackup;
        m.canBackup = inCaelestia;
        m.message = inCaelestia
            ? QStringLiteral("There is no Caelestia-side backup yet. Create one first: it is what the app restores when you come back to Caelestia.")
            : QStringLiteral("There is no Caelestia-side backup yet, and it can only be taken while Caelestia is the running shell "
                             "(taken now, it would store the stock configuration under the Caelestia name). "
                             "Log in with Caelestia active, then open this window again.");
        return m;
    }

    // Screen B.
    m.kind = ScreenModel::Kind::Switch;
    m.direction = inCaelestia ? Direction::ToStock : Direction::ToCaelestia;
    m.backups = listBackups(inCaelestia ? Side::Stock : Side::Caelestia, paths);
    m.canSwitch = !m.backups.isEmpty();
    if (!m.canSwitch) {
        m.message = QStringLiteral("There is no stock-side backup, so there is nothing to restore when going back to stock Plasma. "
                                   "A stock-side backup has to be taken while stock Plasma is running; the app does not create one from the Caelestia state.");
    }
    m.maskShown = !inCaelestia;
    if (m.maskShown && ops) {
        const MaskCheck mc = checkMaskPlasmashell(ops);
        m.maskAllowed = mc.allowed();
        m.maskBlockers = mc.blockers;
        m.maskWeak = mc.weak;
    }
    return m;
}

} // namespace cs
