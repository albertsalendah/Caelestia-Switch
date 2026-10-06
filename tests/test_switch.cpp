#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "backup.h"
#include "consistency.h"
#include "plasmaconfig.h"
#include "readings.h"
#include "screens.h"
#include "state.h"
#include "switch.h"

using namespace cs;

namespace {

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(content.toUtf8());
}

QString readFileText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

UnitState unit(const QString &load, const QString &active, const QString &sub, const QString &fileState)
{
    UnitState u;
    u.queried = true;
    u.loadState = load;
    u.activeState = active;
    u.subState = sub;
    u.unitFileState = fileState;
    return u;
}

Readings finish(Readings r)
{
    r.provider = computeProvider(r);
    r.inconsistentReasons = findInconsistencies(r);
    return r;
}

// Caelestia mode: Caelestia active; plasmashell headless (or masked and stopped).
Readings caelestiaMode(bool plasmaMasked = false)
{
    Readings r;
    r.systemdReachable = true;
    r.caelestiaUnit = unit("loaded", "active", "running", "enabled");
    r.quickshellRunning = true;
    r.shellPackage = QStringLiteral("caelestia.desktop");
    r.install.installed = true;
    if (plasmaMasked) {
        r.plasmashellUnit = unit("masked", "inactive", "dead", "masked");
    } else {
        r.plasmashellUnit = unit("loaded", "active", "running", "static");
        r.plasmashellRunning = true;
    }
    return finish(r);
}

Readings stockMode()
{
    Readings r;
    r.systemdReachable = true;
    r.caelestiaUnit = unit("loaded", "inactive", "dead", "disabled");
    r.plasmashellUnit = unit("loaded", "active", "running", "static");
    r.plasmashellRunning = true;
    r.install.installed = true;
    return finish(r);
}

class FakeOps : public SwitchOps
{
public:
    Readings r;
    QString configHome;
    QMap<QString, UnitState> units;
    QMap<QString, QStringList> deps;
    QStringList commands;
    QStringList failOn;      // exact "systemctl ..." strings that fail
    QString failSuffix = QStringLiteral(" failed");   // appended to the command to form the error text
    bool gracefulOk = true;
    bool logoutOk = true;

    FakeOps(const Readings &readings, const QString &cfgHome) : r(readings), configHome(cfgHome)
    {
        units[QStringLiteral("caelestia-shell.service")] = r.caelestiaUnit;
        units[QStringLiteral("plasma-plasmashell.service")] = r.plasmashellUnit;
        units[QStringLiteral("cliphist.service")] = unit("loaded", "active", "running", "enabled");
    }

    Readings readings() override { return r; }
    UnitState unitState(const QString &u) override { return units.value(u, UnitState{true, QStringLiteral("not-found"), QStringLiteral("inactive"), QStringLiteral("dead"), QString()}); }
    QString shellPackage() override { return readShellPackage(configHome); }

    bool systemctl(const QStringList &args, QString *error) override
    {
        const QString cmd = QStringLiteral("systemctl ") + args.join(QLatin1Char(' '));
        commands << cmd;
        if (failOn.contains(cmd)) {
            if (error) {
                *error = cmd + failSuffix;
            }
            return false;
        }
        const QString verb = args.value(0);
        const QString target = args.last();
        UnitState &u = units[target];
        u.queried = true;
        if (verb == QLatin1String("enable")) {
            u.unitFileState = QStringLiteral("enabled");
            if (args.contains(QStringLiteral("--now"))) u.activeState = QStringLiteral("active");
        } else if (verb == QLatin1String("disable")) {
            u.unitFileState = QStringLiteral("disabled");
            if (args.contains(QStringLiteral("--now"))) u.activeState = QStringLiteral("inactive");
        } else if (verb == QLatin1String("mask")) {
            u.loadState = QStringLiteral("masked");
            u.unitFileState = QStringLiteral("masked");
        } else if (verb == QLatin1String("unmask")) {
            u.loadState = QStringLiteral("loaded");
            u.unitFileState = QStringLiteral("static");
        } else if (verb == QLatin1String("stop")) {
            u.activeState = QStringLiteral("inactive");
        }
        return true;
    }

    QStringList dependents(const QString &u, const QStringList &) override { return deps.value(u); }
    bool waitInactive(const QString &u, int) override { return !units.value(u).active(); }

    bool stopCaelestiaGracefully(QStringList *, QString *error) override
    {
        commands << QStringLiteral("quit-caelestia");
        if (!gracefulOk) {
            if (error) *error = QStringLiteral("quit failed");
            return false;
        }
        units[QStringLiteral("caelestia-shell.service")].activeState = QStringLiteral("inactive");
        return true;
    }
    void reloadOverviewEffect(QStringList *) override { commands << QStringLiteral("reload-overview"); }
    bool logout(QString *error) override
    {
        commands << QStringLiteral("logout");
        if (!logoutOk && error) *error = QStringLiteral("no logout");
        return logoutOk;
    }

    int indexOf(const QString &cmd) const { return commands.indexOf(cmd); }
};

struct Env {
    QTemporaryDir dir;
    SwitchContext ctx;
    Env()
    {
        ctx.paths.home = dir.path();
        ctx.paths.configHome = dir.path() + QStringLiteral("/.config");
        ctx.paths.dataRoot = dir.path() + QStringLiteral("/data/backups");
        ctx.paths.appConfigDir = dir.path() + QStringLiteral("/appcfg");
        ctx.paths.lookupPlasmaVersion = false;
        ctx.stateFile = dir.path() + QStringLiteral("/appcfg/state");
        ctx.helpersFile = dir.path() + QStringLiteral("/appcfg/helpers");
    }
    QString cfg(const QString &rel) const { return ctx.paths.configHome + QLatin1Char('/') + rel; }
    QString configHome() const { return ctx.paths.configHome; }
    void setShellPackage(const QString &pkg)
    {
        writeFile(cfg("plasmashellrc"), pkg.isEmpty() ? QStringLiteral("[Other]\nx=1\n") : QStringLiteral("[Shell]\nShellPackage=%1\n").arg(pkg));
    }
    QString backup(Side side)
    {
        QString ref;
        const OpResult res = createBackup(side, QStringLiteral("manual"), ctx.paths, &ref);
        if (!res.ok) {
            QTest::qFail(qPrintable(res.error), __FILE__, __LINE__);
        }
        return ref;
    }
    SwitchState state() const
    {
        SwitchState s;
        readState(ctx.stateFile, &s);
        return s;
    }
};

// Neither shell running: what a switch that stopped half-way (or an interrupted one) leaves behind.
Readings halfSwitched(const QString &caelestiaFileState = QStringLiteral("enabled"))
{
    Readings r;
    r.systemdReachable = true;
    r.caelestiaUnit = unit("loaded", "inactive", "dead", caelestiaFileState);
    r.plasmashellUnit = unit("loaded", "inactive", "dead", "static");
    r.install.installed = true;
    return finish(r);
}

// A to-stock switch that fails in step 6 (disable Caelestia); returns the state file it leaves.
SwitchState makeFailedToStock(Env &e)
{
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    FakeOps broken(caelestiaMode(), e.configHome());
    broken.failOn << QStringLiteral("systemctl disable caelestia-shell.service");
    e.ctx.ops = &broken;
    SwitchRequest req;
    req.direction = Direction::ToStock;
    runSwitch(req, e.ctx);
    return e.state();
}

} // namespace

class TestSwitch : public QObject
{
    Q_OBJECT

private slots:
    void stateRoundTrip();
    void toStockHappyPath();
    void noLogoutOption();
    void toCaelestiaWithMask();
    void toCaelestiaUnmasksPlasmashell();
    void maskChangeInCaelestiaModeOnlyChangesUnits();
    void maskChangeRefusesBackupRef();
    void noShellRunningSkipsSnapshot();
    void noopCases();
    void preflightRejections();
    void maskRefusedWithDependents();
    void maskAllowedWithWeakDependents();
    void maskDependentsClassified();
    void unitStepFailureStopsBeforeLogout();
    void gracefulQuitFailure();
    void finishSucceeds();
    void finishTimesOut();
    void finishNothingToDo();
    void repairRollsBackFailedSwitch();
    void repairInterruptedSwitch();
    void repairWithoutStateFallsBackToStock();
    void repairNothingToUndo();
    void repairRefusals();
    void repairUndoesMaskOnlyChange();
    void repairFailureStartsTheShell();
    void repairDoesNotTrustReadingsAfterPartialRestore();
    void repairSafetyNetTimeout();
    void executorArgumentsCarryEveryFlag();
    void postLoginUnitText();
    void postLoginServiceIsInstalledOnce();
    void postLoginServiceFailuresAreReported();
    void postLoginReportsOutcome();
    void notificationRetryOnlyWhenNoServer();
    void describeResultCases();
    void markSeenClearsUnseen();
    void guiBinaryLookup();
    void screenBlockedCases();
    void screenNeedBackup();
    void screenSwitchFromCaelestia();
    void screenSwitchFromStock();
    void screenMaskDependents();
    void repairFailureSentenceHasNoTrailingPeriod();
};

