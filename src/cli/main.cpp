#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

#include "readings.h"
#include "version.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("caelestia-switch"));
    QCoreApplication::setApplicationVersion(cs::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Switch between Plasma and Caelestia KDE.\n\n"
        "Commands: status (implemented). Planned: backup, restore, on, off, repair, install, update, uninstall."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("json"), QStringLiteral("status: print the readings as JSON.")));
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run."));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(0);
    }

    if (args.first() == QLatin1String("status")) {
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

    QTextStream(stderr) << "caelestia-switch: '" << args.first() << "' is not implemented yet.\n";
    return 2;
}
