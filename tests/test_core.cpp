#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "consistency.h"
#include "install.h"
#include "readings.h"

using namespace cs;

namespace {

const QString kSha = QStringLiteral("be4188f3c6d2f1f5212981818f692ec53e87c1b3");
const QString kOtherSha = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(content.toUtf8());
}

UnitState unit(const QString &load, const QString &active, const QString &sub, const QString &fileState)
{
    UnitState u;
    u.queried = true;
    u.loadState = load;
    u.activeState = active;
    u.subState = sub;
    u.unitFileState = fileState;
    return u;
}

// Condition A: Caelestia active, plasmashell headless and not masked.
Readings conditionA()
{
    Readings r;
    r.systemdReachable = true;
    r.plasmashellUnit = unit(QStringLiteral("loaded"), QStringLiteral("active"), QStringLiteral("running"), QStringLiteral("static"));
    r.caelestiaUnit = unit(QStringLiteral("loaded"), QStringLiteral("active"), QStringLiteral("running"), QStringLiteral("enabled"));
    r.plasmashellRunning = true;
    r.quickshellRunning = true;
    r.shellPackage = QStringLiteral("caelestia.desktop");
    return r;
}

Readings finish(Readings r)
{
    r.provider = computeProvider(r);
    r.inconsistentReasons = findInconsistencies(r);
    return r;
}

// Builds a fake home with a Caelestia install (and optionally a checkout).
struct FakeHome {
    QTemporaryDir dir;
    QString home() const { return dir.path(); }
    QString appCfg() const { return dir.path() + QStringLiteral("/appcfg"); }

    void install(bool currentVersion = true)
    {
        writeFile(home() + "/.config/quickshell/caelestia/shell.qml", QStringLiteral("// shell\n"));
        writeFile(home() + "/.config/systemd/user/caelestia-shell.service", QStringLiteral("[Unit]\n"));
        writeFile(home() + "/.config/quickshell/caelestia/.current_commit", kSha + QLatin1Char('\n'));
        if (currentVersion) {
            writeFile(home() + "/.config/quickshell/caelestia/.current_version", QStringLiteral("VERSION=v2.5.0\n"));
        }
    }

    void checkout(const QString &origin, const QString &sha, bool packed = false)
    {
        const QString c = home() + "/caelestia-kde";
        writeFile(c + "/.git/HEAD", QStringLiteral("ref: refs/heads/main\n"));
        if (packed) {
            writeFile(c + "/.git/packed-refs", QStringLiteral("# pack-refs\n%1 refs/heads/main\n").arg(sha));
        } else {
            writeFile(c + "/.git/refs/heads/main", sha + QLatin1Char('\n'));
        }
        writeFile(c + "/.git/config",
                  QStringLiteral("[core]\n\tbare = false\n[remote \"origin\"]\n\turl = %1\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n").arg(origin));
        writeFile(c + "/.github/version.env", QStringLiteral("VERSION=v2.5.0\n"));
    }
};

} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void versionEnv();
    void originUrl();
    void normalizeAndSource_data();
    void normalizeAndSource();
    void commits();
    void stateMode();
    void consistencyMatrix();
    void installFromCheckout();
    void installPackedRefs();
    void checkoutMovedAway();
    void installForkCheckout();
    void versionFromCheckoutOnly();
    void markerMatchingAndStale();
    void notInstalled();
};

void TestCore::versionEnv()
{
    QCOMPARE(parseVersionEnv(QStringLiteral("VERSION=v2.5.0\n")), QStringLiteral("v2.5.0"));
    QCOMPARE(parseVersionEnv(QStringLiteral("# c\n VERSION = v2.5.1 # x\n")), QStringLiteral("v2.5.1"));
    QCOMPARE(parseVersionEnv(QStringLiteral("VERSION=\"v3.0.0\"\n")), QStringLiteral("v3.0.0"));
    QCOMPARE(parseVersionEnv(QStringLiteral("FOO=1\n")), QString());
    QCOMPARE(parseVersionEnv(QString()), QString());
}

void TestCore::originUrl()
{
    const QString cfg = QStringLiteral("[core]\n\turl = wrong\n[remote \"upstream\"]\n\turl = https://example.com/x.git\n"
                                       "[remote \"origin\"]\n\tfetch = +refs/*\n\turl = https://github.com/ladybug-me/caelestia-kde.git\n");
    QCOMPARE(parseOriginUrl(cfg), QStringLiteral("https://github.com/ladybug-me/caelestia-kde.git"));
    QCOMPARE(parseOriginUrl(QStringLiteral("[core]\n\tbare = false\n")), QString());
}

