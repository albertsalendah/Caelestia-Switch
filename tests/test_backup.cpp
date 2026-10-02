#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "backup.h"

using namespace cs;

namespace {

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(content.toUtf8());
}

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QStringLiteral("<missing>");
}

// Value of group/key in an INI file, "<missing>" if absent.
QString val(const QString &file, const QString &group, const QString &key)
{
    KConfig cfg(file, KConfig::SimpleConfig);
    KConfigGroup g(&cfg, group);
    const QMap<QString, QString> m = g.entryMap();
    return m.contains(key) ? m.value(key) : QStringLiteral("<missing>");
}

bool hasGroup(const QString &file, const QString &group)
{
    KConfig cfg(file, KConfig::SimpleConfig);
    return cfg.hasGroup(group);
}

struct Fake {
    QTemporaryDir dir;
    BackupPaths paths;
    Fake()
    {
        paths.home = dir.path();
        paths.configHome = dir.path() + QStringLiteral("/.config");
        paths.dataRoot = dir.path() + QStringLiteral("/data/backups");
        paths.appConfigDir = dir.path() + QStringLiteral("/appcfg");
        paths.lookupPlasmaVersion = false;  // would spawn plasmashell
    }
    QString cfg(const QString &rel) const { return paths.configHome + QLatin1Char('/') + rel; }
};

} // namespace

class TestBackup : public QObject
{
    Q_OBJECT

private slots:
    void sideParsing();
    void plasmaVersionParsing();
    void wholeFilesRoundTrip();
    void caelestiaSideOwnsCaelestiaDir();
    void keyLevelRoundTrip();
    void kwinrulesKeepsUserRules();
    void listAndRefs();
    void badRefs();
};

void TestBackup::plasmaVersionParsing()
{
    const QString file = QStringLiteral("# The created file sets PACKAGE_VERSION_EXACT if the current version string and\n"
                                        "# PACKAGE_VERSION_COMPATIBLE if the current version is >= requested version.\n"
                                        "set(PACKAGE_VERSION \"6.7.5\")\n\nif(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)\n");
    QCOMPARE(parsePlasmaVersion(file), QStringLiteral("6.7.5"));
    QCOMPARE(parsePlasmaVersion(QStringLiteral("# only comments\n")), QString());
    QCOMPARE(parsePlasmaVersion(QString()), QString());
}

void TestBackup::sideParsing()
{
    Side s;
    QVERIFY(parseSide(QStringLiteral(" Stock "), &s));
    QCOMPARE(s, Side::Stock);
    QVERIFY(parseSide(QStringLiteral("CAELESTIA"), &s));
    QCOMPARE(s, Side::Caelestia);
    QVERIFY(!parseSide(QStringLiteral("other"), &s));
}

void TestBackup::wholeFilesRoundTrip()
{
    Fake f;
    writeFile(f.cfg("plasmashellrc"), QStringLiteral("[Shell]\nShellPackage=org.kde.plasma.desktop\n"));
    writeFile(f.cfg("plasma-org.kde.plasma.desktop-appletsrc"), QStringLiteral("[Containments][1]\nplugin=org.kde.panel\n"));
    // kscreenlockerrc does not exist at backup time.
    writeFile(f.cfg("caelestia/shell.json"), QStringLiteral("{\"mine\":true}\n"));

    QString ref;
    const OpResult b = createBackup(Side::Stock, QStringLiteral("manual"), f.paths, &ref);
    QVERIFY2(b.ok, qPrintable(b.error));
    QVERIFY(ref.startsWith(QStringLiteral("stock/")));
    // The stock side never snapshots Caelestia's own settings.
    QVERIFY(!QFileInfo::exists(f.paths.dataRoot + QStringLiteral("/") + ref + QStringLiteral("/files/caelestia")));

    // The installer / Caelestia changes things.
    writeFile(f.cfg("plasmashellrc"), QStringLiteral("[Shell]\nShellPackage=caelestia.desktop\n"));
    QFile::remove(f.cfg("plasma-org.kde.plasma.desktop-appletsrc"));
    writeFile(f.cfg("kscreenlockerrc"), QStringLiteral("[Greeter]\nWallpaperPlugin=org.kde.image\n"));
    writeFile(f.cfg("caelestia/shell.json"), QStringLiteral("{\"mine\":false}\n"));

    const OpResult r = restoreBackup(ref, f.paths);
    QVERIFY2(r.ok, qPrintable(r.error + r.warnings.join(';')));
    QCOMPARE(val(f.cfg("plasmashellrc"), "Shell", "ShellPackage"), QStringLiteral("org.kde.plasma.desktop"));
    QVERIFY(QFileInfo::exists(f.cfg("plasma-org.kde.plasma.desktop-appletsrc")));
    QVERIFY(!QFileInfo::exists(f.cfg("kscreenlockerrc")));                    // absent then, absent again
    QCOMPARE(readFile(f.cfg("caelestia/shell.json")), QStringLiteral("{\"mine\":false}\n"));  // untouched by a stock restore
}

