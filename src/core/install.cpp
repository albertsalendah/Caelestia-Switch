#include "install.h"

#include "fsutil.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace cs {

namespace {

bool isHex(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]+$"));
    return re.match(s).hasMatch();
}

bool looksLikeFullSha(const QString &s)
{
    return s.size() >= 40 && isHex(s);
}

struct Marker {
    QString source;
    QString version;
    QString commit;
    QString checkout;
};

Marker readMarker(const QString &appConfigDir)
{
    Marker m;
    const QJsonDocument doc = QJsonDocument::fromJson(readTextFile(appConfigDir + QStringLiteral("/install.json")).toUtf8());
    if (!doc.isObject()) {
        return m;
    }
    const QJsonObject o = doc.object();
    m.source = o.value(QStringLiteral("source")).toString().trimmed();
    m.version = o.value(QStringLiteral("version")).toString().trimmed();
    m.commit = o.value(QStringLiteral("commit")).toString().trimmed();
    m.checkout = o.value(QStringLiteral("checkout")).toString().trimmed();
    return m;
}

} // namespace

QString parseVersionEnv(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*VERSION\\s*=\\s*[\"']?([A-Za-z0-9._-]+)"),
                                       QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString parseOriginUrl(const QString &gitConfigText)
{
    bool inOrigin = false;
    const QStringList lines = gitConfigText.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            QString header = line.simplified();
            header.remove(QLatin1Char(' '));
            inOrigin = (header == QLatin1String("[remote\"origin\"]"));
            continue;
        }
        if (!inOrigin) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0 && line.left(eq).trimmed() == QLatin1String("url")) {
            return line.mid(eq + 1).trimmed();
        }
    }
    return {};
}

QString normalizeRepoUrl(const QString &url)
{
    QString s = url.trimmed().toLower();
    bool hadScheme = false;
    for (const QString &scheme : {QStringLiteral("git+ssh://"), QStringLiteral("ssh://"), QStringLiteral("https://"),
                                  QStringLiteral("http://"), QStringLiteral("git://")}) {
        if (s.startsWith(scheme)) {
            s = s.mid(scheme.size());
            hadScheme = true;
            break;
        }
    }
    // Drop "user@" in front of the host.
    const int at = s.indexOf(QLatin1Char('@'));
    const int firstSlash = s.indexOf(QLatin1Char('/'));
    if (at >= 0 && (firstSlash < 0 || at < firstSlash)) {
        s = s.mid(at + 1);
    }
    // scp-style "host:owner/repo" -> "host/owner/repo".
    if (!hadScheme) {
        const int colon = s.indexOf(QLatin1Char(':'));
        const int slash = s.indexOf(QLatin1Char('/'));
        if (colon >= 0 && (slash < 0 || colon < slash)) {
            s[colon] = QLatin1Char('/');
        }
    }
    while (s.endsWith(QLatin1Char('/'))) {
        s.chop(1);
    }
    if (s.endsWith(QLatin1String(".git"))) {
        s.chop(4);
    }
    while (s.endsWith(QLatin1Char('/'))) {
        s.chop(1);
    }
    return s;
}

QString sourceFromUrl(const QString &url)
{
    const QString n = normalizeRepoUrl(url);
    if (n == QLatin1String("github.com/ladybug-me/caelestia-kde")) {
        return QStringLiteral("ladybug-me");
    }
    if (n == QLatin1String("github.com/albertsalendah/caelestia-kde")) {
        return QStringLiteral("fork");
    }
    return QStringLiteral("unknown");
}

