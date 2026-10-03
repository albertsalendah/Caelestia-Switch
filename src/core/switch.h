#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "backup.h"
#include "switchops.h"

namespace cs {

enum class Direction { ToStock, ToCaelestia };

QString directionKey(Direction d);                 // "to-stock" / "to-caelestia"
bool parseDirection(const QString &text, Direction *out);

struct SwitchRequest {
    Direction direction = Direction::ToStock;
    QString targetRef;                  // backup to restore; empty = newest of the target side
    bool maskPlasmashell = false;       // only meaningful for ToCaelestia
    bool logout = true;                 // false: stop just before the logout (live testing)
    bool repair = false;                // a rollback built by planRepair: skips the checks a half-switched system fails
    QString cause;                      // repair only: the original failure, stored in the state file
    bool configMayBeTouched = false;    // repair only: the failed switch may already have rewritten config, so the
                                        // rollback must restore the snapshot even if the readings already look right
};

// Everything the switch needs; a struct so tests can use a fake system and fake paths.
struct SwitchContext {
    SwitchOps *ops = nullptr;
    BackupPaths paths;
    QString stateFile;                  // ~/.config/caelestia-switch/state
    QString helpersFile;                // helper units this app disabled (re-enabled on the way back)
    std::function<void(const QString &)> log;   // optional progress log
};

struct SwitchPlan {
    Side leaving = Side::Caelestia;
    Side target = Side::Stock;
    QString targetRef;
    bool noop = false;                  // already in the requested mode
    bool unitsOnly = false;             // Caelestia is already running and only the plasmashell mask changes
    bool leavingKnown = true;           // a shell was providing panels, so `leaving` is meaningful
    bool snapshot = true;               // take an automatic snapshot of `leaving` (false: nothing to snapshot)
    QString message;
    QStringList warnings;               // non-blocking notes (e.g. weak dependents of plasmashell)
};

struct SwitchOutcome {
    bool ok = true;
    bool noop = false;
    QString error;
    int failedStep = 0;                 // 0 = none
    QStringList warnings;
};

// Step numbers (the state file records the last completed one).
enum SwitchStep {
    StepPreflight = 1,
    StepSnapshot = 2,
    StepStopOutgoing = 3,
    StepHelpers = 4,
    StepRestore = 5,
    StepUnits = 6,
    StepVerify = 7,
    StepLogout = 8,
    StepFinished = 9,
};

// What depends on plasma-plasmashell.service (architecture D1, amended 2026-10-02).
// `blockers`: RequiredBy / RequisiteOf / BoundBy entries (strong: masking would break the dependent),
// and anything that could not be read or understood (unknown counts as strong).
// `weak`: WantedBy entries (masking is allowed; Test 1 ran two masked boots with the same relation).
// Entries are "Property=value" strings. Used by the pre-flight, by step 6 and (later) by the GUI.
struct MaskCheck {
    QStringList blockers;
    QStringList weak;
    bool allowed() const { return blockers.isEmpty(); }
};

// One live query of plasmashell's dependents, split into strong and weak.
MaskCheck checkMaskPlasmashell(SwitchOps *ops);

// Checks that a switch may start. `checkState` = false when called by the executor that
// the launcher already marked as "transitioning".
OpResult preflight(const SwitchRequest &req, const SwitchContext &ctx, bool checkState, SwitchPlan *plan);

// Runs steps 1-8 in this process (it holds the switch lock and updates the state file after each step).
SwitchOutcome runSwitch(const SwitchRequest &req, const SwitchContext &ctx);

// After the next login: waits until the readings match the expected final state, closes out the
// state file (mode caelestia/stock, result done, unseen). No-op unless the state is pending-logout.
OpResult finishSwitch(const SwitchContext &ctx, int timeoutMs, int pollMs, QString *summary);

// What `repair` would do, worked out from the state file and the readings (architecture D20).
struct RepairPlan {
    bool needed = false;                // false: nothing to repair, see `message`
    bool alreadyThere = false;          // the system already matches the side that was left: only the state file is closed
    SwitchRequest request;              // the rollback; run it with runSwitch / launchSwitch (request.repair is set)
    QString failure;                    // what went wrong, with the cause if known
    QString action;                     // what repair is going to do
    QString message;                    // when !needed
    QStringList warnings;
};

// Default repair: roll back to the side that was being left, using the automatic snapshot of step 2.
// No usable state file: fall back to restoring stock Plasma (spec).
OpResult planRepair(const SwitchContext &ctx, RepairPlan *plan);

// Marks the state "transitioning", then starts runSwitch in a transient user service
// (own cgroup, so stopping either shell cannot kill it). `exePath` is this binary.
OpResult launchSwitch(const SwitchRequest &req, const SwitchContext &ctx, const QString &exePath);

// "stock" or "caelestia" for a settled system, empty if neither (used for no-op and state close-out).
QString modeOf(const Readings &r);

} // namespace cs