void TestBackup::caelestiaSideOwnsCaelestiaDir()
{
    Fake f;
    writeFile(f.cfg("caelestia/cli.json"), QStringLiteral("{\"a\":1}\n"));
    writeFile(f.cfg("caelestia/shell.json"), QStringLiteral("{\"b\":2}\n"));
    writeFile(f.cfg("caelestia/monitors/DP-1.json"), QStringLiteral("{\"m\":1}\n"));
    writeFile(f.cfg("caelestia/stolen-screen-edges.json"), QStringLiteral("[]\n"));

    QString ref;
    QVERIFY(createBackup(Side::Caelestia, QStringLiteral("auto-leave"), f.paths, &ref).ok);
    // Recovery files are never part of a backup.
    QVERIFY(!QFileInfo::exists(f.paths.dataRoot + QStringLiteral("/") + ref + QStringLiteral("/files/caelestia/stolen-screen-edges.json")));

    writeFile(f.cfg("caelestia/shell.json"), QStringLiteral("{\"b\":99}\n"));
    QDir(f.cfg("caelestia/monitors")).removeRecursively();
    QFile::remove(f.cfg("caelestia/keybinds.json"));

    QVERIFY(restoreBackup(ref, f.paths).ok);
    QCOMPARE(readFile(f.cfg("caelestia/shell.json")), QStringLiteral("{\"b\":2}\n"));
    QCOMPARE(readFile(f.cfg("caelestia/monitors/DP-1.json")), QStringLiteral("{\"m\":1}\n"));
    QVERIFY(!QFileInfo::exists(f.cfg("caelestia/keybinds.json")));            // was absent at backup time
    QVERIFY(QFileInfo::exists(f.cfg("caelestia/stolen-screen-edges.json")));  // not ours to touch
}