void TestSwitch::stateRoundTrip()
{
    QTemporaryDir d;
    const QString path = d.path() + QStringLiteral("/s/state");
    SwitchState s;
    s.mode = QStringLiteral("pending-logout");
    s.direction = QStringLiteral("to-caelestia");
    s.step = 8;
    s.stepName = QStringLiteral("waiting for logout");
    s.targetRef = QStringLiteral("caelestia/20261002_073602");
    s.snapshotRef = QStringLiteral("stock/20261002_075632");
    s.maskPlasmashell = true;
    s.logout = false;
    s.helpers = {QStringLiteral("a.service"), QStringLiteral("b.service")};
    s.error = QStringLiteral("line one\nline two");
    s.result = QStringLiteral("failed");
    s.unseen = true;
    s.leaving = QStringLiteral("caelestia");
    s.plasmaWasMasked = true;
    s.failedStep = 6;
    s.cause = QStringLiteral("it broke\nhere");
    s.rolledBack = true;
    QVERIFY(writeState(path, s));

    SwitchState t;
    QVERIFY(readState(path, &t));
    QCOMPARE(t.mode, s.mode);
    QCOMPARE(t.step, 8);
    QCOMPARE(t.targetRef, s.targetRef);
    QCOMPARE(t.snapshotRef, s.snapshotRef);
    QVERIFY(t.maskPlasmashell);
    QVERIFY(!t.logout);
    QCOMPARE(t.helpers, s.helpers);
    QCOMPARE(t.error, QStringLiteral("line one line two"));
    QVERIFY(t.unseen);
    QCOMPARE(t.leaving, QStringLiteral("caelestia"));
    QVERIFY(t.plasmaWasMasked);
    QCOMPARE(t.failedStep, 6);
    QCOMPARE(t.cause, QStringLiteral("it broke here"));
    QVERIFY(t.rolledBack);

    // `status` reads the first line leniently: it must stay "mode=...".
    QCOMPARE(parseStateMode(readFileText(path)), QStringLiteral("pending-logout"));

    QVERIFY(!readState(d.path() + QStringLiteral("/missing"), &t));
    writeFile(d.path() + QStringLiteral("/bare"), QStringLiteral("stock\n"));
    QVERIFY(readState(d.path() + QStringLiteral("/bare"), &t));
    QCOMPARE(t.mode, QStringLiteral("stock"));
}

void TestSwitch::toStockHappyPath()
{
    Env e;
    e.setShellPackage(QString());                         // stock: ShellPackage unset
    const QString stockRef = e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));  // live state: Caelestia
    FakeOps ops(caelestiaMode(), e.configHome());
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToStock;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY2(out.ok, qPrintable(out.error));

    // Order: quit gracefully, reload the corner, helper off, Caelestia disabled, logout last.
    const int quit = ops.indexOf(QStringLiteral("quit-caelestia"));
    const int reload = ops.indexOf(QStringLiteral("reload-overview"));
    const int stopPlasma = ops.indexOf(QStringLiteral("systemctl stop plasma-plasmashell.service"));
    QVERIFY(reload < stopPlasma);                           // plasmashell is stopped before any config is written
    const int helper = ops.indexOf(QStringLiteral("systemctl disable --now cliphist.service"));
    const int disable = ops.indexOf(QStringLiteral("systemctl disable caelestia-shell.service"));
    const int logout = ops.indexOf(QStringLiteral("logout"));
    QVERIFY(quit >= 0 && quit < reload);
    QVERIFY(stopPlasma < helper && helper < disable);
    QCOMPARE(logout, ops.commands.size() - 1);

    // Config was restored from the stock snapshot, after the quit.
    QVERIFY(isStockShellPackage(readShellPackage(e.configHome())));

    // An automatic snapshot of the side we left exists.
    QCOMPARE(listBackups(Side::Caelestia, e.ctx.paths).size(), 1);

    const SwitchState st = e.state();
    QCOMPARE(st.mode, QStringLiteral("pending-logout"));
    QCOMPARE(st.step, 8);
    QCOMPARE(st.targetRef, stockRef);
    QVERIFY(st.snapshotRef.startsWith(QStringLiteral("caelestia/")));
    QCOMPARE(st.helpers, QStringList{QStringLiteral("cliphist.service")});
    QVERIFY(QFileInfo::exists(e.ctx.helpersFile));
}

void TestSwitch::noLogoutOption()
{
    Env e;
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    FakeOps ops(caelestiaMode(), e.configHome());
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToStock;
    req.logout = false;
    QVERIFY(runSwitch(req, e.ctx).ok);
    QVERIFY(ops.indexOf(QStringLiteral("logout")) < 0);
    QCOMPARE(e.state().mode, QStringLiteral("pending-logout"));
    QVERIFY(!e.state().logout);
}

void TestSwitch::toCaelestiaWithMask()
{
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    const QString caeRef = e.backup(Side::Caelestia);
    e.setShellPackage(QString());                          // live state: stock
    writeFile(e.ctx.helpersFile, QStringLiteral("cliphist.service\n"));  // we disabled it on the way out
    FakeOps ops(stockMode(), e.configHome());
    ops.units[QStringLiteral("cliphist.service")] = unit("loaded", "inactive", "dead", "disabled");
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToCaelestia;
    req.maskPlasmashell = true;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY2(out.ok, qPrintable(out.error));

    const int stop = ops.indexOf(QStringLiteral("systemctl stop plasma-plasmashell.service"));
    const int helper = ops.indexOf(QStringLiteral("systemctl enable --now cliphist.service"));
    const int enable = ops.indexOf(QStringLiteral("systemctl enable caelestia-shell.service"));
    const int mask = ops.indexOf(QStringLiteral("systemctl mask plasma-plasmashell.service"));
    QVERIFY(stop >= 0 && stop < helper);
    QVERIFY(helper < enable && enable < mask);
    QCOMPARE(ops.indexOf(QStringLiteral("logout")), ops.commands.size() - 1);
    QCOMPARE(readShellPackage(e.configHome()), QStringLiteral("caelestia.desktop"));
    QVERIFY(!QFileInfo::exists(e.ctx.helpersFile));
    QCOMPARE(e.state().targetRef, caeRef);
    QVERIFY(e.state().snapshotRef.startsWith(QStringLiteral("stock/")));
    QVERIFY(e.state().maskPlasmashell);
}

void TestSwitch::toCaelestiaUnmasksPlasmashell()
{
    // Caelestia mode with plasmashell masked; "on" without the checkbox unmasks it. Not a no-op.
    // Only the unit changes: no backup is needed, nothing is snapshotted or restored, no shell is stopped.
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    FakeOps ops(caelestiaMode(/*plasmaMasked=*/true), e.configHome());
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToCaelestia;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY2(out.ok && !out.noop, qPrintable(out.error));
    QVERIFY(ops.indexOf(QStringLiteral("systemctl unmask plasma-plasmashell.service")) >= 0);
    QVERIFY(ops.indexOf(QStringLiteral("systemctl stop plasma-plasmashell.service")) < 0);  // was not running
    QVERIFY(ops.indexOf(QStringLiteral("quit-caelestia")) < 0);
    QCOMPARE(listBackups(e.ctx.paths).size(), 0);
}

void TestSwitch::maskChangeInCaelestiaModeOnlyChangesUnits()
{
    // Already in Caelestia mode (plasmashell headless), "on --mask" must not snapshot a side that
    // is not the one running, must not restore config into the live shell, and must not stop anything.
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    writeFile(e.cfg("kscreenlockerrc"), QStringLiteral("[Greeter]\nTheme=live\n"));
    FakeOps ops(caelestiaMode(), e.configHome());
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToCaelestia;
    req.maskPlasmashell = true;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY2(out.ok && !out.noop, qPrintable(out.error));

    QVERIFY(ops.indexOf(QStringLiteral("systemctl mask plasma-plasmashell.service")) >= 0);
    QVERIFY(ops.indexOf(QStringLiteral("quit-caelestia")) < 0);
    QVERIFY(ops.indexOf(QStringLiteral("systemctl stop plasma-plasmashell.service")) < 0);
    QVERIFY(ops.indexOf(QStringLiteral("systemctl stop caelestia-shell.service")) < 0);
    QCOMPARE(ops.indexOf(QStringLiteral("logout")), ops.commands.size() - 1);   // the logout stays
    QCOMPARE(listBackups(e.ctx.paths).size(), 0);                                // no snapshot of either side
    QVERIFY(readFileText(e.cfg("kscreenlockerrc")).contains(QStringLiteral("Theme=live")));   // config untouched

    const SwitchState st = e.state();
    QCOMPARE(st.mode, QStringLiteral("pending-logout"));
    QCOMPARE(st.step, int(StepLogout));
    QVERIFY(st.targetRef.isEmpty());
    QVERIFY(st.snapshotRef.isEmpty());
    QVERIFY(st.maskPlasmashell);
}

