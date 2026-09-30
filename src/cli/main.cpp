#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "version.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("caelestia-switch"));
    QCoreApplication::setApplicationVersion(cs::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Switch between Plasma and Caelestia KDE.\n\n"
        "Planned commands: status, backup, restore, on, off, repair, install, update, uninstall."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run."));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(0);
    }

    QTextStream(stderr) << "caelestia-switch: '" << args.first()
                        << "' is not implemented yet (project skeleton).\n";
    return 2;
}
