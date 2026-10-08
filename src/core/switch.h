#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "backup.h"
#include "state.h"
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

// The arguments of `caelestia-switch run-switch` that launchSwitch hands to the executor process. The executor
// rebuilds its SwitchRequest from them (src/cli/main.cpp makeRequest), so every field that matters must be here;
// a test pins this down because a missing flag silently changes what the executor does.
QStringList runSwitchArguments(const SwitchRequest &req);

// Marks the state "transitioning", then starts runSwitch in a transient user service
// (own cgroup, so stopping either shell cannot kill it). `exePath` is this binary.
OpResult launchSwitch(const SwitchRequest &req, const SwitchContext &ctx, const QString &exePath);

// "stock" or "caelestia" for a settled system, empty if neither (used for no-op and state close-out).
QString modeOf(const Readings &r);

// --- Post-login service (Phase A3b, architecture D21) ---

// The user unit that runs `caelestia-switch post-login` at every login (WantedBy=graphical-session.target).
QString postLoginUnitName();

// Text of that unit for a given executable; '%' in the path is escaped for systemd.
QString postLoginUnitText(const QString &exePath);

// Writes the unit under <configHome>/systemd/user and enables it, but only if it is missing, differs
// (for example the binary moved) or is not enabled. Idempotent. A failure never blocks a switch.
bool ensurePostLoginService(const SwitchContext &ctx, const QString &exePath, QString *error);

// --- Result view (Phase A4, first batch; architecture D22) ---

// What the result window and the post-login notification say about the switch the state file describes.
struct ResultView {
    bool present = false;   // false: nothing to report (no switch, or a settled result that was already seen)
    QString kind;           // "done" / "rolled-back" / "failed"
    QString title;
    QString text;
};

// Pure: turns a state file into a ResultView. A settled mode (stock / caelestia) is shown only while `unseen`;
// a failed or unfinished switch (mode transitioning, or pending-logout with a recorded failure) is always shown,
// with the way out ('repair'). pending-logout without a failure is the normal wait: nothing to show.
ResultView describeResult(const SwitchState &st);

// Sets unseen=false in the state file (atomic write). No-op if there is no state file or nothing unseen.
OpResult markSeen(const QString &stateFile);

// The GUI binary that goes with this CLI: next to it (installed layout), then ../gui/ (build tree), then PATH.
// Empty if none is found. `cliDir` is the directory of the CLI binary.
QString findGuiBinary(const QString &cliDir);

// The CLI binary that goes with the GUI (the executor `run-switch` is a CLI command, and `launchSwitch` must be
// given that path, not the GUI's): next to the GUI (installed layout), then ../cli/ (build tree), then PATH.
// Empty if none is found. `guiDir` is the directory of the GUI binary.
QString findCliBinary(const QString &guiDir);

struct PostLoginResult {
    bool ran = false;       // false: no switch was waiting for its login, nothing to report
    bool ok = true;         // the expected final state was reached
    QString title;          // notification title
    QString body;           // notification text
};

// What the post-login service does: if the state is pending-logout, run finishSwitch (which waits up to
// timeoutMs for the readings to match) and turn the outcome into a notification text. The state stays
// `unseen` for the GUI (A4).
PostLoginResult runPostLogin(const SwitchContext &ctx, int timeoutMs, int pollMs);

} // namespace cs