void TestSwitch::maskChangeRefusesBackupRef()
{
    // A backup cannot be restored into the running Caelestia, so asking for one is refused, not ignored.
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    const QString ref = e.backup(Side::Caelestia);
    FakeOps ops(caelestiaMode(), e.configHome());
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToCaelestia;
    req.maskPlasmashell = true;
    req.targetRef = ref;
    SwitchPlan plan;
    const OpResult r = preflight(req, e.ctx, true, &plan);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("no effect")));
    QVERIFY(ops.commands.isEmpty());
}

void TestSwitch::noShellRunningSkipsSnapshot()
{
    // Caelestia disabled, plasmashell headless: neither shell draws panels (provider none), so
    // there is no side to snapshot and none is guessed. The switch itself still works.
    Env e;
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    Readings r;
    r.systemdReachable = true;
    r.caelestiaUnit = unit("loaded", "inactive", "dead", "disabled");
    r.plasmashellUnit = unit("loaded", "active", "running", "static");
    r.plasmashellRunning = true;
    r.shellPackage = QStringLiteral("caelestia.desktop");
    r.install.installed = true;
    r = finish(r);
    QCOMPARE(r.provider, Provider::None);
    QVERIFY(!r.inconsistent());
    FakeOps ops(r, e.configHome());
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToStock;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(listBackups(Side::Caelestia, e.ctx.paths).size(), 0);
    QCOMPARE(listBackups(Side::Stock, e.ctx.paths).size(), 1);        // only the one made above
    QVERIFY(e.state().snapshotRef.isEmpty());
    QVERIFY(out.warnings.join(QLatin1Char(';')).contains(QStringLiteral("snapshot")));
    QVERIFY(isStockShellPackage(readShellPackage(e.configHome())));   // the restore still happened
}

void TestSwitch::noopCases()
{
    Env e;
    FakeOps stock(stockMode(), e.configHome());
    e.ctx.ops = &stock;
    SwitchRequest toStock;
    toStock.direction = Direction::ToStock;
    SwitchPlan plan;
    QVERIFY(preflight(toStock, e.ctx, true, &plan).ok);
    QVERIFY(plan.noop);

    FakeOps cae(caelestiaMode(), e.configHome());
    e.ctx.ops = &cae;
    SwitchRequest toCae;
    toCae.direction = Direction::ToCaelestia;
    QVERIFY(preflight(toCae, e.ctx, true, &plan).ok);
    QVERIFY(plan.noop);
    toCae.maskPlasmashell = true;   // different mask request: not a no-op, but only the units change (no backup needed)
    QVERIFY(preflight(toCae, e.ctx, true, &plan).ok);
    QVERIFY(!plan.noop && plan.unitsOnly);

    // runSwitch on a no-op settles the state file and changes nothing.
    toCae.maskPlasmashell = false;
    const SwitchOutcome out = runSwitch(toCae, e.ctx);
    QVERIFY(out.ok && out.noop);
    QCOMPARE(e.state().mode, QStringLiteral("caelestia"));
    QVERIFY(cae.commands.isEmpty());
}

void TestSwitch::preflightRejections()
{
    Env e;
    SwitchPlan plan;
    SwitchRequest toStock;
    toStock.direction = Direction::ToStock;

    // No stock backup yet.
    FakeOps cae(caelestiaMode(), e.configHome());
    e.ctx.ops = &cae;
    OpResult r = preflight(toStock, e.ctx, true, &plan);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("no stock-side backup")));

    // A named backup that does not exist.
    e.setShellPackage(QString());
    const QString ref = e.backup(Side::Stock);
    toStock.targetRef = QStringLiteral("stock/20000101_000000");
    QVERIFY(!preflight(toStock, e.ctx, true, &plan).ok);
    toStock.targetRef = ref;
    QVERIFY(preflight(toStock, e.ctx, true, &plan).ok);
    QCOMPARE(plan.targetRef, ref);

    // A backup of the wrong side is not accepted.
    toStock.targetRef = e.backup(Side::Caelestia);
    QVERIFY(!preflight(toStock, e.ctx, true, &plan).ok);
    toStock.targetRef.clear();

    // Inconsistent system.
    Readings bad = caelestiaMode();
    bad.plasmashellUnit = unit("masked", "active", "running", "masked");
    FakeOps broken(finish(bad), e.configHome());
    e.ctx.ops = &broken;
    r = preflight(toStock, e.ctx, true, &plan);
    QVERIFY(!r.ok && r.error.contains(QStringLiteral("inconsistent")));

    // A switch already marked in progress.
    e.ctx.ops = &cae;
    SwitchState st;
    st.mode = QStringLiteral("transitioning");
    QVERIFY(writeState(e.ctx.stateFile, st));
    r = preflight(toStock, e.ctx, true, &plan);
    QVERIFY(!r.ok && r.error.contains(QStringLiteral("already in progress")));
    QVERIFY(preflight(toStock, e.ctx, /*checkState=*/false, &plan).ok);   // the executor itself

    // Caelestia not installed.
    Readings none = stockMode();
    none.install.installed = false;
    FakeOps noCae(none, e.configHome());
    e.ctx.ops = &noCae;
    SwitchRequest toCae;
    toCae.direction = Direction::ToCaelestia;
    QVERIFY(preflight(toCae, e.ctx, false, &plan).error.contains(QStringLiteral("not installed")));
}

void TestSwitch::maskRefusedWithDependents()
{
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    e.backup(Side::Caelestia);
    FakeOps ops(stockMode(), e.configHome());
    e.ctx.ops = &ops;

    // Each strong relation blocks, even next to a weak one; unreadable answers count as strong.
    const QStringList strong = {QStringLiteral("RequiredBy=something.service"), QStringLiteral("RequisiteOf=something.service"),
                                QStringLiteral("BoundBy=something.service"), QStringLiteral("(could not query plasma-plasmashell.service: no bus)")};
    for (const QString &dep : strong) {
        ops.deps[QStringLiteral("plasma-plasmashell.service")] = {QStringLiteral("WantedBy=plasma-core.target"), dep};
        ops.commands.clear();
        SwitchRequest req;
        req.direction = Direction::ToCaelestia;
        req.maskPlasmashell = true;
        const SwitchOutcome out = runSwitch(req, e.ctx);
        QVERIFY2(!out.ok, qPrintable(dep));
        QCOMPARE(out.failedStep, int(StepPreflight));
        QVERIFY(out.error.contains(QStringLiteral("refusing to mask")));
        QVERIFY(out.error.contains(dep));
        QVERIFY(!out.error.contains(QStringLiteral("WantedBy")));   // the weak one is not named as a blocker
        QVERIFY(ops.commands.isEmpty());                   // nothing was touched
        QCOMPARE(e.state().result, QStringLiteral("failed"));
    }
}

void TestSwitch::maskAllowedWithWeakDependents()
{
    // Live finding 2026-10-02: WantedBy=plasma-core.target is always present and must not block masking.
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    e.backup(Side::Caelestia);
    FakeOps ops(stockMode(), e.configHome());
    ops.deps[QStringLiteral("plasma-plasmashell.service")] = {QStringLiteral("WantedBy=plasma-core.target")};
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToCaelestia;
    req.maskPlasmashell = true;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY2(out.ok, qPrintable(out.error));
    QVERIFY(ops.indexOf(QStringLiteral("systemctl mask plasma-plasmashell.service")) >= 0);
    const QStringList w = out.warnings;
    QVERIFY(w.filter(QStringLiteral("plasma-core.target")).size() == 1);   // warned once, not at every check
    QVERIFY(e.state().maskPlasmashell);
}