void TestCore::normalizeAndSource_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("normalized");
    QTest::addColumn<QString>("source");

    QTest::newRow("https .git") << "https://github.com/ladybug-me/caelestia-kde.git" << "github.com/ladybug-me/caelestia-kde" << "ladybug-me";
    QTest::newRow("https slash") << "https://github.com/ladybug-me/caelestia-kde/" << "github.com/ladybug-me/caelestia-kde" << "ladybug-me";
    QTest::newRow("scp style") << "git@github.com:ladybug-me/caelestia-kde.git" << "github.com/ladybug-me/caelestia-kde" << "ladybug-me";
    QTest::newRow("ssh scheme") << "ssh://git@github.com/AlbertSalendah/Caelestia-KDE" << "github.com/albertsalendah/caelestia-kde" << "fork";
    QTest::newRow("fork https") << "https://github.com/albertsalendah/caelestia-kde.git" << "github.com/albertsalendah/caelestia-kde" << "fork";
    QTest::newRow("other") << "https://github.com/someone/caelestia-kde.git" << "github.com/someone/caelestia-kde" << "unknown";
    QTest::newRow("empty") << "" << "" << "unknown";
}

void TestCore::normalizeAndSource()
{
    QFETCH(QString, url);
    QFETCH(QString, normalized);
    QFETCH(QString, source);
    QCOMPARE(normalizeRepoUrl(url), normalized);
    QCOMPARE(sourceFromUrl(url), source);
}

void TestCore::commits()
{
    QVERIFY(sameCommit(kSha, kSha));
    QVERIFY(sameCommit(kSha, QStringLiteral("be4188f")));
    QVERIFY(sameCommit(kSha + QLatin1Char('\n'), kSha.toUpper()));
    QVERIFY(!sameCommit(kSha, QStringLiteral("be4188")));      // prefix too short
    QVERIFY(!sameCommit(kSha, kOtherSha));
    QVERIFY(!sameCommit(kSha, QStringLiteral("previous-revision")));
    QVERIFY(!sameCommit(QString(), kSha));
}

void TestCore::stateMode()
{
    QCOMPARE(parseStateMode(QStringLiteral("mode=transitioning\nstep=3\n")), QStringLiteral("transitioning"));
    QCOMPARE(parseStateMode(QStringLiteral("# c\n\nPending-Logout step=9\n")), QStringLiteral("pending-logout"));
    QCOMPARE(parseStateMode(QStringLiteral("caelestia\n")), QStringLiteral("caelestia"));
    QCOMPARE(parseStateMode(QString()), QString());
}

void TestCore::consistencyMatrix()
{
    // Current install: Caelestia active, plasmashell headless, not masked = valid.
    Readings a = finish(conditionA());
    QCOMPARE(a.provider, Provider::Caelestia);
    QVERIFY2(!a.inconsistent(), qPrintable(a.inconsistentReasons.join(';')));

    // plasmashell stopped and masked = valid.
    Readings b = conditionA();
    b.plasmashellRunning = false;
    b.plasmashellUnit = unit(QStringLiteral("masked"), QStringLiteral("inactive"), QStringLiteral("dead"), QStringLiteral("masked"));
    b = finish(b);
    QCOMPARE(b.provider, Provider::Caelestia);
    QVERIFY(!b.inconsistent());

    // Masked but still running = Inconsistent.
    Readings c = conditionA();
    c.plasmashellUnit = unit(QStringLiteral("masked"), QStringLiteral("active"), QStringLiteral("running"), QStringLiteral("masked"));
    c = finish(c);
    QVERIFY(c.inconsistent());

    // Caelestia plus stock panels = Inconsistent, provider Both.
    Readings d = conditionA();
    d.shellPackage = QStringLiteral("org.kde.plasma.desktop");
    d = finish(d);
    QCOMPARE(d.provider, Provider::Both);
    QVERIFY(d.inconsistent());

    // Stock Plasma mode: Caelestia off, plasmashell with the stock package = valid.
    Readings e = conditionA();
    e.caelestiaUnit = unit(QStringLiteral("loaded"), QStringLiteral("inactive"), QStringLiteral("dead"), QStringLiteral("disabled"));
    e.quickshellRunning = false;
    e.shellPackage = QString();
    e = finish(e);
    QCOMPARE(e.provider, Provider::Plasma);
    QVERIFY(!e.inconsistent());

    // Caelestia disabled and stopped, plasmashell still headless: no panels, but valid.
    Readings f = conditionA();
    f.caelestiaUnit = unit(QStringLiteral("loaded"), QStringLiteral("inactive"), QStringLiteral("dead"), QStringLiteral("disabled"));
    f.quickshellRunning = false;
    f = finish(f);
    QCOMPARE(f.provider, Provider::None);
    QVERIFY(!f.inconsistent());

    // Neither shell running = Inconsistent.
    Readings g = f;
    g.plasmashellRunning = false;
    g = finish(g);
    QVERIFY(g.inconsistent());

    // State file says a switch is in progress = Inconsistent.
    Readings h = conditionA();
    h.switchStateMode = QStringLiteral("transitioning");
    h = finish(h);
    QVERIFY(h.inconsistent());

    // systemd unreachable = reported, not guessed.
    Readings i;
    i.systemdError = QStringLiteral("no bus");
    i = finish(i);
    QVERIFY(i.inconsistent());
}

