#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "backup.h"
#include "readings.h"
#include "switch.h"
#include "switchops.h"

namespace cs {

// What the main window shows once the readings are in (Phase A4 batch 2, architecture D23). Pure: the same
// readings, backups and dependency answers always give the same model, so the rules are unit-tested and the
// window is a thin view.
struct ScreenModel {
    enum class Kind {
        Blocked,      // no switching possible right now (inconsistent, not installed, no clear direction)
        NeedBackup,   // Screen A: no Caelestia-side backup yet
        Switch,       // Screen B
    };
    Kind kind = Kind::Blocked;

    // Blocked: why. NeedBackup: what to do. Switch: a note under the controls (may be empty).
    QString message;

    // NeedBackup: the Backup button works only while Caelestia is the running shell. In stock mode a
    // "Caelestia-side" backup would store the stock config under the Caelestia name (the D19-addendum bug class).
    bool canBackup = false;

    // Switch
    Direction direction = Direction::ToStock;   // from the running mode to the other
    QList<BackupInfo> backups;                  // target side only, newest first (a backup of the other side is refused)
    bool canSwitch = false;                     // there is a backup to restore
    bool maskShown = false;                     // the "also disable plasmashell" checkbox applies (only to Caelestia)
    bool maskAllowed = true;                    // no strong dependent of plasmashell (architecture D1)
    QStringList maskBlockers;                   // strong: shown with the greyed-out checkbox
    QStringList maskWeak;                       // weak: a warning, masking is still allowed

    Readings readings;                          // the readings the model was built from
};

// What the user picked on Screen B when pressing Switch (Phase A4 batch 2b, architecture D24).
struct SwitchChoice {
    Direction direction = Direction::ToStock;   // the direction the window was showing
    QString targetRef;                          // the backup selected in the dropdown ("side/id")
    bool mask = false;                          // the "also disable plasmashell" checkbox (only when going to Caelestia)
};

// The answer to "may this choice be started now?", worked out against a freshly read model.
struct ChoiceCheck {
    bool ok = false;
    QString problem;                // !ok: why nothing was started (shown under the Switch button)
    bool needsWeakMaskConfirm = false;   // ok, but the mask has weak dependents: ask first (unless switched off in the settings)
    QStringList weak;               // the weak dependents to name in that question
};

// The window can be old (a switch may have started behind it, or the state changed): the Switch button re-reads
// the state and checks the choice against the fresh model before anything starts. Pure and unit-tested.
ChoiceCheck checkChoice(const ScreenModel &fresh, const SwitchChoice &shown);

// The user-approved warning shown before the switch starts (architecture D19; wording confirmed 2026-10-02).
QString logoutWarningText();

// Decides the screen. `ops` is used only for the live plasmashell dependency check (checkMaskPlasmashell).
ScreenModel buildScreenModel(const Readings &readings, const BackupPaths &paths, SwitchOps *ops);

// "stock Plasma" / "Caelestia": where a switch in this direction ends up.
QString targetLabel(Direction direction);

} // namespace cs
