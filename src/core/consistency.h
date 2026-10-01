#pragma once

#include <QStringList>

#include "readings.h"

namespace cs {

// Caelestia counts as running if its service is active or a quickshell process exists.
bool caelestiaRunning(const Readings &r);

// Stock Plasma draws the panels: plasmashell runs and ShellPackage is the stock one.
// (A headless plasmashell with ShellPackage=caelestia.desktop draws no panels.)
bool plasmaDrawing(const Readings &r);

// Bar/panel provider: Caelestia if it runs, Plasma if plasmaDrawing, Both if both, else None.
Provider computeProvider(const Readings &r);

// Combinations that are actually wrong (spec: Inconsistent). Empty = consistent.
QStringList findInconsistencies(const Readings &r);

} // namespace cs
