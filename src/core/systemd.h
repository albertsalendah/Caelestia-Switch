#pragma once

#include <QString>

#include "readings.h"

namespace cs {

// Queries a systemd *user* unit over the user D-Bus (org.freedesktop.systemd1).
// Uses LoadUnit so that masked and not-installed units still answer (LoadState
// "masked" / "not-found"). Returns false and fills *error if systemd cannot be reached.
bool queryUnit(const QString &unitName, UnitState &out, QString *error);

// True if the current user owns a live (non-zombie) process whose name (comm) equals `comm`.
bool isProcessRunning(const QString &comm);

} // namespace cs
