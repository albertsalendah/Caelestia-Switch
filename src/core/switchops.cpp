#include "switchops.h"

#include "plasmaconfig.h"
#include "systemd.h"

#include <QDBusMessage>
#include <QVariantMap>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include <unistd.h>

namespace cs {

namespace {

const QString kCaelestiaUnit = QStringLiteral("caelestia-shell.service");

// Runs a program, returns its exit status (-1 if it did not start or time out).
int runProgram(const QString &program, const QStringList &args, const QProcessEnvironment &env,
               QString *output, QString *error, int timeoutMs = 20000)
{
    QProcess p;
    p.setProcessEnvironment(env);
    p.start(program, args);
    if (!p.waitForStarted(3000)) {
        if (error) {
            *error = QStringLiteral("cannot start %1").arg(program);
        }
        return -1;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        if (error) {
            *error = QStringLiteral("%1 timed out").arg(program);
        }
        return -1;
    }
    if (output) {
        *output = QString::fromUtf8(p.readAllStandardOutput());
    }
    if (error) {
        *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    }
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

// quickshell only finds instances on the display it is told about. Inside the session
// WAYLAND_DISPLAY is set; over SSH it is not, so look at the user manager's environment,
// then at the sockets in the runtime directory.
QProcessEnvironment sessionEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.contains(QStringLiteral("XDG_RUNTIME_DIR"))) {
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), QStringLiteral("/run/user/%1").arg(getuid()));
    }
    if (env.contains(QStringLiteral("WAYLAND_DISPLAY"))) {
        return env;
    }
    QString out;
    QString err;
    if (runProgram(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("show-environment")}, env, &out, &err, 5000) == 0) {
        const QStringList lines = out.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("WAYLAND_DISPLAY="))) {
                env.insert(QStringLiteral("WAYLAND_DISPLAY"), line.mid(16).trimmed());
                return env;
            }
        }
    }
    const QStringList sockets = QDir(env.value(QStringLiteral("XDG_RUNTIME_DIR")))
                                    .entryList({QStringLiteral("wayland-[0-9]*")}, QDir::System | QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &s : sockets) {
        if (!s.endsWith(QLatin1String(".lock"))) {
            env.insert(QStringLiteral("WAYLAND_DISPLAY"), s);
            break;
        }
    }
    return env;
}

} // namespace

Readings RealOps::readings()
{
    return gatherReadings();
}

UnitState RealOps::unitState(const QString &unit)
{
    UnitState u;
    QString err;
    queryUnit(unit, u, &err);
    return u;
}

QString RealOps::shellPackage()
{
    return readShellPackage();
}

bool RealOps::systemctl(const QStringList &args, QString *error)
{
    QString err;
    const int rc = runProgram(QStringLiteral("systemctl"), QStringList{QStringLiteral("--user")} + args,
                              QProcessEnvironment::systemEnvironment(), nullptr, &err, 30000);
    if (rc != 0 && error) {
        *error = QStringLiteral("systemctl --user %1 failed: %2").arg(args.join(QLatin1Char(' ')), err);
    }
    return rc == 0;
}

QStringList RealOps::dependents(const QString &unit, const QStringList &properties)
{
    QStringList args{QStringLiteral("--user"), QStringLiteral("show")};
    for (const QString &p : properties) {
        args << QStringLiteral("-p") << p;
    }
    args << unit;
    QString out;
    QString err;
    QStringList found;
    if (runProgram(QStringLiteral("systemctl"), args, QProcessEnvironment::systemEnvironment(), &out, &err, 5000) != 0) {
        found << QStringLiteral("(could not query %1: %2)").arg(unit, err);  // unknown counts as "has dependents"
        return found;
    }
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0 && !line.mid(eq + 1).trimmed().isEmpty()) {
            found << line.trimmed();
        }
    }
    return found;
}

