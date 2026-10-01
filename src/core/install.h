#pragma once

#include <QString>

#include "readings.h"

namespace cs {

// --- Pure helpers (unit-tested) ---

// VERSION value from the text of .github/version.env / .current_version; empty if absent.
QString parseVersionEnv(const QString &text);

// URL of [remote "origin"] from the text of a .git/config; empty if absent.
QString parseOriginUrl(const QString &gitConfigText);

// Lowercased "host/owner/repo" form: scheme, user info, ".git" and trailing "/" removed,
// scp-style "git@host:owner/repo" converted.
QString normalizeRepoUrl(const QString &url);

// "ladybug-me" / "fork" for the two known repositories, otherwise "unknown".
QString sourceFromUrl(const QString &url);

// Full commit hash HEAD points to in <dir>/.git (loose ref, then packed-refs); empty if unknown.
QString resolveHead(const QString &checkoutDir);

// True if both are hashes and equal (a prefix of at least 7 characters counts as equal).
bool sameCommit(const QString &a, const QString &b);

// --- Detection ---

// Spec "Source and version detection", in order:
//   source:  app-written marker (only if its commit == .current_commit), else checkout discovery, else unknown.
//   version: marker, else ~/.config/quickshell/caelestia/.current_version, else checkout version.env, else unknown.
// `home` and `appConfigDir` are parameters so tests can use a fake tree;
// `caelestiaDirEnv` is the value of CAELESTIA_DIR (may be empty).
InstallInfo detectInstall(const QString &home, const QString &appConfigDir, const QString &caelestiaDirEnv);

// Same, using the real home directory, app config dir and $CAELESTIA_DIR.
InstallInfo detectInstall();

} // namespace cs