void TestSwitch::maskDependentsClassified()
{
    Env e;
    FakeOps ops(stockMode(), e.configHome());

    MaskCheck none = checkMaskPlasmashell(&ops);
    QVERIFY(none.allowed() && none.weak.isEmpty());

    ops.deps[QStringLiteral("plasma-plasmashell.service")] = {QStringLiteral("WantedBy=plasma-core.target")};
    MaskCheck weak = checkMaskPlasmashell(&ops);
    QVERIFY(weak.allowed());
    QCOMPARE(weak.weak, QStringList{QStringLiteral("WantedBy=plasma-core.target")});

    ops.deps[QStringLiteral("plasma-plasmashell.service")] = {QStringLiteral("WantedBy=plasma-core.target"), QStringLiteral("BoundBy=x.service")};
    MaskCheck both = checkMaskPlasmashell(&ops);
    QVERIFY(!both.allowed());
    QCOMPARE(both.blockers, QStringList{QStringLiteral("BoundBy=x.service")});
    QCOMPARE(both.weak.size(), 1);

    // The same weak relation does not stop a run that is not masking at all.
    Readings r = stockMode();
    FakeOps plain(r, e.configHome());
    plain.deps[QStringLiteral("plasma-plasmashell.service")] = {QStringLiteral("RequiredBy=x.service")};
    e.ctx.ops = &plain;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    e.backup(Side::Caelestia);
    SwitchRequest req;
    req.direction = Direction::ToCaelestia;
    SwitchPlan plan;
    QVERIFY(preflight(req, e.ctx, false, &plan).ok);
}

void TestSwitch::unitStepFailureStopsBeforeLogout()
{
    Env e;
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    FakeOps ops(caelestiaMode(), e.configHome());
    ops.failOn << QStringLiteral("systemctl disable caelestia-shell.service");
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToStock;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY(!out.ok);
    QCOMPARE(out.failedStep, int(StepUnits));
    QVERIFY(ops.indexOf(QStringLiteral("logout")) < 0);
    const SwitchState st = e.state();
    QCOMPARE(st.mode, QStringLiteral("transitioning"));    // left as is, for repair
    QCOMPARE(st.step, int(StepRestore));                   // last completed step
    QCOMPARE(st.result, QStringLiteral("failed"));
    QVERIFY(!st.error.isEmpty());
}

void TestSwitch::gracefulQuitFailure()
{
    Env e;
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    FakeOps ops(caelestiaMode(), e.configHome());
    ops.gracefulOk = false;
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToStock;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY(!out.ok);
    QCOMPARE(out.failedStep, int(StepStopOutgoing));
    // Nothing was written to the config: the live ShellPackage is still Caelestia's.
    QCOMPARE(readShellPackage(e.configHome()), QStringLiteral("caelestia.desktop"));
    QVERIFY(ops.indexOf(QStringLiteral("logout")) < 0);
}

void TestSwitch::finishSucceeds()
{
    Env e;
    FakeOps ops(caelestiaMode(), e.configHome());
    e.ctx.ops = &ops;
    SwitchState st;
    st.mode = QStringLiteral("pending-logout");
    st.direction = QStringLiteral("to-caelestia");
    st.step = 8;
    QVERIFY(writeState(e.ctx.stateFile, st));

    QString summary;
    const OpResult r = finishSwitch(e.ctx, 200, 10, &summary);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(summary.contains(QStringLiteral("Caelestia mode")));
    const SwitchState after = e.state();
    QCOMPARE(after.mode, QStringLiteral("caelestia"));
    QCOMPARE(after.result, QStringLiteral("done"));
    QVERIFY(after.unseen);
    QCOMPARE(after.step, int(StepFinished));

    // And to stock.
    FakeOps stock(stockMode(), e.configHome());
    e.ctx.ops = &stock;
    st.direction = QStringLiteral("to-stock");
    QVERIFY(writeState(e.ctx.stateFile, st));
    QVERIFY(finishSwitch(e.ctx, 200, 10, &summary).ok);
    QCOMPARE(e.state().mode, QStringLiteral("stock"));
}

void TestSwitch::finishTimesOut()
{
    Env e;
    FakeOps ops(stockMode(), e.configHome());          // still stock, but we expected Caelestia
    e.ctx.ops = &ops;
    SwitchState st;
    st.mode = QStringLiteral("pending-logout");
    st.direction = QStringLiteral("to-caelestia");
    QVERIFY(writeState(e.ctx.stateFile, st));

    QString summary;
    const OpResult r = finishSwitch(e.ctx, 60, 10, &summary);
    QVERIFY(!r.ok);
    const SwitchState after = e.state();
    QCOMPARE(after.mode, QStringLiteral("pending-logout"));   // not closed out
    QCOMPARE(after.result, QStringLiteral("failed"));
    QVERIFY(after.error.contains(QStringLiteral("Caelestia")));
}

void TestSwitch::finishNothingToDo()
{
    Env e;
    FakeOps ops(stockMode(), e.configHome());
    e.ctx.ops = &ops;
    QString summary;
    QVERIFY(finishSwitch(e.ctx, 50, 10, &summary).ok);
    QCOMPARE(summary, QStringLiteral("nothing to finish"));
}

void TestSwitch::repairRollsBackFailedSwitch()
{
    Env e;
    const SwitchState failed = makeFailedToStock(e);
    QCOMPARE(failed.mode, QStringLiteral("transitioning"));
    QCOMPARE(failed.leaving, QStringLiteral("caelestia"));
    QCOMPARE(failed.failedStep, int(StepUnits));
    QVERIFY(failed.snapshotRef.startsWith(QStringLiteral("caelestia/")));
    QVERIFY(isStockShellPackage(readShellPackage(e.configHome())));      // the stock config was already restored

    // The system as the failed switch left it: both shells stopped, helper disabled.
    FakeOps fixer(halfSwitched(), e.configHome());
    fixer.units[QStringLiteral("cliphist.service")] = unit("loaded", "inactive", "dead", "disabled");
    e.ctx.ops = &fixer;

    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(plan.needed && !plan.alreadyThere);
    QVERIFY(plan.request.repair);
    QCOMPARE(plan.request.direction, Direction::ToCaelestia);          // back to the side that was left
    QCOMPARE(plan.request.targetRef, failed.snapshotRef);
    QVERIFY(!plan.request.maskPlasmashell);
    QVERIFY(plan.failure.contains(QStringLiteral("failed at step 6")));
    QVERIFY(plan.failure.contains(QStringLiteral("unit changes")));
    QVERIFY(plan.failure.contains(QStringLiteral("systemctl disable caelestia-shell.service failed")));   // the cause
    QVERIFY(plan.action.contains(failed.snapshotRef));

    const int backupsBefore = listBackups(e.ctx.paths).size();
    const SwitchOutcome out = runSwitch(plan.request, e.ctx);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(readShellPackage(e.configHome()), QStringLiteral("caelestia.desktop"));
    QVERIFY(fixer.indexOf(QStringLiteral("quit-caelestia")) >= 0);
    QVERIFY(fixer.indexOf(QStringLiteral("systemctl enable --now cliphist.service")) >= 0);
    QVERIFY(fixer.indexOf(QStringLiteral("systemctl enable caelestia-shell.service")) >= 0);
    QCOMPARE(fixer.indexOf(QStringLiteral("logout")), fixer.commands.size() - 1);
    QCOMPARE(listBackups(e.ctx.paths).size(), backupsBefore);          // a half-switched state is never snapshotted
    SwitchState st = e.state();
    QCOMPARE(st.mode, QStringLiteral("pending-logout"));
    QVERIFY(st.rolledBack);
    QVERIFY(st.cause.contains(QStringLiteral("failed at step 6")));

    // After the login, `finish` says it was a rollback and why.
    FakeOps back(caelestiaMode(), e.configHome());
    e.ctx.ops = &back;
    QString summary;
    QVERIFY(finishSwitch(e.ctx, 200, 10, &summary).ok);
    QVERIFY(summary.startsWith(QStringLiteral("Rolled back")));
    QVERIFY(summary.contains(QStringLiteral("failed at step 6")));
    QVERIFY(summary.contains(QStringLiteral("Caelestia mode")));
    st = e.state();
    QCOMPARE(st.mode, QStringLiteral("caelestia"));
    QCOMPARE(st.result, QStringLiteral("rolled-back"));
    QVERIFY(st.unseen && st.rolledBack && st.error.isEmpty());
}