QString resolveHead(const QString &checkoutDir)
{
    const QString gitDir = checkoutDir + QStringLiteral("/.git");
    if (!QFileInfo(gitDir).isDir()) {
        return {};
    }
    const QString head = readFirstLine(gitDir + QStringLiteral("/HEAD"));
    if (!head.startsWith(QLatin1String("ref:"))) {
        return looksLikeFullSha(head) ? head.toLower() : QString();
    }

    const QString ref = head.mid(4).trimmed();
    if (ref.isEmpty() || ref.contains(QLatin1String(".."))) {
        return {};
    }
    const QString loose = readFirstLine(gitDir + QLatin1Char('/') + ref);
    if (looksLikeFullSha(loose)) {
        return loose.toLower();
    }
    const QStringList packed = readTextFile(gitDir + QStringLiteral("/packed-refs")).split(QLatin1Char('\n'));
    for (const QString &raw : packed) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('^'))) {
            continue;
        }
        const QStringList parts = line.split(QLatin1Char(' '));
        if (parts.size() == 2 && parts.at(1) == ref && looksLikeFullSha(parts.at(0))) {
            return parts.at(0).toLower();
        }
    }
    return {};
}

bool sameCommit(const QString &a0, const QString &b0)
{
    const QString a = a0.trimmed().toLower();
    const QString b = b0.trimmed().toLower();
    if (a.isEmpty() || b.isEmpty() || !isHex(a) || !isHex(b)) {
        return false;
    }
    if (a == b) {
        return true;
    }
    const QString &shorter = a.size() < b.size() ? a : b;
    const QString &longer = a.size() < b.size() ? b : a;
    return shorter.size() >= 7 && longer.startsWith(shorter);
}

InstallInfo detectInstall(const QString &home, const QString &appConfigDir, const QString &caelestiaDirEnv)
{
    InstallInfo info;
    const QString cfg = home + QStringLiteral("/.config/quickshell/caelestia");

    info.installed = QFileInfo::exists(cfg + QStringLiteral("/shell.qml"))
        && QFileInfo::exists(home + QStringLiteral("/.config/systemd/user/caelestia-shell.service"));
    info.commit = readFirstLine(cfg + QStringLiteral("/.current_commit"));
    if (!info.installed) {
        return info;
    }

    const auto setSource = [&info](const QString &source, const QString &from) {
        if (info.source == QLatin1String("unknown") && !source.isEmpty() && source != QLatin1String("unknown")) {
            info.source = source;
            info.sourceFrom = from;
        }
    };
    const auto setVersion = [&info](const QString &version, const QString &from) {
        if (info.version == QLatin1String("unknown") && !version.isEmpty()) {
            info.version = version;
            info.versionFrom = from;
        }
    };

    if (info.commit.isEmpty()) {
        // Without a recorded commit nothing can be tied to this install.
        setVersion(parseVersionEnv(readTextFile(cfg + QStringLiteral("/.current_version"))),
                   QStringLiteral(".current_version"));
        return info;
    }

    // 1. App-written marker, ignored if the install changed outside the app.
    const Marker marker = readMarker(appConfigDir);
    if (sameCommit(marker.commit, info.commit)) {
        setSource(marker.source, QStringLiteral("marker"));
        setVersion(marker.version, QStringLiteral("marker"));
        if (info.checkout.isEmpty()) {
            info.checkout = marker.checkout;
        }
    }

    // Version recorded by the installer next to .current_commit.
    setVersion(parseVersionEnv(readTextFile(cfg + QStringLiteral("/.current_version"))),
               QStringLiteral(".current_version"));

    // 2. Checkout discovery: HEAD must equal .current_commit.
    QStringList candidates;
    if (!caelestiaDirEnv.trimmed().isEmpty()) {
        candidates << caelestiaDirEnv.trimmed();
    }
    candidates << home + QStringLiteral("/caelestia-kde");
    for (const QString &dir : std::as_const(candidates)) {
        if (!sameCommit(resolveHead(dir), info.commit)) {
            continue;
        }
        const QString origin = parseOriginUrl(readTextFile(dir + QStringLiteral("/.git/config")));
        info.origin = normalizeRepoUrl(origin);
        info.checkout = dir;
        setSource(sourceFromUrl(origin), QStringLiteral("checkout"));
        setVersion(parseVersionEnv(readTextFile(dir + QStringLiteral("/.github/version.env"))),
                   QStringLiteral("checkout"));
        break;
    }
    return info;
}

InstallInfo detectInstall()
{
    return detectInstall(QDir::homePath(), appConfigDir(), qEnvironmentVariable("CAELESTIA_DIR"));
}

} // namespace cs
