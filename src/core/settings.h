#pragma once

#include <QString>

namespace cs {

// The app's own small settings (Phase A4 batch 2b, architecture D24). One plain key=value file,
// <appConfigDir>/settings, outside both Caelestia's and Plasma's config. Deleting the file resets everything.
struct AppSettings {
    // Ask before masking plasmashell when it is only weakly wanted (WantedBy=plasma-core.target, always the
    // case in a live session). The confirmation dialog's "don't ask again" turns this off (architecture D1).
    bool confirmWeakMask = true;
};

// Missing, empty or unreadable file: the defaults. Unknown keys and bad values are ignored.
AppSettings readSettings(const QString &appConfigDir);

// Writes atomically (temporary file + rename); false and *error if it cannot be written.
bool writeSettings(const QString &appConfigDir, const AppSettings &settings, QString *error = nullptr);

} // namespace cs
