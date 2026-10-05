#include "switch.h"

#include "fsutil.h"
#include "plasmaconfig.h"
#include "state.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QThread>

namespace cs {

namespace {

const QString kCaelestiaUnit = QStringLiteral("caelestia-shell.service");
const QString kPlasmashellUnit = QStringLiteral("plasma-plasmashell.service");
const QStringList kHelperUnits = {QStringLiteral("cliphist.service")};

void note(const SwitchContext &ctx, const QString &msg)
{
    if (ctx.log) {
        ctx.log(msg);
    }
}

QStringList readLines(const QString &path)
{
    QStringList out;
    const QStringList lines = readTextFile(path).split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        if (!l.trimmed().isEmpty()) {
            out << l.trimmed();
        }
    }
    return out;
}

bool writeLines(const QString &path, const QStringList &lines)
{
    if (lines.isEmpty()) {
        return !QFileInfo::exists(path) || QFile::remove(path);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(lines.join(QLatin1Char('\n')).toUtf8() + '\n') >= 0;
}

QStringList withoutStateReasons(const QStringList &reasons)
{
    QStringList out;
    for (const QString &r : reasons) {
        if (!r.contains(QLatin1String("state file"))) {
            out << r;
        }
    }
    return out;
}

// What must be true after the logout, for finishSwitch.
QStringList finalProblems(const SwitchState &st, const Readings &r)
{
    QStringList p = withoutStateReasons(r.inconsistentReasons);
    const bool toStock = st.direction == directionKey(Direction::ToStock);
    if (toStock) {
        if (r.provider != Provider::Plasma) {
            p << QStringLiteral("stock Plasma is not providing the panels yet");
        }
        if (r.caelestiaUnit.active() || r.quickshellRunning) {
            p << QStringLiteral("Caelestia is still running");
        }
        if (r.caelestiaUnit.enabled()) {
            p << QStringLiteral("Caelestia is still enabled");
        }
    } else {
        if (r.provider != Provider::Caelestia) {
            p << QStringLiteral("Caelestia is not providing the bar yet");
        }
        if (st.maskPlasmashell && (!r.plasmashellUnit.masked() || r.plasmashellRunning)) {
            p << QStringLiteral("plasmashell should be masked and stopped");
        }
        if (!st.maskPlasmashell && r.plasmashellUnit.masked()) {
            p << QStringLiteral("plasmashell is masked");
        }
    }
    return p;
}

QString maskBlockedMessage(const QStringList &blockers)
{
    return QStringLiteral("refusing to mask plasmashell, something depends on it: %1").arg(blockers.join(QStringLiteral(", ")));
}

QString maskWeakMessage(const QStringList &weak)
{
    return QStringLiteral("plasmashell is only weakly wanted by %1; masking it anyway (a WantedBy link does not stop a masked unit)")
        .arg(weak.join(QStringLiteral(", ")));
}

QString stepLabel(int step)
{
    switch (step) {
    case StepPreflight:
        return QStringLiteral("pre-flight");
    case StepSnapshot:
        return QStringLiteral("snapshot of the side being left");
    case StepStopOutgoing:
        return QStringLiteral("stopping the outgoing shell");
    case StepHelpers:
        return QStringLiteral("helper units");
    case StepRestore:
        return QStringLiteral("config restore");
    case StepUnits:
        return QStringLiteral("unit changes");
    case StepVerify:
        return QStringLiteral("verification");
    case StepLogout:
        return QStringLiteral("logout");
    default:
        break;
    }
    return QStringLiteral("unknown step");
}

QString shellLabel(bool stock)
{
    return stock ? QStringLiteral("stock Plasma") : QStringLiteral("Caelestia");
}

// One sentence saying what went wrong in the switch the state file describes (architecture D20).
QString describeFailure(const SwitchState &st)
{
    if (!st.cause.isEmpty()) {
        return st.cause;   // a repair of a repair keeps the original failure
    }
    const QString target = shellLabel(st.direction == directionKey(Direction::ToStock));
    const bool failed = st.result == QLatin1String("failed") || !st.error.isEmpty();
    if (st.mode == QLatin1String("pending-logout") && st.failedStep == 0 && failed) {
        return QStringLiteral("The switch to %1 finished its steps, but the expected state was not reached after the login: %2")
            .arg(target, st.error);
    }
    if (failed) {
        const int step = st.failedStep > 0 ? st.failedStep : st.step + 1;
        return QStringLiteral("The last switch to %1 failed at step %2 (%3): %4").arg(target).arg(step).arg(stepLabel(step), st.error);
    }
    return QStringLiteral("The last switch to %1 was interrupted after step %2 (%3); no failure was recorded")
        .arg(target).arg(st.step).arg(st.stepName.isEmpty() ? QStringLiteral("nothing done yet") : st.stepName);
}

QString rolledBackSummary(const QString &cause, bool toStock)
{
    QString c = cause.trimmed();
    while (c.endsWith(QLatin1Char('.'))) {
        c.chop(1);
    }
    return QStringLiteral("Rolled back: %1. You are back in %2 mode.")
        .arg(c.isEmpty() ? QStringLiteral("the switch did not complete") : c, shellLabel(toStock));
}

} // namespace

MaskCheck checkMaskPlasmashell(SwitchOps *ops)
{
    MaskCheck c;
    const QStringList found = ops->dependents(kPlasmashellUnit, {QStringLiteral("RequiredBy"), QStringLiteral("RequisiteOf"),
                                                                 QStringLiteral("BoundBy"), QStringLiteral("WantedBy")});
    for (const QString &entry : found) {
        if (entry.startsWith(QLatin1String("WantedBy="))) {
            c.weak << entry;
        } else {
            c.blockers << entry;   // strong, or an unreadable answer: treated as a dependent
        }
    }
    return c;
}

QString directionKey(Direction d)
{
    return d == Direction::ToStock ? QStringLiteral("to-stock") : QStringLiteral("to-caelestia");
}

bool parseDirection(const QString &text, Direction *out)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("to-stock")) {
        *out = Direction::ToStock;
        return true;
    }
    if (t == QLatin1String("to-caelestia")) {
        *out = Direction::ToCaelestia;
        return true;
    }
    return false;
}