void TestBackup::keyLevelRoundTrip()
{
    Fake f;
    // Stock-like state.
    writeFile(f.cfg("kwinrc"), QStringLiteral("[Compositing]\nBackend=OpenGL\n\n[Desktops]\nNumber=1\nRows=1\n\n[Plugins]\nkwin_workspace_trackerEnabled=false\n"));
    writeFile(f.cfg("kdeglobals"), QStringLiteral("[General]\nColorScheme=BreezeDark\nfont=Noto,10\n\n[KDE]\nwidgetStyle=Breeze\n\n[Colors:Window]\nBackgroundNormal=30,34,51\n"));
    writeFile(f.cfg("plasmarc"), QStringLiteral("[OSD]\nEnabled=true\n"));

    QString ref;
    QVERIFY(createBackup(Side::Stock, QStringLiteral("manual"), f.paths, &ref).ok);

    // Installer / Caelestia changes, plus things the user changed meanwhile.
    writeFile(f.cfg("kwinrc"), QStringLiteral("[Compositing]\nBackend=Vulkan\n\n[Desktops]\nNumber=5\nRows=1\nName_5=Desktop 5\n\n[Plugins]\nkwin_workspace_trackerEnabled=true\nquickshell-kde-bridgeEnabled=false\nblurEnabled=true\n\n[org.kde.kdecoration2]\nlibrary=org.kde.darkly\n\n[Effect-overview]\nBorderActivate=9\n"));
    writeFile(f.cfg("kdeglobals"), QStringLiteral("[General]\nColorScheme=Darkly\nfont=Inter,11\n\n[KDE]\nwidgetStyle=darkly\nOSDEnabled=false\n\n[Colors:Window]\nBackgroundNormal=#ece7e5\n\n[Colors:Header]\nBackgroundNormal=#fff\n"));
    writeFile(f.cfg("plasmarc"), QStringLiteral("[OSD]\nEnabled=false\nShowOnActiveScreen=false\n\n[Theme]\nname=darkly\n"));

    const OpResult r = restoreBackup(ref, f.paths);
    QVERIFY2(r.ok, qPrintable(r.error + r.warnings.join(';')));

    // Installer-owned values are back, and keys that did not exist then are gone.
    QCOMPARE(val(f.cfg("kwinrc"), "Desktops", "Number"), QStringLiteral("1"));
    QCOMPARE(val(f.cfg("kwinrc"), "Desktops", "Name_5"), QStringLiteral("<missing>"));
    QCOMPARE(val(f.cfg("kwinrc"), "Plugins", "kwin_workspace_trackerEnabled"), QStringLiteral("false"));
    QCOMPARE(val(f.cfg("kwinrc"), "Plugins", "quickshell-kde-bridgeEnabled"), QStringLiteral("<missing>"));
    QVERIFY(!hasGroup(f.cfg("kwinrc"), "org.kde.kdecoration2"));
    QCOMPARE(val(f.cfg("kdeglobals"), "General", "ColorScheme"), QStringLiteral("BreezeDark"));
    QCOMPARE(val(f.cfg("kdeglobals"), "KDE", "widgetStyle"), QStringLiteral("Breeze"));
    QCOMPARE(val(f.cfg("kdeglobals"), "KDE", "OSDEnabled"), QStringLiteral("<missing>"));
    QCOMPARE(val(f.cfg("kdeglobals"), "Colors:Window", "BackgroundNormal"), QStringLiteral("30,34,51"));
    QVERIFY(!hasGroup(f.cfg("kdeglobals"), "Colors:Header"));
    QCOMPARE(val(f.cfg("plasmarc"), "OSD", "Enabled"), QStringLiteral("true"));
    QCOMPARE(val(f.cfg("plasmarc"), "OSD", "ShowOnActiveScreen"), QStringLiteral("<missing>"));
    QVERIFY(!hasGroup(f.cfg("plasmarc"), "Theme"));

    // Everything outside the key list is left exactly as the user has it now.
    QCOMPARE(val(f.cfg("kwinrc"), "Compositing", "Backend"), QStringLiteral("Vulkan"));
    QCOMPARE(val(f.cfg("kwinrc"), "Plugins", "blurEnabled"), QStringLiteral("true"));
    QCOMPARE(val(f.cfg("kwinrc"), "Effect-overview", "BorderActivate"), QStringLiteral("9"));  // runtime state, not ours
    QCOMPARE(val(f.cfg("kdeglobals"), "General", "font"), QStringLiteral("Inter,11"));
}

