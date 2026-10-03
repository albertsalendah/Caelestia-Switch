#include "switch.h"

#include "fsutil.h"
#include "plasmaconfig.h"
#include "state.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
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
    p.leaving = toStock ? Side::Caelestia : Side::Stock;
    p.target = toStock ? Side::Stock : Side::Caelestia;
    const auto fail = [](const QString &why) { return OpResult{false, why, {}}; };

    SwitchState st;
    if (checkState && readState(ctx.stateFile, &st)
        && (st.mode == QLatin1String("transitioning") || st.mode == QLatin1String("pending-logout"))) {
        return fail(QStringLiteral("a switch is already in progress or waiting for the logout (mode=%1); "
                                   "use 'finish' after logging in, or 'repair'").arg(st.mode));
    }

    const Readings r = ctx.ops->readings();
    if (!r.systemdReachable) {
        return fail(QStringLiteral("cannot query the systemd user manager: %1").arg(r.systemdError));
    }
    const QStringList reasons = checkState ? r.inconsistentReasons : withoutStateReasons(r.inconsistentReasons);
    if (!reasons.isEmpty()) {
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
    if (p.noop) {
        if (plan) {
            *plan = p;
        }
        return {};
    }

    if (!toStock && !r.install.installed) {
        return fail(QStringLiteral("Caelestia is not installed"));
    }

    // Target backup: the one asked for, else the newest of the target side.
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
    st.mode = QStringLiteral("transitioning");
    st.direction = directionKey(req.direction);
    st.maskPlasmashell = req.maskPlasmashell;
    st.logout = req.logout;
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
        st.error = why;
        st.result = QStringLiteral("failed");
        st.step = step - 1;
        save();
        out.ok = false;
        out.error = why;
        out.failedStep = step;
        note(ctx, QStringLiteral("step %1 FAILED: %2").arg(step).arg(why));
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
        st.result = QStringLiteral("done");
        st.step = StepFinished;
        save();
        out.noop = true;
        return out;
    }
    st.targetRef = plan.targetRef;
    out.warnings << plan.warnings;
    const Readings before = ctx.ops->readings();
    done(StepPreflight, QStringLiteral("pre-flight"));

    // 2. Automatic snapshot of the mode being left.
    QString snapRef;
    const OpResult snap = createBackup(plan.leaving, QStringLiteral("auto-leave"), ctx.paths, &snapRef);
    if (!snap.ok) {
        return fail(StepSnapshot, QStringLiteral("snapshot failed: %1").arg(snap.error));
    }
    st.snapshotRef = snapRef;
    done(StepSnapshot, QStringLiteral("snapshot %1").arg(snapRef));

    // 3. Stop the outgoing shell (config must only be written after it has stopped).
    // Both shells are stopped in both directions: a running plasmashell (even headless) may
    // rewrite its config on exit and clobber the restore (D13).
    QString err;
    if (toStock) {
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

    // 5. Restore the selected backup.
    const OpResult rest = restoreBackup(plan.targetRef, ctx.paths);
    if (!rest.ok) {
        return fail(StepRestore, QStringLiteral("restore of %1 failed: %2 %3").arg(plan.targetRef, rest.error, rest.warnings.join(QStringLiteral("; "))));
    }
    out.warnings << rest.warnings;
    done(StepRestore, QStringLiteral("restored %1").arg(plan.targetRef));

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
    done(StepLogout, QStringLiteral("waiting for logout"));
    if (req.logout) {
        if (!ctx.ops->logout(&err)) {
            st.error = err;
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
        st.result = QStringLiteral("done");
        st.error.clear();
        st.step = StepFinished;
        st.stepName = QStringLiteral("finished");
        st.unseen = true;
        writeState(ctx.stateFile, st);
        if (summary) {
            *summary = toStock ? QStringLiteral("Switch complete: you are now in stock Plasma mode.")
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

    SwitchState previous;
    const bool hadState = readState(ctx.stateFile, &previous);
    SwitchState st;
    st.mode = QStringLiteral("transitioning");
    st.direction = directionKey(req.direction);
    st.targetRef = plan.targetRef;
    st.maskPlasmashell = req.maskPlasmashell;
    st.logout = req.logout;
    QString err;
    if (!writeState(ctx.stateFile, st, &err)) {
        return {false, err, {}};
    }

    QStringList args{QStringLiteral("--user"), QStringLiteral("--unit=caelestia-switch-run"), QStringLiteral("--collect"),
                     QStringLiteral("--quiet")};
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
        args << QStringLiteral("--setenv=WAYLAND_DISPLAY");
    }
    args << exePath << QStringLiteral("run-switch") << QStringLiteral("--direction") << directionKey(req.direction);
    if (!req.targetRef.isEmpty()) {
        args << QStringLiteral("--backup") << req.targetRef;
    }
    if (req.maskPlasmashell) {
        args << QStringLiteral("--mask");
    }
    if (!req.logout) {
        args << QStringLiteral("--no-logout");
    }

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