void TestSwitch::repairInterruptedSwitch()
{
    // The executor was killed after step 4 of a switch to Caelestia: no error and no result were recorded.
    Env e;
    e.setShellPackage(QString());
    const QString stockRef = e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    SwitchState st;
    st.mode = QStringLiteral("transitioning");
    st.direction = QStringLiteral("to-caelestia");
    st.step = 4;
    st.stepName = QStringLiteral("helper units");
    st.leaving = QStringLiteral("stock");
    st.snapshotRef = stockRef;
    QVERIFY(writeState(e.ctx.stateFile, st));
    FakeOps ops(halfSwitched(), e.configHome());
    e.ctx.ops = &ops;

    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(plan.needed);
    QVERIFY2(plan.failure.contains(QStringLiteral("interrupted after step 4")), qPrintable(plan.failure));
    QVERIFY(plan.failure.contains(QStringLiteral("no failure was recorded")));
    QCOMPARE(plan.request.direction, Direction::ToStock);              // back to stock, the side that was left
    QCOMPARE(plan.request.targetRef, stockRef);

    const SwitchOutcome out = runSwitch(plan.request, e.ctx);
    QVERIFY2(out.ok, qPrintable(out.error));
    QVERIFY(isStockShellPackage(readShellPackage(e.configHome())));
    QVERIFY(ops.indexOf(QStringLiteral("systemctl disable caelestia-shell.service")) >= 0);
    QCOMPARE(ops.indexOf(QStringLiteral("logout")), ops.commands.size() - 1);
}

void TestSwitch::repairWithoutStateFallsBackToStock()
{
    Env e;
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    FakeOps ops(halfSwitched("disabled"), e.configHome());             // inconsistent, and there is no state file
    e.ctx.ops = &ops;

    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(plan.needed);
    QVERIFY(plan.failure.contains(QStringLiteral("No switch is recorded")));
    QVERIFY(plan.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("no record")));
    QCOMPARE(plan.request.direction, Direction::ToStock);
    QVERIFY2(runSwitch(plan.request, e.ctx).ok, "fallback repair");
    QVERIFY(isStockShellPackage(readShellPackage(e.configHome())));

    // Without a stock backup there is nothing to restore from: refused with the reason.
    Env e2;
    FakeOps ops2(halfSwitched("disabled"), e2.configHome());
    e2.ctx.ops = &ops2;
    RepairPlan plan2;
    const OpResult r = planRepair(e2.ctx, &plan2);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("no stock-side backup")));
}

void TestSwitch::repairNothingToUndo()
{
    // The executor failed in its own pre-flight (step 1): nothing was changed, only the record is stale.
    Env e;
    FakeOps ops(caelestiaMode(), e.configHome());
    e.ctx.ops = &ops;
    SwitchState st;
    st.mode = QStringLiteral("transitioning");
    st.direction = QStringLiteral("to-stock");
    st.leaving = QStringLiteral("caelestia");
    st.error = QStringLiteral("no stock-side backup exists");
    st.result = QStringLiteral("failed");
    st.failedStep = StepPreflight;
    QVERIFY(writeState(e.ctx.stateFile, st));

    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(plan.needed && plan.alreadyThere);
    QVERIFY(plan.failure.contains(QStringLiteral("failed at step 1")));
    const SwitchOutcome out = runSwitch(plan.request, e.ctx);
    QVERIFY(out.ok && out.noop);
    QVERIFY(ops.commands.isEmpty());
    const SwitchState after = e.state();
    QCOMPARE(after.mode, QStringLiteral("caelestia"));
    QCOMPARE(after.result, QStringLiteral("rolled-back"));
    QVERIFY(after.rolledBack && after.unseen);
    QVERIFY(after.cause.contains(QStringLiteral("failed at step 1")));
}

void TestSwitch::repairRefusals()
{
    Env e;
    FakeOps waiting(halfSwitched(), e.configHome());
    e.ctx.ops = &waiting;
    SwitchState st;
    st.mode = QStringLiteral("pending-logout");                        // the normal wait for the logout
    st.direction = QStringLiteral("to-stock");
    st.step = 8;
    QVERIFY(writeState(e.ctx.stateFile, st));
    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(!plan.needed);
    QVERIFY(plan.message.contains(QStringLiteral("finish")));

    Env e2;                                                            // consistent system, nothing recorded
    FakeOps fine(caelestiaMode(), e2.configHome());
    e2.ctx.ops = &fine;
    RepairPlan plan2;
    QVERIFY(planRepair(e2.ctx, &plan2).ok);
    QVERIFY(!plan2.needed);
    QVERIFY(plan2.message.contains(QStringLiteral("Nothing to repair")));
}

void TestSwitch::repairUndoesMaskOnlyChange()
{
    // A mask-only switch masked plasmashell, then the logout call failed. Repair puts the mask back,
    // restores nothing and snapshots nothing (Caelestia is running).
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    FakeOps ops(caelestiaMode(/*plasmaMasked=*/true), e.configHome());
    e.ctx.ops = &ops;
    SwitchState st;
    st.mode = QStringLiteral("pending-logout");
    st.direction = QStringLiteral("to-caelestia");
    st.maskPlasmashell = true;
    st.leaving = QStringLiteral("caelestia");
    st.plasmaWasMasked = false;
    st.step = 8;
    st.error = QStringLiteral("no logout");
    st.failedStep = StepLogout;
    QVERIFY(writeState(e.ctx.stateFile, st));

    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(plan.needed && !plan.alreadyThere);
    QCOMPARE(plan.request.direction, Direction::ToCaelestia);
    QVERIFY(!plan.request.maskPlasmashell);
    QVERIFY(plan.request.targetRef.isEmpty());
    QVERIFY(plan.action.contains(QStringLiteral("mask")));
    QVERIFY(plan.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("finish")));   // "just log out and finish" hint

    const SwitchOutcome out = runSwitch(plan.request, e.ctx);
    QVERIFY2(out.ok, qPrintable(out.error));
    QVERIFY(ops.indexOf(QStringLiteral("systemctl unmask plasma-plasmashell.service")) >= 0);
    QVERIFY(ops.indexOf(QStringLiteral("quit-caelestia")) < 0);
    QCOMPARE(listBackups(e.ctx.paths).size(), 0);
    QCOMPARE(ops.indexOf(QStringLiteral("logout")), ops.commands.size() - 1);
}

void TestSwitch::repairFailureStartsTheShell()
{
    // If the rollback itself fails, the session must not be left without a shell, the original cause
    // must survive, and the repair can simply be run again.
    Env e;
    const SwitchState failed = makeFailedToStock(e);
    FakeOps fixer(halfSwitched(), e.configHome());
    fixer.failOn << QStringLiteral("systemctl enable caelestia-shell.service");
    e.ctx.ops = &fixer;

    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    const SwitchOutcome out = runSwitch(plan.request, e.ctx);
    QVERIFY(!out.ok);
    QCOMPARE(out.failedStep, int(StepUnits));
    QVERIFY(fixer.indexOf(QStringLiteral("systemctl start caelestia-shell.service")) >= 0);
    QVERIFY(out.error.contains(QStringLiteral("started caelestia-shell.service")));
    QVERIFY(fixer.indexOf(QStringLiteral("logout")) < 0);
    SwitchState st = e.state();
    QCOMPARE(st.result, QStringLiteral("failed"));
    QVERIFY(!st.rolledBack);
    QVERIFY(st.cause.contains(QStringLiteral("systemctl disable caelestia-shell.service failed")));   // the original cause

    // Second attempt: the plan still names the original failure and the original snapshot (even though a
    // newer backup of that side exists meanwhile), and now succeeds.
    e.backup(Side::Caelestia);
    fixer.failOn.clear();
    RepairPlan again;
    QVERIFY(planRepair(e.ctx, &again).ok);
    QVERIFY(again.needed);
    QVERIFY(again.failure.contains(QStringLiteral("failed at step 6")));
    QCOMPARE(again.request.targetRef, failed.snapshotRef);
    QVERIFY2(runSwitch(again.request, e.ctx).ok, "second repair");
    QVERIFY(e.state().rolledBack);
}

