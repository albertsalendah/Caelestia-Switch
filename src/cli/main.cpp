#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

#include "backup.h"
#include "readings.h"
#include "version.h"

namespace {

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
        "Commands: status, backup, backups, restore <side/id>. Planned: on, off, repair, install, update, uninstall."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("json"), QStringLiteral("status: print the readings as JSON.")));
    parser.addOption(QCommandLineOption(QStringLiteral("side"), QStringLiteral("backup: 'stock' or 'caelestia' (default: the active mode)."),
                                        QStringLiteral("side")));
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
    QTextStream(stderr) << "caelestia-switch: '" << cmd << "' is not implemented yet.\n";
    return 2;
}
