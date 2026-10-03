#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include "backup.h"
#include "fsutil.h"
#include "readings.h"
#include "state.h"
#include "switch.h"
#include "version.h"

namespace {

// Switch log: ~/.local/share/caelestia-switch/switch.log, also echoed to stderr (the journal when run as a service).
void logLine(const QString &msg)
{
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODate) + QLatin1Char(' ') + msg;
    QTextStream(stderr) << line << '\n';
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/caelestia-switch/switch.log");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write(line.toUtf8() + '\n');
    }
}

cs::SwitchContext makeContext(cs::SwitchOps *ops)
{
    cs::SwitchContext ctx;
    ctx.ops = ops;
    ctx.paths = cs::BackupPaths::defaults();
    ctx.stateFile = cs::appConfigDir() + QStringLiteral("/state");
    ctx.helpersFile = cs::appConfigDir() + QStringLiteral("/helpers");
    ctx.log = logLine;
    return ctx;
}

cs::SwitchRequest makeRequest(cs::Direction dir, const QCommandLineParser &parser)
{
    cs::SwitchRequest req;
    req.direction = dir;
    req.targetRef = parser.value(QStringLiteral("backup"));
    req.maskPlasmashell = dir == cs::Direction::ToCaelestia && parser.isSet(QStringLiteral("mask"));
    req.logout = !parser.isSet(QStringLiteral("no-logout"));
    req.repair = parser.isSet(QStringLiteral("as-repair"));   // run-switch started by `repair`; the cause is in the state file
    return req;
}

// Prints state-file progress until the executor reaches the logout (or fails).
int followSwitch(const QString &stateFile)
{
    QTextStream out(stdout);
    int lastStep = -1;
    for (int i = 0; i < 1500; ++i) {   // about 10 minutes
        cs::SwitchState st;
        if (cs::readState(stateFile, &st)) {
            if (st.step != lastStep) {
                lastStep = st.step;
                out << "step " << st.step << ": " << st.stepName << '\n';
                out.flush();
            }
            if (st.result == QLatin1String("failed")) {
                out << "FAILED: " << st.error << '\n';
                if (st.rolledBack || !st.cause.isEmpty()) {
                    out << "The failure that started this repair: " << st.cause << '\n';
                    out << "Fix the cause, then run 'repair' again.\n";
                }
                return 1;
            }
            if (st.mode == QLatin1String("pending-logout")) {
                out << (st.logout ? "Logging out now.\n" : "Stopped before the logout (--no-logout). Log out yourself, then run 'finish'.\n");
                return 0;
            }
            if (st.mode != QLatin1String("transitioning")) {
                out << "Done (mode " << st.mode << ").\n";
                return 0;
            }
        }
        QThread::msleep(400);
    }
    out << "Timed out waiting for the switch.\n";
    return 1;
}

int cmdSwitch(cs::Direction dir, const QCommandLineParser &parser)
{
    cs::RealOps ops;
    const cs::SwitchContext ctx = makeContext(&ops);
    const cs::SwitchRequest req = makeRequest(dir, parser);
    if (req.maskPlasmashell) {
        // Early look at what depends on plasmashell, so the weak-dependency note shows before the switch starts.
        // (launchSwitch repeats the check; a strong dependent is refused there.)
        const cs::MaskCheck mc = cs::checkMaskPlasmashell(&ops);
        if (mc.allowed() && !mc.weak.isEmpty()) {
            QTextStream(stdout) << "Note: plasmashell is only weakly wanted by " << mc.weak.join(QStringLiteral(", "))
                                << "; masking it is allowed.\n";
        }
    }
    const cs::OpResult res = cs::launchSwitch(req, ctx, QCoreApplication::applicationFilePath());
    if (!res.ok) {
        QTextStream(stderr) << "caelestia-switch: " << res.error << '\n';
        return 1;
    }
    if (!res.warnings.isEmpty()) {          // no-op
        QTextStream(stdout) << res.warnings.join(QLatin1Char('\n')) << '\n';
        return 0;
    }
    QTextStream(stdout) << "Switch started in the background (unit caelestia-switch-run). "
                           "It logs out at the end; the log is ~/.local/share/caelestia-switch/switch.log\n";
    return parser.isSet(QStringLiteral("wait")) ? followSwitch(ctx.stateFile) : 0;
}