void TestBackup::kwinrulesKeepsUserRules()
{
    Fake f;
    // Stock side: the user has one rule of their own.
    writeFile(f.cfg("kwinrulesrc"), QStringLiteral("[General]\ncount=1\nrules=my-rule\n\n[my-rule]\nDescription=mine\n"));
    QString stockRef;
    QVERIFY(createBackup(Side::Stock, QStringLiteral("manual"), f.paths, &stockRef).ok);

    // The installer adds the caelestia rules; the user adds a second rule of their own.
    writeFile(f.cfg("kwinrulesrc"),
              QStringLiteral("[General]\ncount=5\nrules=my-rule,caelestia-dialogs,caelestia-opacity,caelestia-pip,my-rule2\n\n"
                             "[my-rule]\nDescription=mine\n\n[my-rule2]\nDescription=mine too\n\n"
                             "[caelestia-dialogs]\nDescription=c1\n\n[caelestia-opacity]\nDescription=c2\n\n[caelestia-pip]\nDescription=c3\n"));
    QString caeRef;
    QVERIFY(createBackup(Side::Caelestia, QStringLiteral("auto-leave"), f.paths, &caeRef).ok);

    // Back to stock: caelestia rules go, both user rules stay, count is recomputed.
    QVERIFY(restoreBackup(stockRef, f.paths).ok);
    const QString file = f.cfg("kwinrulesrc");
    QCOMPARE(val(file, "General", "rules"), QStringLiteral("my-rule,my-rule2"));
    QCOMPARE(val(file, "General", "count"), QStringLiteral("2"));
    QVERIFY(!hasGroup(file, "caelestia-pip"));
    QVERIFY(!hasGroup(file, "caelestia-dialogs"));
    QVERIFY(hasGroup(file, "my-rule2"));

    // And back to Caelestia: its rules return, the user's stay.
    QVERIFY(restoreBackup(caeRef, f.paths).ok);
    QCOMPARE(val(file, "General", "rules"), QStringLiteral("my-rule,my-rule2,caelestia-dialogs,caelestia-opacity,caelestia-pip"));
    QCOMPARE(val(file, "General", "count"), QStringLiteral("5"));
    QCOMPARE(val(file, "caelestia-opacity", "Description"), QStringLiteral("c2"));

    // A stock snapshot with no rules at all removes the list entirely.
    Fake g;
    writeFile(g.cfg("kwinrulesrc"), QString());
    QString emptyRef;
    QVERIFY(createBackup(Side::Stock, QStringLiteral("manual"), g.paths, &emptyRef).ok);
    writeFile(g.cfg("kwinrulesrc"), QStringLiteral("[General]\ncount=1\nrules=caelestia-pip\n\n[caelestia-pip]\nDescription=c3\n"));
    QVERIFY(restoreBackup(emptyRef, g.paths).ok);
    QCOMPARE(val(g.cfg("kwinrulesrc"), "General", "rules"), QStringLiteral("<missing>"));
    QVERIFY(!hasGroup(g.cfg("kwinrulesrc"), "caelestia-pip"));
}

void TestBackup::listAndRefs()
{
    Fake f;
    QCOMPARE(listBackups(f.paths).size(), 0);
    QString a, b, c;
    QVERIFY(createBackup(Side::Stock, QStringLiteral("manual"), f.paths, &a).ok);
    QVERIFY(createBackup(Side::Stock, QStringLiteral("auto-leave"), f.paths, &b).ok);   // same second: id gets a suffix
    QVERIFY(createBackup(Side::Caelestia, QStringLiteral("manual"), f.paths, &c).ok);
    QVERIFY(a != b);

    QCOMPARE(listBackups(Side::Stock, f.paths).size(), 2);
    QCOMPARE(listBackups(Side::Caelestia, f.paths).size(), 1);
    QCOMPARE(listBackups(f.paths).size(), 3);
    const QList<BackupInfo> stock = listBackups(Side::Stock, f.paths);
    QVERIFY(stock.at(0).id > stock.at(1).id);                    // newest first
    QCOMPARE(stock.at(0).ref(), b);
    QCOMPARE(stock.at(0).trigger, QStringLiteral("auto-leave"));
    QVERIFY(stock.at(0).created.isValid());

    // A half-written backup is ignored.
    QDir().mkpath(f.paths.dataRoot + QStringLiteral("/stock/20990101_000000.partial"));
    QDir().mkpath(f.paths.dataRoot + QStringLiteral("/stock/20990101_000001"));
    QCOMPARE(listBackups(Side::Stock, f.paths).size(), 2);
}

void TestBackup::badRefs()
{
    Fake f;
    QVERIFY(!restoreBackup(QStringLiteral("nonsense"), f.paths).ok);
    QVERIFY(!restoreBackup(QStringLiteral("stock/../../etc"), f.paths).ok);
    QVERIFY(!restoreBackup(QStringLiteral("other/20260101_000000"), f.paths).ok);
    QVERIFY(!restoreBackup(QStringLiteral("stock/20260101_000000"), f.paths).ok);   // well-formed but missing
}

QTEST_GUILESS_MAIN(TestBackup)
#include "test_backup.moc"
