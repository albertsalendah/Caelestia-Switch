#include "plasmaconfig.h"

#include <KConfig>
#include <KConfigGroup>

namespace cs {

QString readShellPackage(const QString &configHome)
{
    const QString file = configHome.isEmpty() ? QStringLiteral("plasmashellrc")
                                              : configHome + QStringLiteral("/plasmashellrc");
    KConfig config(file, KConfig::SimpleConfig);
    KConfigGroup shell(&config, QStringLiteral("Shell"));
    return shell.readEntry("ShellPackage", QString()).trimmed();
}

bool isStockShellPackage(const QString &shellPackage)
{
    return shellPackage.isEmpty() || shellPackage == QLatin1String("org.kde.plasma.desktop");
}

} // namespace cs
