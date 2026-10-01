#include "systemd.h"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QList>
#include <QVariantMap>

#include <unistd.h>

namespace cs {

namespace {

constexpr int kTimeoutMs = 3000;
const QString kService = QStringLiteral("org.freedesktop.systemd1");

// The systemd user manager lives on the user's session bus. Over SSH
// DBUS_SESSION_BUS_ADDRESS is often unset, so fall back to the standard socket
// (and do not let Qt try to autolaunch a bus).
QDBusConnection userBus()
{
    if (qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS")) {
        return QDBusConnection::sessionBus();
    }
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/run/user/%1").arg(getuid()));
    return QDBusConnection::connectToBus(QStringLiteral("unix:path=%1/bus").arg(runtime),
                                         QStringLiteral("caelestia-switch-user-bus"));
}

QDBusMessage callMethod(QDBusConnection &bus, const QString &path, const QString &iface,
                        const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, path, iface, method);
    msg.setArguments(args);
    return bus.call(msg, QDBus::Block, kTimeoutMs);
}

} // namespace

bool queryUnit(const QString &unitName, UnitState &out, QString *error)
{
    QDBusConnection bus = userBus();
    if (!bus.isConnected()) {
        if (error) {
            *error = QStringLiteral("cannot connect to the user D-Bus: %1").arg(bus.lastError().message());
        }
        return false;
    }

    const QDBusMessage loaded = callMethod(bus, QStringLiteral("/org/freedesktop/systemd1"),
                                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                                           QStringLiteral("LoadUnit"), {unitName});
    if (loaded.type() != QDBusMessage::ReplyMessage || loaded.arguments().isEmpty()) {
        if (error) {
            *error = QStringLiteral("systemd LoadUnit(%1) failed: %2").arg(unitName, loaded.errorMessage());
        }
        return false;
    }
    const QString unitPath = loaded.arguments().at(0).value<QDBusObjectPath>().path();

    const QDBusMessage props = callMethod(bus, unitPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("GetAll"),
                                          {QStringLiteral("org.freedesktop.systemd1.Unit")});
    if (props.type() != QDBusMessage::ReplyMessage || props.arguments().isEmpty()) {
        if (error) {
            *error = QStringLiteral("systemd GetAll(%1) failed: %2").arg(unitName, props.errorMessage());
        }
        return false;
    }
    const QVariantMap map = qdbus_cast<QVariantMap>(props.arguments().at(0));

    out.queried = true;
    out.loadState = map.value(QStringLiteral("LoadState")).toString();
    out.activeState = map.value(QStringLiteral("ActiveState")).toString();
    out.subState = map.value(QStringLiteral("SubState")).toString();
    out.unitFileState = map.value(QStringLiteral("UnitFileState")).toString();
    return true;
}

bool isProcessRunning(const QString &comm)
{
    const QByteArray target = comm.toUtf8();
    const uint me = getuid();
    const QDir proc(QStringLiteral("/proc"));

    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool isPid = false;
        entry.toInt(&isPid);
        if (!isPid) {
            continue;
        }
        QFile f(QStringLiteral("/proc/%1/status").arg(entry));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        QByteArray name;
        QByteArray state;
        uint uid = static_cast<uint>(-1);
        const QList<QByteArray> lines = f.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("Name:")) {
                name = line.mid(5).trimmed();
            } else if (line.startsWith("State:")) {
                state = line.mid(6).trimmed();
            } else if (line.startsWith("Uid:")) {
                const QList<QByteArray> parts = line.mid(4).simplified().split(' ');
                if (!parts.isEmpty()) {
                    uid = parts.at(0).toUInt();
                }
            }
        }
        if (name == target && uid == me && !state.startsWith('Z')) {
            return true;
        }
    }
    return false;
}

} // namespace cs
