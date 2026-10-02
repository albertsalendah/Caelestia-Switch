#include "state.h"

#include "fsutil.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace cs {

namespace {

bool toBool(const QString &v)
{
    return v == QLatin1String("true") || v == QLatin1String("1") || v == QLatin1String("yes");
}

QString oneLine(const QString &v)
{
    QString s = v;
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return s;
}

} // namespace

bool readState(const QString &path, SwitchState *out)
{
    const QString text = readTextFile(path);
    if (text.trimmed().isEmpty()) {
        return false;
    }
    SwitchState s;
    bool first = true;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) {
            if (first) {
                s.mode = line.section(QLatin1Char(' '), 0, 0).toLower();  // bare word: mode only
            }
            first = false;
            continue;
        }
        first = false;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("mode")) {
            s.mode = val.toLower();
        } else if (key == QLatin1String("direction")) {
            s.direction = val;
        } else if (key == QLatin1String("step")) {
            s.step = val.toInt();
        } else if (key == QLatin1String("stepName")) {
            s.stepName = val;
        } else if (key == QLatin1String("targetRef")) {
            s.targetRef = val;
        } else if (key == QLatin1String("snapshotRef")) {
            s.snapshotRef = val;
        } else if (key == QLatin1String("maskPlasmashell")) {
            s.maskPlasmashell = toBool(val);
        } else if (key == QLatin1String("logout")) {
            s.logout = toBool(val);
        } else if (key == QLatin1String("helpers")) {
            s.helpers = val.split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (key == QLatin1String("error")) {
            s.error = val;
        } else if (key == QLatin1String("result")) {
            s.result = val;
        } else if (key == QLatin1String("unseen")) {
            s.unseen = toBool(val);
        }
    }
    if (s.mode.isEmpty()) {
        return false;
    }
    *out = s;
    return true;
}

bool writeState(const QString &path, const SwitchState &s, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("cannot write %1").arg(path);
        }
        return false;
    }
    const auto b = [](bool v) { return v ? QStringLiteral("true") : QStringLiteral("false"); };
    QString out;
    out += QStringLiteral("mode=%1\n").arg(s.mode);
    out += QStringLiteral("direction=%1\n").arg(s.direction);
    out += QStringLiteral("step=%1\n").arg(s.step);
    out += QStringLiteral("stepName=%1\n").arg(oneLine(s.stepName));
    out += QStringLiteral("targetRef=%1\n").arg(s.targetRef);
    out += QStringLiteral("snapshotRef=%1\n").arg(s.snapshotRef);
    out += QStringLiteral("maskPlasmashell=%1\n").arg(b(s.maskPlasmashell));
    out += QStringLiteral("logout=%1\n").arg(b(s.logout));
    out += QStringLiteral("helpers=%1\n").arg(s.helpers.join(QLatin1Char(',')));
    out += QStringLiteral("error=%1\n").arg(oneLine(s.error));
    out += QStringLiteral("result=%1\n").arg(s.result);
    out += QStringLiteral("unseen=%1\n").arg(b(s.unseen));
    f.write(out.toUtf8());
    if (!f.commit()) {
        if (error) {
            *error = QStringLiteral("cannot commit %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace cs