QString modeOf(const Readings &r)
{
    if (r.provider == Provider::Caelestia) {
        return QStringLiteral("caelestia");
    }
    if (r.provider == Provider::Plasma) {
        return QStringLiteral("stock");
    }
    return {};
}

OpResult preflight(const SwitchRequest &req, const SwitchContext &ctx, bool checkState, SwitchPlan *plan)
{
    SwitchPlan p;
    const bool toStock = req.direction == Direction::ToStock;
    p.target = toStock ? Side::Stock : Side::Caelestia;
    const auto fail = [](const QString &why) { return OpResult{false, why, {}}; };

    SwitchState st;
    if (checkState && !req.repair && readState(ctx.stateFile, &st)
        && (st.mode == QLatin1String("transitioning") || st.mode == QLatin1String("pending-logout"))) {
        return fail(QStringLiteral("a switch is already in progress or waiting for the logout (mode=%1); "
                                   "use 'finish' after logging in, or 'repair'").arg(st.mode));
    }

    const Readings r = ctx.ops->readings();
    if (!r.systemdReachable) {
        return fail(QStringLiteral("cannot query the systemd user manager: %1").arg(r.systemdError));
    }
    // A repair exists to fix a half-switched system, so it is the one caller that skips the consistency check.
    const QStringList reasons = checkState ? r.inconsistentReasons : withoutStateReasons(r.inconsistentReasons);
    if (!req.repair && !reasons.isEmpty()) {
        return fail(QStringLiteral("the current state is inconsistent (%1); fix it with 'repair' first").arg(reasons.join(QStringLiteral("; "))));
    }

    if (toStock && r.provider == Provider::Plasma) {
        p.noop = true;
        p.message = QStringLiteral("already in stock Plasma mode");
    }
    if (!toStock && r.provider == Provider::Caelestia && r.plasmashellUnit.masked() == req.maskPlasmashell) {
        p.noop = true;
        p.message = QStringLiteral("already in Caelestia mode");
    }
    if (p.noop && req.repair && req.configMayBeTouched) {
        // The readings look right (for example plasmashell runs with the stock ShellPackage) but the failed
        // switch had started rewriting config: the snapshot still has to be restored.
        p.noop = false;
        p.message.clear();
    }
    if (p.noop) {
        if (plan) {
            *plan = p;
        }
        return {};
    }

    if (!toStock && !r.install.installed) {
        return fail(QStringLiteral("Caelestia is not installed"));
    }

    // The side being left is the one that is actually running, not the one implied by the direction:
    // snapshotting the wrong side would store the live config under the wrong name. With neither
    // shell drawing panels (provider none) there is no side to snapshot, and none is guessed.
    if (r.provider == Provider::Caelestia) {
        p.leaving = Side::Caelestia;
    } else if (r.provider == Provider::Plasma) {
        p.leaving = Side::Stock;
    } else {
        p.leavingKnown = false;
        p.snapshot = false;
        if (!req.repair) {
            p.warnings << QStringLiteral("neither shell was providing panels, so no snapshot of the side being left was taken");
        }
    }
    if (req.repair) {
        p.snapshot = false;   // never file a half-switched state under a side's name
    }

    // Not a no-op and Caelestia already runs: only the plasmashell mask differs. Nothing is stopped,
    // snapshotted or restored (restoring config into the running Caelestia is untested and unsafe).
    // (A repair that has a snapshot to restore takes the full path instead: Caelestia is quit first, then restored.)
    p.unitsOnly = !toStock && r.provider == Provider::Caelestia && (!req.repair || req.targetRef.isEmpty());
    if (p.unitsOnly) {
        p.snapshot = false;
        if (!req.targetRef.isEmpty() && !req.repair) {
            return fail(QStringLiteral("Caelestia is already running, so a backup would not be restored and has no effect here; "
                                       "leave out --backup, or switch to stock first"));
        }
    }

    // Target backup: the one asked for, else the newest of the target side (not needed when only the units change).
    if (!p.unitsOnly) {
        const QList<BackupInfo> available = listBackups(p.target, ctx.paths);
        if (req.targetRef.isEmpty()) {
            if (available.isEmpty()) {
                return fail(QStringLiteral("no %1-side backup exists; create one with 'backup --side %1'").arg(sideKey(p.target)));
            }
            p.targetRef = available.first().ref();
        } else {
            bool found = false;
            for (const BackupInfo &b : available) {
                found = found || b.ref() == req.targetRef;
            }
            if (!found) {
                return fail(QStringLiteral("backup '%1' not found on the %2 side").arg(req.targetRef, sideKey(p.target)));
            }
            p.targetRef = req.targetRef;
        }
    }

    // D1: live dependency check before masking plasmashell (strong dependents block, weak ones only warn).
    if (!toStock && req.maskPlasmashell) {
        const MaskCheck mc = checkMaskPlasmashell(ctx.ops);
        if (!mc.allowed()) {
            return fail(maskBlockedMessage(mc.blockers));
        }
        if (!mc.weak.isEmpty()) {
            p.warnings << maskWeakMessage(mc.weak);
        }
    }

    if (plan) {
        *plan = p;
    }
    return {};
}

