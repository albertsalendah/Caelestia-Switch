#include "settings.h"

#include "fsutil.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace cs {

namespace {

QString settingsPath(const QString &appConfigDir)
{
    return appConfigDir + QStringLiteral("/settings");
}

} // namespace

AppSettings readSettings(const QString &appConfigDir)
{
    AppSettings s;
    const QStringList lines = readTextFile(settingsPath(appConfigDir)).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed().toLower();
        if (key == QLatin1String("confirmWeakMask")) {
            if (val == QLatin1String("false") || val == QLatin1String("0") || val == QLatin1String("no")) {
                s.confirmWeakMask = false;
            } else if (val == QLatin1String("true") || val == QLatin1String("1") || val == QLatin1String("yes")) {
                s.confirmWeakMask = true;
            }
        }
    }
    return s;
}

bool writeSettings(const QString &appConfigDir, const AppSettings &settings, QString *error)
{
    const QString path = settingsPath(appConfigDir);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("cannot write %1").arg(path);
        }
        return false;
    }
    f.write(QStringLiteral("confirmWeakMask=%1\n").arg(settings.confirmWeakMask ? QStringLiteral("true") : QStringLiteral("false")).toUtf8());
    if (!f.commit()) {
        if (error) {
            *error = QStringLiteral("cannot commit %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace cs