// Undoes a failed or interrupted switch: back to the side that was being left (architecture D20).
int cmdRepair(const QCommandLineParser &parser)
{
    cs::RealOps ops;
    const cs::SwitchContext ctx = makeContext(&ops);
    QTextStream out(stdout);
    cs::RepairPlan plan;
    const cs::OpResult planned = cs::planRepair(ctx, &plan);
    if (!planned.ok) {
        QTextStream(stderr) << "caelestia-switch: repair is not possible: " << planned.error << '\n';
        return 1;
    }
    if (!plan.needed) {
        out << plan.message << '\n';
        return 0;
    }
    out << plan.failure << ".\n";
    for (const QString &w : std::as_const(plan.warnings)) {
        out << "Note: " << w << '\n';
    }
    out << plan.action << '\n';
    out.flush();

    cs::SwitchRequest req = plan.request;
    req.logout = !parser.isSet(QStringLiteral("no-logout"));
    if (plan.alreadyThere) {
        // Nothing is stopped or restored, so no background service is needed: close the record here.
        const cs::SwitchOutcome res = cs::runSwitch(req, ctx);
        if (!res.ok) {
            QTextStream(stderr) << "caelestia-switch: " << res.error << '\n';
            return 1;
        }
        out << "Done. No logout is needed.\n";
        return 0;
    }
    const cs::OpResult res = cs::launchSwitch(req, ctx, QCoreApplication::applicationFilePath());
    if (!res.ok) {
        QTextStream(stderr) << "caelestia-switch: " << res.error << '\n';
        return 1;
    }
    out << "Repair started in the background (unit caelestia-switch-run). "
           "It logs out at the end; after logging in, run 'finish'. The log is ~/.local/share/caelestia-switch/switch.log\n";
    out.flush();
    return parser.isSet(QStringLiteral("wait")) ? followSwitch(ctx.stateFile) : 0;
}

// Internal: the executor, started by launchSwitch inside a transient service.
int cmdRunSwitch(const QCommandLineParser &parser)
{
    cs::Direction dir;
    if (!cs::parseDirection(parser.value(QStringLiteral("direction")), &dir)) {
        QTextStream(stderr) << "caelestia-switch: run-switch needs --direction to-stock|to-caelestia\n";
        return 2;
    }
    cs::RealOps ops;
    const cs::SwitchContext ctx = makeContext(&ops);
    const cs::SwitchOutcome out = cs::runSwitch(makeRequest(dir, parser), ctx);
    for (const QString &w : out.warnings) {
        logLine(QStringLiteral("warning: ") + w);
    }
    return out.ok ? 0 : 1;
}

int cmdFinish(const QCommandLineParser &parser)
{
    cs::RealOps ops;
    const cs::SwitchContext ctx = makeContext(&ops);
    const int timeoutSec = parser.isSet(QStringLiteral("timeout")) ? parser.value(QStringLiteral("timeout")).toInt() : 60;
    QString summary;
    const cs::OpResult res = cs::finishSwitch(ctx, timeoutSec * 1000, 2000, &summary);
    if (!res.ok) {
        QTextStream(stderr) << "caelestia-switch: " << res.error << '\n';
        return 1;
    }
    QTextStream(stdout) << summary << '\n';
    return 0;
}

int cmdStatus(const QCommandLineParser &parser)
{
    const cs::Readings r = cs::gatherReadings();
    QTextStream out(stdout);
    if (parser.isSet(QStringLiteral("json"))) {
        out << QJsonDocument(cs::toJson(r)).toJson(QJsonDocument::Indented);
    } else {
        out << cs::toText(r);
    }
    // Exit 0 even when Inconsistent (it is a reading, not a failure); 3 if systemd cannot be queried.
    return r.systemdReachable ? 0 : 3;
}

int cmdBackup(const QCommandLineParser &parser)
{
    cs::Side side;
    if (parser.isSet(QStringLiteral("side"))) {
        if (!cs::parseSide(parser.value(QStringLiteral("side")), &side)) {
            QTextStream(stderr) << "caelestia-switch: --side must be 'stock' or 'caelestia'.\n";
            return 2;
        }
    } else {
        // Default: the side of the mode that is active now.
        side = cs::gatherReadings().provider == cs::Provider::Caelestia ? cs::Side::Caelestia : cs::Side::Stock;
    }
    QString ref;
    const cs::OpResult res = cs::createBackup(side, QStringLiteral("manual"), cs::BackupPaths::defaults(), &ref);
    for (const QString &w : res.warnings) {
        QTextStream(stderr) << "warning: " << w << '\n';
    }
    if (!res.ok) {
        QTextStream(stderr) << "caelestia-switch: backup failed: " << res.error << '\n';
        return 1;
    }
    QTextStream(stdout) << "Created backup " << ref << '\n';
    return 0;
}