void TestSwitch::repairDoesNotTrustReadingsAfterPartialRestore()
{
    // Live finding 2026-10-04: a switch failed in the config restore (step 5) after replacing some files, then
    // plasmashell was running with the stock ShellPackage, so the readings said "stock" while the config was
    // half Caelestia's. The snapshot must still be restored, also on a later repair of a failed repair.
    Env e;
    e.setShellPackage(QString());
    writeFile(e.cfg("kscreenlockerrc"), QStringLiteral("[Greeter]\nTheme=stock\n"));
    const QString stockRef = e.backup(Side::Stock);
    writeFile(e.cfg("kscreenlockerrc"), QStringLiteral("[Greeter]\nTheme=half\n"));   // replaced before the failure
    FakeOps ops(stockMode(), e.configHome());                                            // plasmashell runs, stock package
    e.ctx.ops = &ops;

    SwitchState st;
    st.mode = QStringLiteral("transitioning");
    st.direction = QStringLiteral("to-caelestia");
    st.step = 4;
    st.leaving = QStringLiteral("stock");
    st.snapshotRef = stockRef;
    st.error = QStringLiteral("restore failed: cannot restore plasmashellrc");
    st.result = QStringLiteral("failed");
    st.failedStep = StepRestore;
    QVERIFY(writeState(e.ctx.stateFile, st));

    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(plan.needed);
    QVERIFY(!plan.alreadyThere);                                       // not trusted: the config may be half-restored
    QVERIFY(plan.request.configMayBeTouched);
    QCOMPARE(plan.request.targetRef, stockRef);
    QVERIFY2(runSwitch(plan.request, e.ctx).ok, "rollback");
    QVERIFY(readFileText(e.cfg("kscreenlockerrc")).contains(QStringLiteral("Theme=stock")));
    QVERIFY(ops.indexOf(QStringLiteral("systemctl stop plasma-plasmashell.service")) >= 0);

    // A failure that happened before the restore step changed nothing, so the same readings do mean "nothing to undo".
    Env e2;
    e2.setShellPackage(QString());
    const QString ref2 = e2.backup(Side::Stock);
    FakeOps ops2(stockMode(), e2.configHome());
    e2.ctx.ops = &ops2;
    SwitchState early;
    early.mode = QStringLiteral("transitioning");
    early.direction = QStringLiteral("to-caelestia");
    early.step = 2;
    early.leaving = QStringLiteral("stock");
    early.snapshotRef = ref2;
    early.error = QStringLiteral("could not stop plasmashell");
    early.result = QStringLiteral("failed");
    early.failedStep = StepStopOutgoing;
    QVERIFY(writeState(e2.ctx.stateFile, early));
    RepairPlan plan2;
    QVERIFY(planRepair(e2.ctx, &plan2).ok);
    QVERIFY(plan2.alreadyThere && !plan2.request.configMayBeTouched);

    // But once a repair has been attempted (cause recorded), progress was reset, so it is not trusted either.
    early.cause = QStringLiteral("The last switch to Caelestia failed at step 3 (stopping the outgoing shell): x");
    early.step = 0;
    QVERIFY(writeState(e2.ctx.stateFile, early));
    RepairPlan plan3;
    QVERIFY(planRepair(e2.ctx, &plan3).ok);
    QVERIFY(!plan3.alreadyThere && plan3.request.configMayBeTouched);
}

void TestSwitch::repairSafetyNetTimeout()
{
    // `systemctl start` timing out (the shell waits on an error dialog) must not be reported as "could not start".
    Env e;
    makeFailedToStock(e);
    FakeOps fixer(halfSwitched(), e.configHome());
    fixer.failSuffix = QStringLiteral(": systemctl timed out");
    fixer.failOn << QStringLiteral("systemctl enable caelestia-shell.service")
                 << QStringLiteral("systemctl start caelestia-shell.service");
    e.ctx.ops = &fixer;
    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    const SwitchOutcome out = runSwitch(plan.request, e.ctx);
    QVERIFY(!out.ok);
    QVERIFY2(out.error.contains(QStringLiteral("asked systemd to start caelestia-shell.service")), qPrintable(out.error));
    QVERIFY(!out.error.contains(QStringLiteral("could not start")));
}

void TestSwitch::executorArgumentsCarryEveryFlag()
{
    // Live finding 2026-10-04: the executor runs as a separate process and rebuilds its request from these
    // arguments. `configMayBeTouched` was missing, so the first fixed build still closed the record as a no-op.
    SwitchRequest repair;
    repair.direction = Direction::ToStock;
    repair.targetRef = QStringLiteral("stock/20261004_071518");
    repair.logout = false;
    repair.repair = true;
    repair.configMayBeTouched = true;
    const QStringList a = runSwitchArguments(repair);
    QCOMPARE(a.first(), QStringLiteral("run-switch"));
    QVERIFY(a.contains(QStringLiteral("--as-repair")));
    QVERIFY(a.contains(QStringLiteral("--config-touched")));
    QVERIFY(a.contains(QStringLiteral("--no-logout")));
    QVERIFY(a.indexOf(QStringLiteral("--backup")) >= 0 && a.at(a.indexOf(QStringLiteral("--backup")) + 1) == QLatin1String("stock/20261004_071518"));
    QVERIFY(a.indexOf(QStringLiteral("--direction")) >= 0 && a.at(a.indexOf(QStringLiteral("--direction")) + 1) == QLatin1String("to-stock"));

    SwitchRequest normal;
    normal.direction = Direction::ToCaelestia;
    normal.maskPlasmashell = true;
    const QStringList b = runSwitchArguments(normal);
    QVERIFY(b.contains(QStringLiteral("--mask")));
    QVERIFY(!b.contains(QStringLiteral("--as-repair")) && !b.contains(QStringLiteral("--config-touched")) && !b.contains(QStringLiteral("--no-logout")));
}

void TestSwitch::postLoginUnitText()
{
    const QString t = cs::postLoginUnitText(QStringLiteral("/home/u/Caelestia-Switch/build/src/cli/caelestia-switch"));
    QVERIFY(t.contains(QStringLiteral("ExecStart=\"/home/u/Caelestia-Switch/build/src/cli/caelestia-switch\" post-login")));
    QVERIFY(t.contains(QStringLiteral("Type=oneshot")));
    QVERIFY(t.contains(QStringLiteral("WantedBy=graphical-session.target")));
    QVERIFY(t.contains(QStringLiteral("After=graphical-session.target")));
    // A '%' in the path must not be read as a systemd specifier.
    QVERIFY(cs::postLoginUnitText(QStringLiteral("/a%b/x")).contains(QStringLiteral("/a%%b/x")));
    QCOMPARE(cs::postLoginUnitName(), QStringLiteral("caelestia-switch-post-login.service"));
}

void TestSwitch::postLoginServiceIsInstalledOnce()
{
    Env e;
    FakeOps ops(stockMode(), e.configHome());
    e.ctx.ops = &ops;
    const QString unitFile = e.cfg("systemd/user/caelestia-switch-post-login.service");
    const QString enableCmd = QStringLiteral("systemctl enable caelestia-switch-post-login.service");
    QString err;

    // First time: written and enabled.
    QVERIFY2(ensurePostLoginService(e.ctx, QStringLiteral("/opt/cs/caelestia-switch"), &err), qPrintable(err));
    QCOMPARE(readFileText(unitFile), cs::postLoginUnitText(QStringLiteral("/opt/cs/caelestia-switch")));
    QCOMPARE(ops.commands.count(enableCmd), 1);

    // Second time, same binary, already enabled: nothing happens.
    ops.commands.clear();
    QVERIFY(ensurePostLoginService(e.ctx, QStringLiteral("/opt/cs/caelestia-switch"), &err));
    QVERIFY(ops.commands.isEmpty());

    // The binary moved: the unit is rewritten and enabled again.
    QVERIFY(ensurePostLoginService(e.ctx, QStringLiteral("/elsewhere/caelestia-switch"), &err));
    QVERIFY(readFileText(unitFile).contains(QStringLiteral("/elsewhere/caelestia-switch")));
    QCOMPARE(ops.commands.count(enableCmd), 1);

    // The file is right but the unit got disabled meanwhile: enabled again, file untouched.
    ops.commands.clear();
    ops.units[QStringLiteral("caelestia-switch-post-login.service")].unitFileState = QStringLiteral("disabled");
    QVERIFY(ensurePostLoginService(e.ctx, QStringLiteral("/elsewhere/caelestia-switch"), &err));
    QCOMPARE(ops.commands, QStringList{enableCmd});
}

void TestSwitch::postLoginServiceFailuresAreReported()
{
    // The unit cannot be written: <configHome>/systemd is a file, so the directory cannot be created.
    Env e;
    FakeOps ops(stockMode(), e.configHome());
    e.ctx.ops = &ops;
    writeFile(e.cfg("systemd"), QStringLiteral("not a directory\n"));
    QString err;
    QVERIFY(!ensurePostLoginService(e.ctx, QStringLiteral("/opt/cs/caelestia-switch"), &err));
    QVERIFY(err.contains(QStringLiteral("cannot write")));
    QVERIFY(ops.commands.isEmpty());

    // `systemctl enable` fails.
    Env e2;
    FakeOps ops2(stockMode(), e2.configHome());
    ops2.failOn << QStringLiteral("systemctl enable caelestia-switch-post-login.service");
    e2.ctx.ops = &ops2;
    QVERIFY(!ensurePostLoginService(e2.ctx, QStringLiteral("/opt/cs/caelestia-switch"), &err));
    QVERIFY(err.contains(QStringLiteral("could not enable")));
}

