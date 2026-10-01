#pragma once

#include <QString>

namespace cs {

// ShellPackage from [Shell] in plasmashellrc, or an empty string if unset.
// Read-only (KF6 ConfigCore).
QString readShellPackage();

// Empty (unset) or the stock "org.kde.plasma.desktop" means stock Plasma draws the panels.
bool isStockShellPackage(const QString &shellPackage);

} // namespace cs
