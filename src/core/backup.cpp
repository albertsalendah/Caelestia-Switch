#include "backup.h"

#include "fsutil.h"
#include "install.h"

#include <KConfig>
#include <KConfigGroup>

#include <algorithm>
#include <functional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

namespace cs {

namespace {

constexpr int kManifestVersion = 1;
const QString kRulePrefix = QStringLiteral("caelestia-");

// ---- What is backed up (architecture D11) ----

struct WholeItem {
    QString rel;          // relative to ~/.config
    bool caelestiaOnly;   // only in Caelestia-side snapshots, so a stock restore never wipes Caelestia's own settings
};

const QList<WholeItem> &wholeItems()
{
    static const QList<WholeItem> items = {
        {QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"), false},
        {QStringLiteral("plasmashellrc"), false},
        {QStringLiteral("kscreenlockerrc"), false},
        {QStringLiteral("caelestia/cli.json"), true},
        {QStringLiteral("caelestia/keybinds.json"), true},
        {QStringLiteral("caelestia/shell.json"), true},
        {QStringLiteral("caelestia/monitors"), true},
    };
    return items;
}

struct KeySpec {
    QString file;
    QString group;       // group name, or name prefix when `prefix`
    QStringList keys;    // keys to snapshot; ignored when wholeGroup/prefix
    bool wholeGroup = false;
    bool prefix = false;
};

const QList<KeySpec> &keySpecs()
{
    static const QList<KeySpec> specs = {
        {QStringLiteral("kwinrc"), QStringLiteral("Desktops"), {}, true, false},
        {QStringLiteral("kwinrc"), QStringLiteral("Plugins"),
         {QStringLiteral("quickshell-kde-bridgeEnabled"), QStringLiteral("kwin_workspace_trackerEnabled")}, false, false},
        {QStringLiteral("kwinrc"), QStringLiteral("org.kde.kdecoration2"), {}, true, false},
        {QStringLiteral("plasmarc"), QStringLiteral("Theme"), {QStringLiteral("name")}, false, false},
        {QStringLiteral("plasmarc"), QStringLiteral("OSD"), {QStringLiteral("Enabled"), QStringLiteral("ShowOnActiveScreen")}, false, false},
        {QStringLiteral("kdeglobals"), QStringLiteral("KDE"), {QStringLiteral("widgetStyle"), QStringLiteral("OSDEnabled")}, false, false},
        {QStringLiteral("kdeglobals"), QStringLiteral("General"), {QStringLiteral("ColorScheme")}, false, false},
        {QStringLiteral("kdeglobals"), QStringLiteral("WM"), {}, true, false},
        {QStringLiteral("kdeglobals"), QStringLiteral("Colors:"), {}, true, true},
        {QStringLiteral("plasmanotifyrc"), QStringLiteral("Notifications"), {QStringLiteral("LoudnessChangedOSD")}, false, false},
        {QStringLiteral("powerdevilrc"), QStringLiteral("BrightnessControl"), {QStringLiteral("showOSD")}, false, false},
        {QStringLiteral("powerdevilrc"), QStringLiteral("AC"), {QStringLiteral("brightnessosd")}, false, false},
        {QStringLiteral("kmixrc"), QStringLiteral("Global"), {QStringLiteral("ShowOSD")}, false, false},
        {QStringLiteral("ksplashrc"), QStringLiteral("KSplash"), {QStringLiteral("Engine")}, false, false},
    };
    return specs;
}

const QString kRulesFile = QStringLiteral("kwinrulesrc");  // special case, see buildRulesRecord

// ---- File helpers ----

bool copyRecursively(const QString &src, const QString &dst)
{
    const QFileInfo info(src);
    if (info.isDir()) {
        if (!QDir().mkpath(dst)) {
            return false;
        }
        const QStringList names = QDir(src).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QString &name : names) {
            if (!copyRecursively(src + QLatin1Char('/') + name, dst + QLatin1Char('/') + name)) {
                return false;
            }
        }
        return true;
    }
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    return QFile::copy(src, dst);
}

bool removePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        return true;
    }
    if (info.isDir() && !info.isSymLink()) {
        return QDir(path).removeRecursively();
    }
    return QFile::remove(path);
}