SwitchOutcome runSwitch(const SwitchRequest &req, const SwitchContext &ctx)
{
    SwitchOutcome out;
    SwitchState st;
    if (req.repair) {
        readState(ctx.stateFile, &st);   // keeps what the switch being repaired recorded (leaving, cause, ...)
    }
    st.mode = QStringLiteral("transitioning");
    st.direction = directionKey(req.direction);
    st.maskPlasmashell = req.maskPlasmashell;
    st.logout = req.logout;
    st.step = 0;
    st.stepName.clear();
    st.targetRef.clear();
    if (!req.repair) {
        st.snapshotRef.clear();   // a repair keeps the snapshot of the switch it undoes, so it can be run again
    }
    st.helpers.clear();
    st.error.clear();
    st.result.clear();
    st.failedStep = 0;
    st.rolledBack = false;
    if (req.repair && !req.cause.isEmpty()) {
        st.cause = req.cause;
    }
    const bool toStock = req.direction == Direction::ToStock;

    QDir().mkpath(QFileInfo(ctx.stateFile).absolutePath());
    QLockFile lock(QFileInfo(ctx.stateFile).absolutePath() + QStringLiteral("/switch.lock"));
    if (!lock.tryLock(0)) {
        out.ok = false;
        out.error = QStringLiteral("another switch is already running");
        return out;
    }

    const auto save = [&]() {
        QString err;
        if (!writeState(ctx.stateFile, st, &err)) {
            out.warnings << err;
        }
    };
    const auto fail = [&](int step, const QString &why) {
        QString msg = why;
        if (req.repair && step >= StepStopOutgoing) {
            // Safety net: the rollback itself failed after shells were stopped. Do not leave the session
            // without a shell (the black desktop seen in the manual round trips): start the one being restored.
            const QString unit = toStock ? kPlasmashellUnit : kCaelestiaUnit;
            QString e;
            if (ctx.ops->systemctl({QStringLiteral("start"), unit}, &e)) {
                msg += QStringLiteral(" (started %1 so the session is not left without a shell; run 'repair' again)").arg(unit);
            } else if (e.contains(QLatin1String("timed out"))) {
                // systemctl start waits until the service reports ready; a shell waiting on an error dialog never does.
                // The job stays queued in systemd, so the shell is usually coming up anyway.
                msg += QStringLiteral(" (asked systemd to start %1, but it had not finished starting when the wait ended; "
                                      "a shell waiting on an error dialog does this. Fix the cause, then run 'repair' again)").arg(unit);
            } else {
                msg += QStringLiteral(" (could not start %1 either: %2)").arg(unit, e);
            }
        }
        st.error = msg;
        st.result = QStringLiteral("failed");
        st.failedStep = step;
        st.step = step - 1;
        save();
        out.ok = false;
        out.error = msg;
        out.failedStep = step;
        note(ctx, QStringLiteral("step %1 FAILED: %2").arg(step).arg(msg));
        return out;
    };
    const auto done = [&](int step, const QString &name) {
        st.step = step;
        st.stepName = name;
        save();
        note(ctx, QStringLiteral("step %1 done: %2").arg(step).arg(name));
    };

    // 1. Pre-flight.
    SwitchPlan plan;
    const OpResult pre = preflight(req, ctx, /*checkState=*/false, &plan);
    if (!pre.ok) {
        return fail(StepPreflight, pre.error);
    }
    if (plan.noop) {
        // Nothing to do: leave the state file describing the settled mode.
        st.mode = toStock ? QStringLiteral("stock") : QStringLiteral("caelestia");
        st.result = req.repair ? QStringLiteral("rolled-back") : QStringLiteral("done");
        st.step = StepFinished;
        if (req.repair) {
            st.rolledBack = true;   // the system already matched the side that was left; only the record is closed
            st.unseen = true;
        }
        save();
        out.noop = true;
        return out;
    }
    st.targetRef = plan.targetRef;
    out.warnings << plan.warnings;
    const Readings before = ctx.ops->readings();
    if (!req.repair) {
        // What `repair` needs to undo this switch later.
        st.leaving = plan.leavingKnown ? sideKey(plan.leaving) : QStringLiteral("none");
        st.plasmaWasMasked = before.plasmashellUnit.masked();
    }
    done(StepPreflight, QStringLiteral("pre-flight"));

    // 2. Automatic snapshot of the mode being left.
    if (plan.snapshot) {
        QString snapRef;
        const OpResult snap = createBackup(plan.leaving, QStringLiteral("auto-leave"), ctx.paths, &snapRef);
        if (!snap.ok) {
            return fail(StepSnapshot, QStringLiteral("snapshot failed: %1").arg(snap.error));
        }
        st.snapshotRef = snapRef;
        done(StepSnapshot, QStringLiteral("snapshot %1").arg(snapRef));
    } else {
        done(StepSnapshot, req.repair ? QStringLiteral("no snapshot (repair)")
                                      : plan.unitsOnly ? QStringLiteral("no snapshot (only the mask changes)")
                                                       : QStringLiteral("no snapshot (no shell was providing panels)"));
    }

    // 3. Stop the outgoing shell (config must only be written after it has stopped).
    // Both shells are stopped in both directions: a running plasmashell (even headless) may
    // rewrite its config on exit and clobber the restore (D13).
    QString err;
    if (plan.unitsOnly) {
        // Caelestia keeps running and no config is written, so nothing needs to stop before the logout.
        done(StepStopOutgoing, QStringLiteral("no shell stopped (only the mask changes)"));
    } else {
        if (toStock || req.repair) {   // a repair may have to restore Caelestia's config: quit it first, as for any restore
            if (!ctx.ops->stopCaelestiaGracefully(&out.warnings, &err)) {
                return fail(StepStopOutgoing, QStringLiteral("could not stop Caelestia: %1").arg(err));
            }
            ctx.ops->reloadOverviewEffect(&out.warnings);
        }
        if (before.plasmashellRunning || ctx.ops->unitState(kPlasmashellUnit).active()) {
            if (!ctx.ops->systemctl({QStringLiteral("stop"), kPlasmashellUnit}, &err) || !ctx.ops->waitInactive(kPlasmashellUnit, 15000)) {
                return fail(StepStopOutgoing, QStringLiteral("could not stop plasmashell: %1").arg(err));
            }
        }
        done(StepStopOutgoing, toStock ? QStringLiteral("Caelestia quit, plasmashell stopped") : QStringLiteral("plasmashell stopped"));
    }

    // 4. Helper units (best effort: problems are warnings).
    QStringList disabledNow;
    QStringList remembered = readLines(ctx.helpersFile);
    if (toStock) {
        for (const QString &u : kHelperUnits) {
            const UnitState us = ctx.ops->unitState(u);
            if (!us.queried || us.notFound() || (!us.enabled() && !us.active())) {
                continue;
            }
            const QStringList deps = ctx.ops->dependents(u, {QStringLiteral("RequiredBy"), QStringLiteral("BoundBy")});
            if (!deps.isEmpty()) {
                out.warnings << QStringLiteral("%1 left running, something depends on it: %2").arg(u, deps.join(QStringLiteral(", ")));
                continue;
            }
            QString e;
            if (ctx.ops->systemctl({QStringLiteral("disable"), QStringLiteral("--now"), u}, &e)) {
                disabledNow << u;
                if (!remembered.contains(u)) {
                    remembered << u;
                }
            } else {
                out.warnings << e;
            }
        }
        writeLines(ctx.helpersFile, remembered);
        st.helpers = disabledNow;
    } else {
        for (const QString &u : remembered) {
            QString e;
            if (!ctx.ops->systemctl({QStringLiteral("enable"), QStringLiteral("--now"), u}, &e)) {
                out.warnings << e;
            }
        }
        writeLines(ctx.helpersFile, {});
    }
    done(StepHelpers, QStringLiteral("helper units"));

    // 5. Restore the selected backup (not when only the units change: Caelestia is running, nothing to restore).
    if (plan.unitsOnly) {
        done(StepRestore, QStringLiteral("no config restore (only the mask changes)"));
    } else {
        const OpResult rest = restoreBackup(plan.targetRef, ctx.paths);
        if (!rest.ok) {
            const QString detail = rest.warnings.isEmpty() ? rest.error : rest.warnings.join(QStringLiteral("; "));
            return fail(StepRestore, QStringLiteral("restore of %1 failed: %2").arg(plan.targetRef, detail));
        }
        out.warnings << rest.warnings;
        done(StepRestore, QStringLiteral("restored %1").arg(plan.targetRef));
    }

    // 6. Unit changes.
    const UnitState plasmaUnit = ctx.ops->unitState(kPlasmashellUnit);
    if (toStock) {
        if (!ctx.ops->systemctl({QStringLiteral("disable"), kCaelestiaUnit}, &err)) {
            return fail(StepUnits, err);
        }
        if (plasmaUnit.masked() && !ctx.ops->systemctl({QStringLiteral("unmask"), kPlasmashellUnit}, &err)) {
            return fail(StepUnits, err);
        }
    } else {
        if (!ctx.ops->systemctl({QStringLiteral("enable"), kCaelestiaUnit}, &err)) {
            return fail(StepUnits, err);
        }
        if (req.maskPlasmashell) {
            const MaskCheck mc = checkMaskPlasmashell(ctx.ops);
            if (!mc.allowed()) {
                return fail(StepUnits, maskBlockedMessage(mc.blockers));
            }
            if (!plasmaUnit.masked() && !ctx.ops->systemctl({QStringLiteral("mask"), kPlasmashellUnit}, &err)) {
                return fail(StepUnits, err);
            }
        } else if (plasmaUnit.masked() && !ctx.ops->systemctl({QStringLiteral("unmask"), kPlasmashellUnit}, &err)) {
            return fail(StepUnits, err);
        }
    }
    done(StepUnits, QStringLiteral("unit changes"));

    // 7. Verify the configuration the next login will use.
    QStringList problems;
    const UnitState caelestiaNow = ctx.ops->unitState(kCaelestiaUnit);
    const UnitState plasmaNow = ctx.ops->unitState(kPlasmashellUnit);
    const QString shellPkg = ctx.ops->shellPackage();
    if (toStock) {
        if (caelestiaNow.enabled()) {
            problems << QStringLiteral("Caelestia is still enabled");
        }
        if (plasmaNow.masked()) {
            problems << QStringLiteral("plasmashell is still masked");
        }
        if (!isStockShellPackage(shellPkg)) {
            problems << QStringLiteral("ShellPackage is '%1', expected the stock shell").arg(shellPkg);
        }
    } else {
        if (!caelestiaNow.enabled()) {
            problems << QStringLiteral("Caelestia is not enabled");
        }
        if (shellPkg != QLatin1String("caelestia.desktop")) {
            problems << QStringLiteral("ShellPackage is '%1', expected caelestia.desktop").arg(shellPkg);
        }
        if (plasmaNow.masked() != req.maskPlasmashell) {
            problems << QStringLiteral("plasmashell mask state is not what was requested");
        }
    }
    if (!problems.isEmpty()) {
        return fail(StepVerify, problems.join(QStringLiteral("; ")));
    }
    done(StepVerify, QStringLiteral("verified"));

    // 8. Pending logout, then log out.
    st.mode = QStringLiteral("pending-logout");
    st.rolledBack = req.repair;
    done(StepLogout, QStringLiteral("waiting for logout"));
    if (req.logout) {
        if (!ctx.ops->logout(&err)) {
            st.error = err;
            st.failedStep = StepLogout;
            save();
            out.ok = false;
            out.error = QStringLiteral("%1; log out manually, then run 'finish'").arg(err);
            out.failedStep = StepLogout;
            return out;
        }
    }
    return out;
}

