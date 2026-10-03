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
    QString result;          // "" / done / failed / rolled-back
    bool unseen = false;     // result not yet shown to the user

    // Recorded so that `repair` can undo a failed or interrupted switch (architecture D20).
    QString leaving;         // side that was running when the switch started: stock / caelestia / none
    bool plasmaWasMasked = false;  // plasmashell was masked when the switch started
    int failedStep = 0;      // step that failed (0 = none recorded)
    QString cause;           // the original failure, kept while a repair runs and after a rollback
    bool rolledBack = false; // this state closes a repair: the system went back to the side that was left
};

// Reads the file; false if it does not exist or is empty.
bool readState(const QString &path, SwitchState *out);

// Writes atomically (temporary file + rename).
bool writeState(const QString &path, const SwitchState &state, QString *error = nullptr);

} // namespace cs