// Replaces dst with a copy of src (file or directory) without leaving a half-written dst.
bool replaceWithCopy(const QString &src, const QString &dst)
{
    const QString tmp = dst + QStringLiteral(".cs-tmp");
    removePath(tmp);
    if (!copyRecursively(src, tmp)) {
        removePath(tmp);
        return false;
    }
    if (!removePath(dst)) {
        removePath(tmp);
        return false;
    }
    return QFile::rename(tmp, dst);
}

bool safeRelative(const QString &rel)
{
    return !rel.isEmpty() && !rel.startsWith(QLatin1Char('/')) && !rel.contains(QLatin1String(".."));
}

bool writeJson(const QString &path, const QJsonObject &obj)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented)) >= 0;
}

QJsonObject readJson(const QString &path)
{
    return QJsonDocument::fromJson(readTextFile(path).toUtf8()).object();
}

QString plasmaVersion()
{
    // `plasmashell --version` crashes when started by the app, so read the version that the
    // installed plasma-workspace ships in its CMake package instead (no process involved).
    const QStringList candidates = {QStringLiteral("/usr/lib/cmake/LibKWorkspace/LibKWorkspaceConfigVersion.cmake"),
                                    QStringLiteral("/usr/lib64/cmake/LibKWorkspace/LibKWorkspaceConfigVersion.cmake")};
    for (const QString &path : candidates) {
        const QString v = parsePlasmaVersion(readTextFile(path));
        if (!v.isEmpty()) {
            return v;
        }
    }
    return QStringLiteral("unknown");
}

// ---- Key-level snapshots ----

QJsonObject entriesToJson(KConfig &cfg, const QString &group)
{
    QJsonObject o;
    const QMap<QString, QString> m = KConfigGroup(&cfg, group).entryMap();
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        o.insert(it.key(), it.value());
    }
    return o;
}

void writeEntries(KConfig &cfg, const QString &group, const QJsonObject &entries)
{
    KConfigGroup g(&cfg, group);
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        g.writeEntry(it.key().toUtf8().constData(), it.value().toString());
    }
}

QStringList groupsWithPrefix(const KConfig &cfg, const QString &prefix)
{
    QStringList out;
    const QStringList all = cfg.groupList();
    for (const QString &g : all) {
        if (g.startsWith(prefix)) {
            out << g;
        }
    }
    return out;
}

QJsonObject buildRecord(KConfig &cfg, const KeySpec &spec)
{
    QJsonObject rec;
    if (spec.prefix) {
        QJsonObject groups;
        for (const QString &g : groupsWithPrefix(cfg, spec.group)) {
            groups.insert(g, entriesToJson(cfg, g));
        }
        rec.insert(QStringLiteral("type"), QStringLiteral("prefix"));
        rec.insert(QStringLiteral("prefix"), spec.group);
        rec.insert(QStringLiteral("groups"), groups);
    } else if (spec.wholeGroup) {
        rec.insert(QStringLiteral("type"), QStringLiteral("group"));
        rec.insert(QStringLiteral("group"), spec.group);
        rec.insert(QStringLiteral("present"), cfg.hasGroup(spec.group));
        rec.insert(QStringLiteral("entries"), entriesToJson(cfg, spec.group));
    } else {
        const QMap<QString, QString> present = KConfigGroup(&cfg, spec.group).entryMap();
        QJsonObject values;
        for (const QString &k : spec.keys) {
            values.insert(k, present.contains(k) ? QJsonValue(present.value(k)) : QJsonValue(QJsonValue::Null));
        }
        rec.insert(QStringLiteral("type"), QStringLiteral("keys"));
        rec.insert(QStringLiteral("group"), spec.group);
        rec.insert(QStringLiteral("values"), values);
    }
    return rec;
}

// kwinrulesrc is shared with the user's own window rules: snapshot only the caelestia-*
// groups and their names in [General] rules, never the whole list.
QStringList ruleNames(KConfig &cfg)
{
    const QString list = KConfigGroup(&cfg, QStringLiteral("General")).readEntry("rules", QString());
    QStringList names;
    const QStringList parts = list.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        names << p.trimmed();
    }
    return names;
}