OpResult finishSwitch(const SwitchContext &ctx, int timeoutMs, int pollMs, QString *summary)
{
    SwitchState st;
    if (!readState(ctx.stateFile, &st) || st.mode != QLatin1String("pending-logout")) {
        if (summary) {
            *summary = QStringLiteral("nothing to finish");
        }
        return {};
    }
    QElapsedTimer t;
    t.start();
    QStringList problems;
    while (true) {
        problems = finalProblems(st, ctx.ops->readings());
        if (problems.isEmpty()) {
            break;
        }
        if (t.elapsed() > timeoutMs) {
            break;
        }
        QThread::msleep(static_cast<unsigned long>(pollMs));
    }

    if (problems.isEmpty()) {
        const bool toStock = st.direction == directionKey(Direction::ToStock);
        st.mode = toStock ? QStringLiteral("stock") : QStringLiteral("caelestia");
        st.result = st.rolledBack ? QStringLiteral("rolled-back") : QStringLiteral("done");
        st.error.clear();
        st.failedStep = 0;
        st.step = StepFinished;
        st.stepName = QStringLiteral("finished");
        st.unseen = true;
        writeState(ctx.stateFile, st);
        if (summary) {
            *summary = st.rolledBack ? rolledBackSummary(st.cause, toStock)
                : toStock ? QStringLiteral("Switch complete: you are now in stock Plasma mode.")
                          : QStringLiteral("Switch complete: you are now in Caelestia mode.");
        }
        note(ctx, QStringLiteral("finished: %1").arg(summary ? *summary : QString()));
        return {};
    }
    st.result = QStringLiteral("failed");
    st.error = problems.join(QStringLiteral("; "));
    st.unseen = true;
    writeState(ctx.stateFile, st);
    note(ctx, QStringLiteral("finish FAILED: %1").arg(st.error));
    return {false, QStringLiteral("the expected state was not reached: %1").arg(st.error), {}};
}

