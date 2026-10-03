#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "backup.h"
#include "consistency.h"
#include "plasmaconfig.h"
#include "readings.h"
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
                *error = cmd + QStringLiteral(" failed");
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
    Env e;
    e.setShellPackage(QStringLiteral("caelestia.desktop"));
    e.backup(Side::Caelestia);
    FakeOps ops(caelestiaMode(/*plasmaMasked=*/true), e.configHome());
    e.ctx.ops = &ops;

    SwitchRequest req;
    req.direction = Direction::ToCaelestia;
    const SwitchOutcome out = runSwitch(req, e.ctx);
    QVERIFY2(out.ok && !out.noop, qPrintable(out.error));
    QVERIFY(ops.indexOf(QStringLiteral("systemctl unmask plasma-plasmashell.service")) >= 0);
    QVERIFY(ops.indexOf(QStringLiteral("systemctl stop plasma-plasmashell.service")) < 0);  // was not running
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
    toCae.maskPlasmashell = true;   // different mask request: not a no-op (needs a backup, so it fails here)
    QVERIFY(!preflight(toCae, e.ctx, true, &plan).ok);

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

QTEST_GUILESS_MAIN(TestSwitch)
#include "test_switch.moc"
