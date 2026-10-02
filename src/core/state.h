#pragma once

#include <QString>
#include <QStringList>

namespace cs {

// The switch state file (spec: State file). Simple key=value lines, `mode=` first so that
// the lenient reader in `status` (first word of the first line) keeps working.
struct SwitchState {
    QString mode;            // caelestia / stock / transitioning / pending-logout
    QString direction;       // to-stock / to-caelestia (while a switch is in progress or just finished)
    int step = 0;            // number of the last completed step
    QString stepName;
    QString targetRef;       // backup applied by the switch ("side/id")
    QString snapshotRef;     // automatic snapshot of the side that was left
    bool maskPlasmashell = false;
    bool logout = true;
    QStringList helpers;     // helper units this switch disabled
    QString error;
    QString result;          // "" / done / failed
    bool unseen = false;     // result not yet shown to the user
};

// Reads the file; false if it does not exist or is empty.
bool readState(const QString &path, SwitchState *out);

// Writes atomically (temporary file + rename).
bool writeState(const QString &path, const SwitchState &state, QString *error = nullptr);

} // namespace cs