OpResult planRepair(const SwitchContext &ctx, RepairPlan *out)
{
    RepairPlan p;
    const auto fail = [](const QString &why) { return OpResult{false, why, {}}; };
    const auto give = [&]() {
        if (out) {
            *out = p;
        }
        return OpResult{};
    };

    const Readings r = ctx.ops->readings();
    if (!r.systemdReachable) {
        return fail(QStringLiteral("cannot query the systemd user manager: %1").arg(r.systemdError));
    }
    SwitchState st;
    const bool hasState = readState(ctx.stateFile, &st);
    const bool failedState = hasState
        && (st.mode == QLatin1String("transitioning")
            || (st.mode == QLatin1String("pending-logout") && (st.result == QLatin1String("failed") || !st.error.isEmpty())));
    const QStringList reasons = withoutStateReasons(r.inconsistentReasons);

    if (hasState && st.mode == QLatin1String("pending-logout") && !failedState) {
        p.message = QStringLiteral("Nothing to repair: the switch finished its steps and is waiting for the logout. "
                                   "Log out, then run 'finish'.");
        return give();
    }
    if (!failedState && reasons.isEmpty()) {
        p.message = QStringLiteral("Nothing to repair: the system is consistent and no failed switch is recorded.");
        return give();
    }

    // The side that was being left. Without a usable record the spec's fallback applies: stock Plasma.
    QString leaving = QStringLiteral("none");
    if (failedState) {
        leaving = !st.leaving.isEmpty() ? st.leaving
            : (st.direction == directionKey(Direction::ToStock) ? QStringLiteral("caelestia") : QStringLiteral("stock"));
        p.failure = describeFailure(st);
    } else {
        p.failure = QStringLiteral("No switch is recorded, but the system is inconsistent (%1)").arg(reasons.join(QStringLiteral("; ")));
    }
    if (failedState && st.failedStep == StepLogout) {
        p.warnings << QStringLiteral("All steps finished; only the logout call failed. If you just want the switch to complete, "
                                     "log out and run 'finish' instead of repairing.");
    }

    SwitchRequest req;
    req.repair = true;
    req.cause = p.failure;
    Side side = Side::Stock;
    if (leaving == QLatin1String("caelestia")) {
        req.direction = Direction::ToCaelestia;
        req.maskPlasmashell = failedState && st.plasmaWasMasked;
        side = Side::Caelestia;
    } else {
        req.direction = Direction::ToStock;
        if (leaving != QLatin1String("stock")) {
            p.warnings << QStringLiteral("There is no record of which side was being left, so the system is restored to stock Plasma.");
        }
    }

    // The switch may have rewritten config if it got as far as the restore step, or if this record already
    // belongs to an earlier repair (its progress was reset). Only meaningful when there is a snapshot to restore.
    req.configMayBeTouched = failedState && !st.snapshotRef.isEmpty()
        && (st.step >= StepHelpers || st.failedStep >= StepRestore || !st.cause.isEmpty());

    // The automatic snapshot of step 2, if it is still there; otherwise the newest backup of that side.
    if (failedState && !st.snapshotRef.isEmpty()) {
        bool found = false;
        const QList<BackupInfo> available = listBackups(side, ctx.paths);
        for (const BackupInfo &b : available) {
            found = found || b.ref() == st.snapshotRef;
        }
        if (found) {
            req.targetRef = st.snapshotRef;
        } else {
            p.warnings << QStringLiteral("The snapshot %1 is no longer available; using the newest %2 backup instead.")
                              .arg(st.snapshotRef, sideKey(side));
        }
    }

    SwitchPlan sp;
    const OpResult pre = preflight(req, ctx, /*checkState=*/false, &sp);
    if (!pre.ok) {
        return fail(pre.error);
    }
    p.warnings << sp.warnings;
    p.alreadyThere = sp.noop;
    const QString where = shellLabel(side == Side::Stock);
    if (sp.noop) {
        p.action = QStringLiteral("The system already matches %1 mode, so there is nothing to undo; only the record of the failed switch is closed.").arg(where);
    } else if (sp.unitsOnly) {
        p.action = QStringLiteral("Rolling back to %1 mode (only the plasmashell mask is changed back).").arg(where);
    } else {
        p.action = QStringLiteral("Rolling back to %1 mode using backup %2.").arg(where, sp.targetRef);
    }
    p.request = req;
    p.needed = true;
    return give();
}