void TestSwitch::postLoginReportsOutcome()
{
    // No state file, or a settled mode: nothing was waiting for this login, nothing is reported.
    Env e;
    FakeOps stock(stockMode(), e.configHome());
    e.ctx.ops = &stock;
    PostLoginResult r = runPostLogin(e.ctx, 100, 10);
    QVERIFY(!r.ran);
    SwitchState settled;
    settled.mode = QStringLiteral("stock");
    QVERIFY(writeState(e.ctx.stateFile, settled));
    QVERIFY(!runPostLogin(e.ctx, 100, 10).ran);
    QCOMPARE(e.state().mode, QStringLiteral("stock"));

    // A switch to Caelestia that reached its final state: closed out, reported as complete, left unseen for the GUI.
    FakeOps cae(caelestiaMode(), e.configHome());
    e.ctx.ops = &cae;
    SwitchState waiting;
    waiting.mode = QStringLiteral("pending-logout");
    waiting.direction = QStringLiteral("to-caelestia");
    waiting.step = 8;
    QVERIFY(writeState(e.ctx.stateFile, waiting));
    r = runPostLogin(e.ctx, 200, 10);
    QVERIFY(r.ran && r.ok);
    QCOMPARE(r.title, QStringLiteral("Switch complete"));
    QVERIFY(r.body.contains(QStringLiteral("Caelestia mode")));
    QCOMPARE(r.body, QStringLiteral("You are now in Caelestia mode."));   // the title is not repeated in the text
    QCOMPARE(e.state().mode, QStringLiteral("caelestia"));
    QVERIFY(e.state().unseen);

    // A rollback after a repair is reported as such, with the cause.
    waiting.direction = QStringLiteral("to-caelestia");
    waiting.rolledBack = true;
    waiting.cause = QStringLiteral("The last switch to stock Plasma failed at step 6 (unit changes): x");
    QVERIFY(writeState(e.ctx.stateFile, waiting));
    r = runPostLogin(e.ctx, 200, 10);
    QVERIFY(r.ran && r.ok);
    QCOMPARE(r.title, QStringLiteral("Switch rolled back"));
    QVERIFY(r.body.startsWith(QStringLiteral("The last switch to stock Plasma failed at step 6")));   // cause first, no "Rolled back:" twice
    QVERIFY(r.body.endsWith(QStringLiteral("You are back in Caelestia mode.")));

    // The expected state was not reached: reported as a failure with the way out; the record stays open.
    FakeOps wrong(stockMode(), e.configHome());          // still stock, but a switch to Caelestia was expected
    e.ctx.ops = &wrong;
    SwitchState bad;
    bad.mode = QStringLiteral("pending-logout");
    bad.direction = QStringLiteral("to-caelestia");
    QVERIFY(writeState(e.ctx.stateFile, bad));
    r = runPostLogin(e.ctx, 60, 10);
    QVERIFY(r.ran && !r.ok);
    QCOMPARE(r.title, QStringLiteral("Switch did not complete"));
    QVERIFY(r.body.contains(QStringLiteral("repair")));
    QVERIFY(!r.body.contains(QStringLiteral("..")));
    QCOMPARE(e.state().mode, QStringLiteral("pending-logout"));
}

