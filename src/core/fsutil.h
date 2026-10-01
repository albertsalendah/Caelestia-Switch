#pragma once

#include <QFile>
#include <QStandardPaths>
#include <QString>

namespace cs {

// Reads up to maxBytes of a text file; empty string if it cannot be opened.
inline QString readTextFile(const QString &path, qint64 maxBytes = 1 << 20)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.read(maxBytes));
}

// First line of a text file, trimmed; empty if missing/unreadable.
inline QString readFirstLine(const QString &path)
{
    return readTextFile(path, 4096).section(QLatin1Char('\n'), 0, 0).trimmed();
}

// The app's own config directory (state file, install marker). Outside both
// Caelestia's and Plasma's config (spec: State file, Source detection).
inline QString appConfigDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/caelestia-switch");
}

} // namespace cs