QStringList runSwitchArguments(const SwitchRequest &req)
{
    QStringList args{QStringLiteral("run-switch"), QStringLiteral("--direction"), directionKey(req.direction)};
    if (!req.targetRef.isEmpty()) {
        args << QStringLiteral("--backup") << req.targetRef;
    }
    if (req.maskPlasmashell) {
        args << QStringLiteral("--mask");
    }
    if (!req.logout) {
        args << QStringLiteral("--no-logout");
    }
    if (req.repair) {
        args << QStringLiteral("--as-repair");
        if (req.configMayBeTouched) {
            args << QStringLiteral("--config-touched");   // without it the executor trusts the readings (found live 2026-10-04)
        }
    }
    return args;
}

QString postLoginUnitName()
{
    return QStringLiteral("caelestia-switch-post-login.service");
}

QString postLoginUnitText(const QString &exePath)
{
    QString exe = exePath;
    exe.replace(QLatin1Char('%'), QStringLiteral("%%"));   // systemd specifier escape
    return QStringLiteral("[Unit]\n"
                          "Description=Caelestia Switch: report the result of a switch after login\n"
                          "After=graphical-session.target\n"
                          "\n"
                          "[Service]\n"
                          "Type=oneshot\n"
                          "ExecStart=\"%1\" post-login\n"
                          "TimeoutStartSec=180\n"
                          "\n"
                          "[Install]\n"
                          "WantedBy=graphical-session.target\n")
        .arg(exe);
}