void TestSwitch::notificationRetryOnlyWhenNoServer()
{
    // Live finding 2026-10-05: a slow notification server answered late, the call timed out, the retry sent the
    // message again and Caelestia showed it three times. Only "no server yet" may be retried.
    QVERIFY(notificationErrorIsRetryable(QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown")));
    QVERIFY(notificationErrorIsRetryable(QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner")));
    QVERIFY(!notificationErrorIsRetryable(QStringLiteral("org.freedesktop.DBus.Error.NoReply")));      // timeout
    QVERIFY(!notificationErrorIsRetryable(QStringLiteral("org.freedesktop.DBus.Error.Timeout")));
    QVERIFY(!notificationErrorIsRetryable(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied")));
    QVERIFY(!notificationErrorIsRetryable(QString()));
}

void TestSwitch::describeResultCases()
{
    // Settled and unseen: complete.
    SwitchState done;
    done.mode = QStringLiteral("caelestia");
    done.direction = QStringLiteral("to-caelestia");
    done.result = QStringLiteral("done");
    done.unseen = true;
    ResultView v = describeResult(done);
    QVERIFY(v.present);
    QCOMPARE(v.kind, QStringLiteral("done"));
    QCOMPARE(v.title, QStringLiteral("Switch complete"));
    QCOMPARE(v.text, QStringLiteral("You are now in Caelestia mode."));
    done.mode = QStringLiteral("stock");
    QCOMPARE(describeResult(done).text, QStringLiteral("You are now in stock Plasma mode."));

    // Already seen: nothing to show. An unseen settled state without a known result: nothing either.
    done.unseen = false;
    QVERIFY(!describeResult(done).present);
    SwitchState odd;
    odd.mode = QStringLiteral("stock");
    odd.unseen = true;
    QVERIFY(!describeResult(odd).present);

    // Rolled back: the cause first, then where the system is.
    SwitchState rb;
    rb.mode = QStringLiteral("stock");
    rb.result = QStringLiteral("rolled-back");
    rb.rolledBack = true;
    rb.unseen = true;
    rb.cause = QStringLiteral("The last switch to Caelestia failed at step 5 (config restore): cannot restore x.");
    v = describeResult(rb);
    QVERIFY(v.present);
    QCOMPARE(v.kind, QStringLiteral("rolled-back"));
    QCOMPARE(v.title, QStringLiteral("Switch rolled back"));
    QCOMPARE(v.text, QStringLiteral("The last switch to Caelestia failed at step 5 (config restore): cannot restore x. You are back in stock Plasma mode."));
    rb.cause.clear();
    QCOMPARE(describeResult(rb).text, QStringLiteral("The switch did not complete. You are back in stock Plasma mode."));

    // The normal wait for the logout is not a result.
    SwitchState wait;
    wait.mode = QStringLiteral("pending-logout");
    wait.direction = QStringLiteral("to-stock");
    wait.step = 8;
    QVERIFY(!describeResult(wait).present);

    // The login finished but the expected state was not reached.
    wait.result = QStringLiteral("failed");
    wait.error = QStringLiteral("Caelestia is not providing the bar yet");
    wait.unseen = true;
    v = describeResult(wait);
    QVERIFY(v.present);
    QCOMPARE(v.kind, QStringLiteral("failed"));
    QCOMPARE(v.title, QStringLiteral("Switch did not complete"));
    QVERIFY(v.text.contains(QStringLiteral("expected state was not reached")));
    QVERIFY(v.text.contains(QStringLiteral("Caelestia is not providing the bar yet")));
    QVERIFY(v.text.contains(QStringLiteral("repair")));
    QVERIFY(!v.text.contains(QStringLiteral("..")));

    // A step failed: shown even though `unseen` is false, with the way out.
    SwitchState failed;
    failed.mode = QStringLiteral("transitioning");
    failed.direction = QStringLiteral("to-stock");
    failed.result = QStringLiteral("failed");
    failed.error = QStringLiteral("boom");
    failed.failedStep = StepUnits;
    v = describeResult(failed);
    QVERIFY(v.present);
    QCOMPARE(v.title, QStringLiteral("Switch failed"));
    QVERIFY(v.text.startsWith(QStringLiteral("The last switch to stock Plasma failed at step 6 (unit changes): boom.")));
    QVERIFY(v.text.contains(QStringLiteral("repair")));

    // Interrupted (or still running): no failure recorded.
    SwitchState cut;
    cut.mode = QStringLiteral("transitioning");
    cut.direction = QStringLiteral("to-caelestia");
    cut.step = 4;
    cut.stepName = QStringLiteral("helper units");
    v = describeResult(cut);
    QVERIFY(v.present);
    QCOMPARE(v.title, QStringLiteral("Switch not finished"));
    QVERIFY(v.text.contains(QStringLiteral("interrupted after step 4")));
}

void TestSwitch::markSeenClearsUnseen()
{
    Env e;
    QVERIFY(markSeen(e.ctx.stateFile).ok);              // no state file: nothing to do
    QVERIFY(!QFileInfo::exists(e.ctx.stateFile));

    SwitchState st;
    st.mode = QStringLiteral("caelestia");
    st.direction = QStringLiteral("to-caelestia");
    st.result = QStringLiteral("done");
    st.unseen = true;
    QVERIFY(writeState(e.ctx.stateFile, st));
    QVERIFY(markSeen(e.ctx.stateFile).ok);
    SwitchState after = e.state();
    QVERIFY(!after.unseen);
    QCOMPARE(after.mode, QStringLiteral("caelestia"));   // everything else is kept
    QCOMPARE(after.result, QStringLiteral("done"));
    QVERIFY(!describeResult(after).present);              // and the window has nothing left to show
    QVERIFY(markSeen(e.ctx.stateFile).ok);                // idempotent
}

void TestSwitch::guiBinaryLookup()
{
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", "/nonexistent-for-the-test");
    const auto makeExe = [](const QString &path, bool exec) {
        writeFile(path, QStringLiteral("#!/bin/sh\n"));
        QFile::setPermissions(path, exec ? QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                         : QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    };

    QTemporaryDir d;
    const QString cli = d.path() + QStringLiteral("/bin");
    QDir().mkpath(cli);
    QVERIFY(findGuiBinary(cli).isEmpty());

    // Not executable: ignored.
    makeExe(d.path() + QStringLiteral("/gui/caelestia-switch-gui"), false);
    QVERIFY(findGuiBinary(cli).isEmpty());

    // The build-tree layout (../gui/ next to the cli directory).
    makeExe(d.path() + QStringLiteral("/gui/caelestia-switch-gui"), true);
    QCOMPARE(findGuiBinary(cli), d.path() + QStringLiteral("/gui/caelestia-switch-gui"));

    // The installed layout (next to the CLI) wins.
    makeExe(cli + QStringLiteral("/caelestia-switch-gui"), true);
    QCOMPARE(findGuiBinary(cli), cli + QStringLiteral("/caelestia-switch-gui"));

    qputenv("PATH", oldPath);
}

void TestSwitch::screenBlockedCases()
{
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    e.backup(Side::Caelestia);
    FakeOps ops(caelestiaMode(), e.configHome());

    // systemd cannot be queried.
    Readings bad;
    bad.systemdError = QStringLiteral("no bus");
    ScreenModel m = buildScreenModel(finish(bad), e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Blocked);
    QVERIFY(m.message.contains(QStringLiteral("no bus")));
    QVERIFY(m.message.contains(QStringLiteral("repair")));

    // Inconsistent: plasmashell masked but running.
    Readings masked = caelestiaMode();
    masked.plasmashellUnit = unit("masked", "active", "running", "masked");
    m = buildScreenModel(finish(masked), e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Blocked);
    QVERIFY(m.message.contains(QStringLiteral("masked but still running")));

    // A switch that is waiting for its logout is not a state to switch from.
    Readings waiting = caelestiaMode();
    waiting.switchStateMode = QStringLiteral("pending-logout");
    m = buildScreenModel(finish(waiting), e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Blocked);

    // Caelestia not installed.
    Readings none = stockMode();
    none.install.installed = false;
    m = buildScreenModel(none, e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Blocked);
    QVERIFY(m.message.contains(QStringLiteral("not installed")));

    // Neither shell draws panels (valid, but the direction is unclear).
    Readings noProvider = stockMode();
    noProvider.caelestiaUnit = unit("loaded", "inactive", "dead", "disabled");
    noProvider.shellPackage = QStringLiteral("caelestia.desktop");
    noProvider = finish(noProvider);
    QCOMPARE(noProvider.provider, Provider::None);
    m = buildScreenModel(noProvider, e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Blocked);
    QVERIFY(m.message.contains(QStringLiteral("caelestia-switch on")));
}

void TestSwitch::screenNeedBackup()
{
    // No Caelestia-side backup while Caelestia runs: Screen A with a working Backup button.
    Env e;
    FakeOps cae(caelestiaMode(), e.configHome());
    ScreenModel m = buildScreenModel(caelestiaMode(), e.ctx.paths, &cae);
    QCOMPARE(m.kind, ScreenModel::Kind::NeedBackup);
    QVERIFY(m.canBackup);

    // The same in stock mode: the button is not offered (it would file the stock config under the Caelestia name).
    FakeOps stock(stockMode(), e.configHome());
    m = buildScreenModel(stockMode(), e.ctx.paths, &stock);
    QCOMPARE(m.kind, ScreenModel::Kind::NeedBackup);
    QVERIFY(!m.canBackup);
    QVERIFY(m.message.contains(QStringLiteral("running shell")));

    // A stock-side backup alone does not skip Screen A.
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    QCOMPARE(buildScreenModel(caelestiaMode(), e.ctx.paths, &cae).kind, ScreenModel::Kind::NeedBackup);
}

void TestSwitch::screenSwitchFromCaelestia()
{
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    const QString caeRef = e.backup(Side::Caelestia);
    FakeOps ops(caelestiaMode(), e.configHome());

    // No stock backup: Screen B, but nothing to restore, and the note says why. No mask checkbox going to stock.
    ScreenModel m = buildScreenModel(caelestiaMode(), e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Switch);
    QCOMPARE(m.direction, Direction::ToStock);
    QVERIFY(m.backups.isEmpty());
    QVERIFY(!m.canSwitch);
    QVERIFY(m.message.contains(QStringLiteral("no stock-side backup")));
    QVERIFY(!m.maskShown);

    // With stock backups: only the stock side is offered, newest first.
    e.setShellPackage(QString());
    const QString older = e.backup(Side::Stock);
    QTest::qSleep(1100);   // backup ids have one-second resolution
    const QString newer = e.backup(Side::Stock);
    m = buildScreenModel(caelestiaMode(), e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Switch);
    QVERIFY(m.canSwitch);
    QVERIFY(m.message.isEmpty());
    QCOMPARE(m.backups.size(), 2);
    QCOMPARE(m.backups.first().ref(), newer);
    QCOMPARE(m.backups.last().ref(), older);
    for (const BackupInfo &b : m.backups) {
        QVERIFY(b.ref() != caeRef);
        QCOMPARE(b.side, Side::Stock);
    }
}

void TestSwitch::screenSwitchFromStock()
{
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    const QString caeRef = e.backup(Side::Caelestia);
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    FakeOps ops(stockMode(), e.configHome());

    const ScreenModel m = buildScreenModel(stockMode(), e.ctx.paths, &ops);
    QCOMPARE(m.kind, ScreenModel::Kind::Switch);
    QCOMPARE(m.direction, Direction::ToCaelestia);
    QVERIFY(m.canSwitch);
    QCOMPARE(m.backups.size(), 1);
    QCOMPARE(m.backups.first().ref(), caeRef);           // only the target side
    QVERIFY(m.maskShown);                                // the checkbox applies when switching to Caelestia
    QVERIFY(m.maskAllowed && m.maskBlockers.isEmpty() && m.maskWeak.isEmpty());
    QCOMPARE(m.readings.provider, Provider::Plasma);
}

void TestSwitch::screenMaskDependents()
{
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    e.backup(Side::Caelestia);
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    FakeOps ops(stockMode(), e.configHome());

    // Weak (always WantedBy=plasma-core.target in a live session): allowed, with the units named for the warning.
    ops.deps[QStringLiteral("plasma-plasmashell.service")] = {QStringLiteral("WantedBy=plasma-core.target")};
    ScreenModel m = buildScreenModel(stockMode(), e.ctx.paths, &ops);
    QVERIFY(m.maskAllowed);
    QCOMPARE(m.maskWeak, QStringList{QStringLiteral("WantedBy=plasma-core.target")});

    // Strong, or unreadable: not allowed, and the blockers are named (the weak one is not a blocker).
    ops.deps[QStringLiteral("plasma-plasmashell.service")] = {QStringLiteral("WantedBy=plasma-core.target"), QStringLiteral("BoundBy=x.service")};
    m = buildScreenModel(stockMode(), e.ctx.paths, &ops);
    QVERIFY(!m.maskAllowed);
    QCOMPARE(m.maskBlockers, QStringList{QStringLiteral("BoundBy=x.service")});
    QCOMPARE(m.maskWeak.size(), 1);
}

void TestSwitch::repairFailureSentenceHasNoTrailingPeriod()
{
    // Seen live 2026-10-05: the CLI adds its own period, so the sentence printed "(...).." .
    Env e;
    e.setShellPackage(QString());
    e.backup(Side::Stock);
    FakeOps ops(halfSwitched("disabled"), e.configHome());
    e.ctx.ops = &ops;
    RepairPlan plan;
    QVERIFY(planRepair(e.ctx, &plan).ok);
    QVERIFY(plan.failure.startsWith(QStringLiteral("No switch is recorded")));
    QVERIFY2(!plan.failure.endsWith(QLatin1Char('.')), qPrintable(plan.failure));
}

QTEST_GUILESS_MAIN(TestSwitch)
#include "test_switch.moc"