QJsonObject buildRulesRecord(KConfig &cfg)
{
    QJsonObject groups;
    for (const QString &g : groupsWithPrefix(cfg, kRulePrefix)) {
        groups.insert(g, entriesToJson(cfg, g));
    }
    QJsonArray names;
    for (const QString &n : ruleNames(cfg)) {
        if (n.startsWith(kRulePrefix)) {
            names.append(n);
        }
    }
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("rules")},
                       {QStringLiteral("groups"), groups},
                       {QStringLiteral("names"), names}};
}

void applyRecord(KConfig &cfg, const QJsonObject &rec)
{
    const QString type = rec.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("group")) {
        const QString group = rec.value(QStringLiteral("group")).toString();
        cfg.deleteGroup(group);
        if (rec.value(QStringLiteral("present")).toBool()) {
            writeEntries(cfg, group, rec.value(QStringLiteral("entries")).toObject());
        }
    } else if (type == QLatin1String("keys")) {
        KConfigGroup g(&cfg, rec.value(QStringLiteral("group")).toString());
        const QJsonObject values = rec.value(QStringLiteral("values")).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QByteArray key = it.key().toUtf8();
            if (it.value().isNull()) {
                g.deleteEntry(key.constData());
            } else {
                g.writeEntry(key.constData(), it.value().toString());
            }
        }
    } else if (type == QLatin1String("prefix")) {
        for (const QString &g : groupsWithPrefix(cfg, rec.value(QStringLiteral("prefix")).toString())) {
            cfg.deleteGroup(g);
        }
        const QJsonObject groups = rec.value(QStringLiteral("groups")).toObject();
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            writeEntries(cfg, it.key(), it.value().toObject());
        }
    } else if (type == QLatin1String("rules")) {
        for (const QString &g : groupsWithPrefix(cfg, kRulePrefix)) {
            cfg.deleteGroup(g);
        }
        const QJsonObject groups = rec.value(QStringLiteral("groups")).toObject();
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            writeEntries(cfg, it.key(), it.value().toObject());
        }
        // Keep the user's own rules: drop only caelestia-* names from the current list,
        // then add the snapshot's caelestia-* names.
        QStringList list;
        const QStringList current = ruleNames(cfg);
        for (const QString &n : current) {
            if (!n.startsWith(kRulePrefix)) {
                list << n;
            }
        }
        const QJsonArray names = rec.value(QStringLiteral("names")).toArray();
        for (const QJsonValue &n : names) {
            if (!list.contains(n.toString())) {
                list << n.toString();
            }
        }
        KConfigGroup general(&cfg, QStringLiteral("General"));
        if (list.isEmpty()) {
            general.deleteEntry("rules");
            general.deleteEntry("count");
        } else {
            general.writeEntry("rules", list.join(QLatin1Char(',')));
            general.writeEntry("count", QString::number(list.size()));
        }
    }
}

QString newId(const QString &sideDir)
{
    const QString base = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString id = base;
    for (int n = 2; QFileInfo::exists(sideDir + QLatin1Char('/') + id) || QFileInfo::exists(sideDir + QLatin1Char('/') + id + QStringLiteral(".partial")); ++n) {
        id = QStringLiteral("%1_%2").arg(base).arg(n);
    }
    return id;
}

bool parseRef(const QString &ref, Side *side, QString *id)
{
    const QStringList parts = ref.split(QLatin1Char('/'));
    static const QRegularExpression idRe(QStringLiteral("^[0-9]{8}_[0-9]{6}(_[0-9]+)?$"));
    if (parts.size() != 2 || !parseSide(parts.at(0), side) || !idRe.match(parts.at(1)).hasMatch()) {
        return false;
    }
    *id = parts.at(1);
    return true;
}

} // namespace

QString parsePlasmaVersion(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^set\\(PACKAGE_VERSION\\s+\"([^\"]+)\"\\)"), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString sideKey(Side side)
{
    return side == Side::Stock ? QStringLiteral("stock") : QStringLiteral("caelestia");
}

bool parseSide(const QString &text, Side *out)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("stock")) {
        *out = Side::Stock;
        return true;
    }
    if (t == QLatin1String("caelestia")) {
        *out = Side::Caelestia;
        return true;
    }
    return false;
}