void TestCore::installFromCheckout()
{
    FakeHome h;
    h.install();
    h.checkout(QStringLiteral("https://github.com/ladybug-me/caelestia-kde.git"), kSha);
    const InstallInfo i = detectInstall(h.home(), h.appCfg(), QString());
    QVERIFY(i.installed);
    QCOMPARE(i.commit, kSha);
    QCOMPARE(i.source, QStringLiteral("ladybug-me"));
    QCOMPARE(i.sourceFrom, QStringLiteral("checkout"));
    QCOMPARE(i.version, QStringLiteral("v2.5.0"));
    QCOMPARE(i.versionFrom, QStringLiteral(".current_version"));
    QCOMPARE(i.checkout, h.home() + "/caelestia-kde");
}

void TestCore::installPackedRefs()
{
    FakeHome h;
    h.install();
    h.checkout(QStringLiteral("git@github.com:ladybug-me/caelestia-kde.git"), kSha, /*packed=*/true);
    const InstallInfo i = detectInstall(h.home(), h.appCfg(), QString());
    QCOMPARE(i.source, QStringLiteral("ladybug-me"));
}

void TestCore::checkoutMovedAway()
{
    // Roadmap A1: moving the checkout away must read "unknown", not a wrong answer.
    FakeHome h;
    h.install();
    const InstallInfo i = detectInstall(h.home(), h.appCfg(), QString());
    QVERIFY(i.installed);
    QCOMPARE(i.source, QStringLiteral("unknown"));
    QCOMPARE(i.sourceFrom, QStringLiteral("none"));
    // The version is still known because the installer records it next to .current_commit.
    QCOMPARE(i.version, QStringLiteral("v2.5.0"));

    // A checkout whose HEAD differs from .current_commit is not trusted.
    FakeHome h2;
    h2.install();
    h2.checkout(QStringLiteral("https://github.com/ladybug-me/caelestia-kde.git"), kOtherSha);
    const InstallInfo j = detectInstall(h2.home(), h2.appCfg(), QString());
    QCOMPARE(j.source, QStringLiteral("unknown"));
    QVERIFY(j.checkout.isEmpty());
}

void TestCore::installForkCheckout()
{
    FakeHome h;
    h.install();
    h.checkout(QStringLiteral("https://github.com/albertsalendah/caelestia-kde.git"), kSha);
    QCOMPARE(detectInstall(h.home(), h.appCfg(), QString()).source, QStringLiteral("fork"));

    // CAELESTIA_DIR is tried too.
    FakeHome h2;
    h2.install();
    h2.checkout(QStringLiteral("https://github.com/albertsalendah/caelestia-kde.git"), kSha);
    QVERIFY(QDir(h2.home()).rename(QStringLiteral("caelestia-kde"), QStringLiteral("elsewhere")));
    QCOMPARE(detectInstall(h2.home(), h2.appCfg(), h2.home() + "/elsewhere").source, QStringLiteral("fork"));
    QCOMPARE(detectInstall(h2.home(), h2.appCfg(), QString()).source, QStringLiteral("unknown"));
}

void TestCore::versionFromCheckoutOnly()
{
    FakeHome h;
    h.install(/*currentVersion=*/false);
    h.checkout(QStringLiteral("https://github.com/ladybug-me/caelestia-kde.git"), kSha);
    const InstallInfo i = detectInstall(h.home(), h.appCfg(), QString());
    QCOMPARE(i.version, QStringLiteral("v2.5.0"));
    QCOMPARE(i.versionFrom, QStringLiteral("checkout"));

    FakeHome h2;
    h2.install(false);
    QCOMPARE(detectInstall(h2.home(), h2.appCfg(), QString()).version, QStringLiteral("unknown"));
}

void TestCore::markerMatchingAndStale()
{
    FakeHome h;
    h.install();
    writeFile(h.appCfg() + "/install.json",
              QStringLiteral(R"({"source":"fork","version":"v9.9.9","commit":"%1","checkout":"/x"})").arg(kSha));
    InstallInfo i = detectInstall(h.home(), h.appCfg(), QString());
    QCOMPARE(i.source, QStringLiteral("fork"));
    QCOMPARE(i.sourceFrom, QStringLiteral("marker"));
    QCOMPARE(i.version, QStringLiteral("v9.9.9"));
    QCOMPARE(i.versionFrom, QStringLiteral("marker"));

    // Marker written for another commit (install changed outside the app) is ignored.
    writeFile(h.appCfg() + "/install.json",
              QStringLiteral(R"({"source":"fork","version":"v9.9.9","commit":"%1"})").arg(kOtherSha));
    i = detectInstall(h.home(), h.appCfg(), QString());
    QCOMPARE(i.source, QStringLiteral("unknown"));
    QCOMPARE(i.version, QStringLiteral("v2.5.0"));
}

void TestCore::notInstalled()
{
    FakeHome h;
    QVERIFY(!detectInstall(h.home(), h.appCfg(), QString()).installed);

    // shell.qml without the unit file is not a complete install.
    writeFile(h.home() + "/.config/quickshell/caelestia/shell.qml", QStringLiteral("//\n"));
    QVERIFY(!detectInstall(h.home(), h.appCfg(), QString()).installed);
}

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