int cmdBackups()
{
    QTextStream out(stdout);
    const QList<cs::BackupInfo> list = cs::listBackups(cs::BackupPaths::defaults());
    if (list.isEmpty()) {
        out << "No backups.\n";
        return 0;
    }
    for (const cs::BackupInfo &b : list) {
        out << b.ref().leftJustified(30) << ' ' << b.created.toLocalTime().toString(Qt::ISODate).leftJustified(20)
            << ' ' << b.trigger.leftJustified(11) << " caelestia " << b.caelestiaVersion
            << ", plasma " << b.plasmaVersion << '\n';
    }
    return 0;
}

int cmdRestore(const QStringList &args)
{
    if (args.size() < 2) {
        QTextStream(stderr) << "caelestia-switch: restore needs a backup reference (see 'backups').\n";
        return 2;
    }
    const cs::OpResult res = cs::restoreBackup(args.at(1), cs::BackupPaths::defaults());
    for (const QString &w : res.warnings) {
        QTextStream(stderr) << "warning: " << w << '\n';
    }
    if (!res.ok) {
        QTextStream(stderr) << "caelestia-switch: " << res.error << '\n';
        return 1;
    }
    QTextStream(stdout) << "Restored " << args.at(1)
                        << " (config files only; the running shell was not stopped or restarted).\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("caelestia-switch"));
    QCoreApplication::setApplicationVersion(cs::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Switch between Plasma and Caelestia KDE.\n\n"
        "Commands: status, backup, backups, restore <side/id>, on, off, finish, repair. Planned: install, update, uninstall."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("json"), QStringLiteral("status: print the readings as JSON.")));
    parser.addOption(QCommandLineOption(QStringLiteral("side"), QStringLiteral("backup: 'stock' or 'caelestia' (default: the active mode)."),
                                        QStringLiteral("side")));
    parser.addOption(QCommandLineOption(QStringLiteral("mask"), QStringLiteral("on: also stop and mask plasmashell.")));
    parser.addOption(QCommandLineOption(QStringLiteral("backup"), QStringLiteral("on/off: backup to restore (side/id); default: the newest of the target side."),
                                        QStringLiteral("ref")));
    parser.addOption(QCommandLineOption(QStringLiteral("no-logout"), QStringLiteral("on/off/repair: stop just before the logout (for testing).")));
    parser.addOption(QCommandLineOption(QStringLiteral("wait"), QStringLiteral("on/off/repair: follow the progress until the logout.")));
    parser.addOption(QCommandLineOption(QStringLiteral("direction"), QStringLiteral("run-switch (internal): to-stock or to-caelestia."), QStringLiteral("dir")));
    parser.addOption(QCommandLineOption(QStringLiteral("as-repair"), QStringLiteral("run-switch (internal): this run is a repair.")));
    parser.addOption(QCommandLineOption(QStringLiteral("timeout"), QStringLiteral("finish: seconds to wait for the final state (default 60)."), QStringLiteral("seconds")));
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run."));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(0);
    }
    const QString cmd = args.first();
    if (cmd == QLatin1String("status")) {
        return cmdStatus(parser);
    }
    if (cmd == QLatin1String("backup")) {
        return cmdBackup(parser);
    }
    if (cmd == QLatin1String("backups")) {
        return cmdBackups();
    }
    if (cmd == QLatin1String("restore")) {
        return cmdRestore(args);
    }
    if (cmd == QLatin1String("on")) {
        return cmdSwitch(cs::Direction::ToCaelestia, parser);
    }
    if (cmd == QLatin1String("off")) {
        return cmdSwitch(cs::Direction::ToStock, parser);
    }
    if (cmd == QLatin1String("run-switch")) {
        return cmdRunSwitch(parser);
    }
    if (cmd == QLatin1String("finish")) {
        return cmdFinish(parser);
    }
    if (cmd == QLatin1String("repair")) {
        return cmdRepair(parser);
    }
    QTextStream(stderr) << "caelestia-switch: '" << cmd << "' is not implemented yet.\n";
    return 2;
}