BackupPaths BackupPaths::defaults()
{
    BackupPaths p;
    p.home = QDir::homePath();
    p.configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    p.dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/caelestia-switch/backups");
    p.appConfigDir = cs::appConfigDir();
    return p;
}

OpResult createBackup(Side side, const QString &trigger, const BackupPaths &paths, QString *refOut)
{
    OpResult res;
    const QString sideDir = paths.dataRoot + QLatin1Char('/') + sideKey(side);
    if (!QDir().mkpath(sideDir)) {
        return {false, QStringLiteral("cannot create %1").arg(sideDir), {}};
    }
    const QString id = newId(sideDir);
    const QString finalDir = sideDir + QLatin1Char('/') + id;
    const QString work = finalDir + QStringLiteral(".partial");
    const auto fail = [&](const QString &why) {
        removePath(work);
        return OpResult{false, why, res.warnings};
    };

    // Whole-file items.
    QJsonArray files;
    for (const WholeItem &item : wholeItems()) {
        if (item.caelestiaOnly && side == Side::Stock) {
            continue;
        }
        const QString src = paths.configHome + QLatin1Char('/') + item.rel;
        const QFileInfo info(src);
        const bool present = info.exists();
        if (present && !copyRecursively(src, work + QStringLiteral("/files/") + item.rel)) {
            return fail(QStringLiteral("cannot copy %1").arg(src));
        }
        files.append(QJsonObject{{QStringLiteral("path"), item.rel},
                                 {QStringLiteral("present"), present},
                                 {QStringLiteral("isDir"), present && info.isDir()}});
    }

    // Key-level items, one JSON file per config file.
    QMap<QString, QJsonArray> byFile;
    QStringList fileOrder;
    for (const KeySpec &spec : keySpecs()) {
        if (!byFile.contains(spec.file)) {
            fileOrder << spec.file;
        }
        KConfig cfg(paths.configHome + QLatin1Char('/') + spec.file, KConfig::SimpleConfig);
        byFile[spec.file].append(buildRecord(cfg, spec));
    }
    {
        fileOrder << kRulesFile;
        KConfig cfg(paths.configHome + QLatin1Char('/') + kRulesFile, KConfig::SimpleConfig);
        byFile[kRulesFile].append(buildRulesRecord(cfg));
    }
    for (const QString &file : std::as_const(fileOrder)) {
        if (!writeJson(work + QStringLiteral("/keys/") + file + QStringLiteral(".json"),
                       QJsonObject{{QStringLiteral("records"), byFile.value(file)}})) {
            return fail(QStringLiteral("cannot write key snapshot for %1").arg(file));
        }
    }

    // Manifest.
    const InstallInfo install = detectInstall(paths.home, paths.appConfigDir, qEnvironmentVariable("CAELESTIA_DIR"));
    const QJsonObject manifest{
        {QStringLiteral("manifestVersion"), kManifestVersion},
        {QStringLiteral("side"), sideKey(side)},
        {QStringLiteral("id"), id},
        {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("trigger"), trigger},
        {QStringLiteral("caelestiaCommit"), install.commit},
        {QStringLiteral("caelestiaVersion"), install.version},
        {QStringLiteral("plasmaVersion"), paths.lookupPlasmaVersion ? plasmaVersion() : QStringLiteral("unknown")},
        {QStringLiteral("files"), files},
        {QStringLiteral("keyFiles"), QJsonArray::fromStringList(fileOrder)},
    };
    if (!writeJson(work + QStringLiteral("/manifest.json"), manifest)) {
        return fail(QStringLiteral("cannot write manifest"));
    }
    if (!QDir().rename(work, finalDir)) {
        return fail(QStringLiteral("cannot finalize %1").arg(finalDir));
    }
    if (refOut) {
        *refOut = sideKey(side) + QLatin1Char('/') + id;
    }
    return res;
}