bool RealOps::waitInactive(const QString &unit, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (true) {
        const UnitState u = unitState(unit);
        if (u.queried && u.activeState != QLatin1String("active") && u.activeState != QLatin1String("activating")
            && u.activeState != QLatin1String("deactivating")) {
            return true;
        }
        if (t.elapsed() > timeoutMs) {
            return false;
        }
        QThread::msleep(300);
    }
}

bool RealOps::stopCaelestiaGracefully(QStringList *warnings, QString *error)
{
    if (!unitState(kCaelestiaUnit).active() && !isProcessRunning(QStringLiteral("quickshell"))) {
        return true;  // nothing to stop
    }
    const QString shellQml = QDir::homePath() + QStringLiteral("/.config/quickshell/caelestia/shell.qml");
    QString out;
    QString err;
    const int rc = runProgram(QStringLiteral("quickshell"), {QStringLiteral("kill"), QStringLiteral("-p"), shellQml},
                              sessionEnvironment(), &out, &err, 10000);
    if (rc == 0 && waitInactive(kCaelestiaUnit, 20000)) {
        return true;
    }
    if (warnings) {
        *warnings << QStringLiteral("graceful quit failed (%1); fell back to a hard stop, so Caelestia may have left its "
                                    "stolen shortcuts and screen corner in place")
                         .arg(rc == 0 ? QStringLiteral("unit stayed active") : err);
    }
    if (!systemctl({QStringLiteral("stop"), kCaelestiaUnit}, error)) {
        return false;
    }
    if (!waitInactive(kCaelestiaUnit, 10000)) {
        if (error) {
            *error = QStringLiteral("caelestia-shell.service did not stop");
        }
        return false;
    }
    return true;
}

void RealOps::reloadOverviewEffect(QStringList *warnings)
{
    QDBusConnection bus = userBusConnection();
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                                                      QStringLiteral("org.kde.kwin.Effects"),
                                                      QStringLiteral("reconfigureEffect"));
    msg.setArguments({QStringLiteral("overview")});
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage && warnings) {
        *warnings << QStringLiteral("could not reload the KWin Overview effect: %1").arg(reply.errorMessage());
    }
}

bool RealOps::logout(QString *error)
{
    QDBusConnection bus = userBusConnection();
    const QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
                                                            QStringLiteral("org.kde.Shutdown"), QStringLiteral("logout"));
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        if (error) {
            *error = QStringLiteral("logout call failed: %1").arg(reply.errorMessage());
        }
        return false;
    }
    return true;
}

bool notificationErrorIsRetryable(const QString &dbusErrorName)
{
    return dbusErrorName == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
        || dbusErrorName == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner");
}

bool sendNotification(const QString &summary, const QString &body, int waitSeconds, QString *error)
{
    QString lastError;
    for (int attempt = 0; attempt <= waitSeconds; ++attempt) {
        QDBusConnection bus = userBusConnection();
        if (bus.isConnected()) {
            QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                              QStringLiteral("/org/freedesktop/Notifications"),
                                                              QStringLiteral("org.freedesktop.Notifications"),
                                                              QStringLiteral("Notify"));
            msg.setArguments({QStringLiteral("Caelestia Switch"), QVariant::fromValue<quint32>(0), QStringLiteral("preferences-desktop"),
                              summary, body, QStringList(), QVariantMap(), -1});
            // A long call timeout: right after a login the server can be busy for several seconds on the 4 GiB laptop.
            const QDBusMessage reply = bus.call(msg, QDBus::Block, 20000);
            if (reply.type() == QDBusMessage::ReplyMessage) {
                return true;
            }
            lastError = reply.errorMessage();
            if (!notificationErrorIsRetryable(reply.errorName())) {
                if (error) {
                    *error = QStringLiteral("notification not confirmed (it may still have been shown): %1").arg(lastError);
                }
                return false;   // never resend: it would show the message twice
            }
        } else {
            lastError = QStringLiteral("cannot connect to the user D-Bus");
        }
        if (attempt < waitSeconds) {
            QThread::sleep(1);
        }
    }
    if (error) {
        *error = QStringLiteral("notification not delivered: %1").arg(lastError);
    }
    return false;
}

} // namespace cs