bool ensurePostLoginService(const SwitchContext &ctx, const QString &exePath, QString *error)
{
    const QString unit = postLoginUnitName();
    const QString dir = ctx.paths.configHome + QStringLiteral("/systemd/user");
    const QString path = dir + QLatin1Char('/') + unit;
    const QString wanted = postLoginUnitText(exePath);

    const bool same = readTextFile(path) == wanted;
    if (!same) {
        QDir().mkpath(dir);
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) {
                *error = QStringLiteral("cannot write %1; the result of the switch will not be reported after login").arg(path);
            }
            return false;
        }
        f.write(wanted.toUtf8());
        if (!f.commit()) {
            if (error) {
                *error = QStringLiteral("cannot write %1; the result of the switch will not be reported after login").arg(path);
            }
            return false;
        }
    }
    if (same && ctx.ops->unitState(unit).enabled()) {
        return true;
    }
    QString err;
    if (!ctx.ops->systemctl({QStringLiteral("enable"), unit}, &err)) {
        if (error) {
            *error = QStringLiteral("could not enable %1: %2").arg(unit, err);
        }
        return false;
    }
    return true;
}

PostLoginResult runPostLogin(const SwitchContext &ctx, int timeoutMs, int pollMs)
{
    PostLoginResult r;
    SwitchState st;
    if (!readState(ctx.stateFile, &st) || st.mode != QLatin1String("pending-logout")) {
        return r;   // nothing was waiting for this login
    }
    r.ran = true;
    QString summary;
    const OpResult res = finishSwitch(ctx, timeoutMs, pollMs, &summary);
    if (res.ok) {
        SwitchState after;
        readState(ctx.stateFile, &after);
        r.title = after.rolledBack ? QStringLiteral("Switch rolled back") : QStringLiteral("Switch complete");
        r.body = summary;
        return r;
    }
    r.ok = false;
    r.title = QStringLiteral("Switch did not complete");
    QString why = res.error.trimmed();
    while (why.endsWith(QLatin1Char('.'))) {
        why.chop(1);
    }
    r.body = QStringLiteral("%1. Run 'caelestia-switch repair' in a terminal to go back to the side you left.").arg(why);
    return r;
}