QList<BackupInfo> listBackups(Side side, const BackupPaths &paths)
{
    QList<BackupInfo> out;
    const QString sideDir = paths.dataRoot + QLatin1Char('/') + sideKey(side);
    QStringList ids = QDir(sideDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(ids.begin(), ids.end(), std::greater<QString>());
    for (const QString &id : std::as_const(ids)) {
        const QString dir = sideDir + QLatin1Char('/') + id;
        const QJsonObject m = readJson(dir + QStringLiteral("/manifest.json"));
        if (m.isEmpty() || m.value(QStringLiteral("manifestVersion")).toInt() != kManifestVersion) {
            continue;  // partial or unknown backup
        }
        BackupInfo b;
        b.side = side;
        b.id = id;
        b.dir = dir;
        b.created = QDateTime::fromString(m.value(QStringLiteral("created")).toString(), Qt::ISODate);
        b.trigger = m.value(QStringLiteral("trigger")).toString();
        b.caelestiaCommit = m.value(QStringLiteral("caelestiaCommit")).toString();
        b.caelestiaVersion = m.value(QStringLiteral("caelestiaVersion")).toString();
        b.plasmaVersion = m.value(QStringLiteral("plasmaVersion")).toString();
        out << b;
    }
    return out;
}

QList<BackupInfo> listBackups(const BackupPaths &paths)
{
    QList<BackupInfo> all = listBackups(Side::Stock, paths) + listBackups(Side::Caelestia, paths);
    std::sort(all.begin(), all.end(), [](const BackupInfo &a, const BackupInfo &b) { return a.id > b.id; });
    return all;
}

OpResult restoreBackup(const QString &ref, const BackupPaths &paths)
{
    OpResult res;
    Side side;
    QString id;
    if (!parseRef(ref, &side, &id)) {
        return {false, QStringLiteral("invalid backup reference '%1' (expected side/YYYYMMDD_HHMMSS)").arg(ref), {}};
    }
    const QString dir = paths.dataRoot + QLatin1Char('/') + sideKey(side) + QLatin1Char('/') + id;
    const QJsonObject manifest = readJson(dir + QStringLiteral("/manifest.json"));
    if (manifest.isEmpty() || manifest.value(QStringLiteral("manifestVersion")).toInt() != kManifestVersion) {
        return {false, QStringLiteral("backup %1 not found or unreadable").arg(ref), {}};
    }

    const auto softFail = [&res](const QString &why) {
        res.ok = false;
        res.warnings << why;
    };

    // Whole-file items: copy back, or remove if the file did not exist at backup time.
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &v : files) {
        const QJsonObject f = v.toObject();
        const QString rel = f.value(QStringLiteral("path")).toString();
        if (!safeRelative(rel)) {
            softFail(QStringLiteral("skipped unsafe path '%1'").arg(rel));
            continue;
        }
        const QString target = paths.configHome + QLatin1Char('/') + rel;
        if (f.value(QStringLiteral("present")).toBool()) {
            const QString src = dir + QStringLiteral("/files/") + rel;
            if (!replaceWithCopy(src, target)) {
                softFail(QStringLiteral("cannot restore %1").arg(rel));
            }
        } else if (!removePath(target)) {
            softFail(QStringLiteral("cannot remove %1").arg(rel));
        }
    }

    // Key-level items.
    const QJsonArray keyFiles = manifest.value(QStringLiteral("keyFiles")).toArray();
    for (const QJsonValue &kf : keyFiles) {
        const QString file = kf.toString();
        if (!safeRelative(file)) {
            softFail(QStringLiteral("skipped unsafe file '%1'").arg(file));
            continue;
        }
        const QJsonObject snap = readJson(dir + QStringLiteral("/keys/") + file + QStringLiteral(".json"));
        const QJsonArray records = snap.value(QStringLiteral("records")).toArray();
        if (records.isEmpty()) {
            softFail(QStringLiteral("no key snapshot for %1").arg(file));
            continue;
        }
        KConfig cfg(paths.configHome + QLatin1Char('/') + file, KConfig::SimpleConfig);
        for (const QJsonValue &r : records) {
            applyRecord(cfg, r.toObject());
        }
        if (!cfg.sync()) {
            softFail(QStringLiteral("cannot write %1").arg(file));
        }
    }

    if (!res.ok) {
        res.error = QStringLiteral("restore of %1 finished with problems").arg(ref);
    }
    return res;
}

} // namespace cs
