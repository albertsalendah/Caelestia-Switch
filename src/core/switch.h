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
    QString message;
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

// Checks that a switch may start. `checkState` = false when called by the executor that
// the launcher already marked as "transitioning".
OpResult preflight(const SwitchRequest &req, const SwitchContext &ctx, bool checkState, SwitchPlan *plan);

// Runs steps 1-8 in this process (it holds the switch lock and updates the state file after each step).
SwitchOutcome runSwitch(const SwitchRequest &req, const SwitchContext &ctx);

// After the next login: waits until the readings match the expected final state, closes out the
// state file (mode caelestia/stock, result done, unseen). No-op unless the state is pending-logout.
OpResult finishSwitch(const SwitchContext &ctx, int timeoutMs, int pollMs, QString *summary);

// Marks the state "transitioning", then starts runSwitch in a transient user service
// (own cgroup, so stopping either shell cannot kill it). `exePath` is this binary.
OpResult launchSwitch(const SwitchRequest &req, const SwitchContext &ctx, const QString &exePath);

// "stock" or "caelestia" for a settled system, empty if neither (used for no-op and state close-out).
QString modeOf(const Readings &r);

} // namespace cs