OpResult launchSwitch(const SwitchRequest &req, const SwitchContext &ctx, const QString &exePath)
{
    SwitchPlan plan;
    const OpResult pre = preflight(req, ctx, /*checkState=*/true, &plan);
    if (!pre.ok) {
        return pre;
    }
    if (plan.noop) {
        return {true, {}, {plan.message}};
    }

    // The service that reports the result after the next login. Best effort: a switch must not fail because of it.
    QString serviceErr;
    if (!ensurePostLoginService(ctx, exePath, &serviceErr)) {
        note(ctx, QStringLiteral("warning: %1").arg(serviceErr));
    }

    SwitchState previous;
    const bool hadState = readState(ctx.stateFile, &previous);
    SwitchState st;
    if (req.repair && hadState) {
        st = previous;   // keep leaving / plasmaWasMasked / cause for the executor
        if (st.cause.isEmpty()) {
            st.cause = describeFailure(previous);
        }
    }
    if (req.repair && !req.cause.isEmpty()) {
        st.cause = req.cause;
    }
    st.mode = QStringLiteral("transitioning");
    st.direction = directionKey(req.direction);
    st.targetRef = plan.targetRef;
    st.maskPlasmashell = req.maskPlasmashell;
    st.logout = req.logout;
    st.step = 0;
    st.stepName.clear();
    if (!req.repair) {
        st.snapshotRef.clear();
    }
    st.error.clear();
    st.result.clear();
    st.failedStep = 0;
    st.rolledBack = false;
    QString err;
    if (!writeState(ctx.stateFile, st, &err)) {
        return {false, err, {}};
    }

    QStringList args{QStringLiteral("--user"), QStringLiteral("--unit=caelestia-switch-run"), QStringLiteral("--collect"),
                     QStringLiteral("--quiet")};
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
        args << QStringLiteral("--setenv=WAYLAND_DISPLAY");
    }
    args << exePath << runSwitchArguments(req);

    QProcess p;
    p.start(QStringLiteral("systemd-run"), args);
    if (!p.waitForStarted(3000) || !p.waitForFinished(15000) || p.exitCode() != 0) {
        const QString why = QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (hadState) {
            writeState(ctx.stateFile, previous);
        } else {
            QFile::remove(ctx.stateFile);
        }
        return {false, QStringLiteral("could not start the switch service: %1").arg(why), {}};
    }
    return {};
}

} // namespace cs
