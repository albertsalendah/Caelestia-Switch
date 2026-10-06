#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QtTest>

#include "mainwindow.h"

using namespace csgui;

namespace {

cs::BackupInfo backup(cs::Side side, const QString &id)
{
    cs::BackupInfo b;
    b.side = side;
    b.id = id;
    b.created = QDateTime::fromString(QStringLiteral("2026-10-06T11:43:38"), Qt::ISODate);
    b.trigger = QStringLiteral("auto-leave");
    b.caelestiaVersion = QStringLiteral("v2.5.0");
    b.plasmaVersion = QStringLiteral("6.7.5");
    return b;
}

cs::ScreenModel switchModel(cs::Direction dir)
{
    cs::ScreenModel m;
    m.kind = cs::ScreenModel::Kind::Switch;
    m.direction = dir;
    const cs::Side side = dir == cs::Direction::ToStock ? cs::Side::Stock : cs::Side::Caelestia;
    m.backups = {backup(side, QStringLiteral("20261006_114338")), backup(side, QStringLiteral("20261005_090228"))};
    m.canSwitch = true;
    m.readings.provider = dir == cs::Direction::ToStock ? cs::Provider::Caelestia : cs::Provider::Plasma;
    m.readings.install.source = QStringLiteral("ladybug-me");
    m.readings.install.version = QStringLiteral("v2.5.0");
    return m;
}

} // namespace

class TestGui : public QObject
{
    Q_OBJECT

private slots:
    void startsOnTheLoadingPage();
    void blockedPage();
    void needBackupPage();
    void switchPageToStock();
    void switchPageToCaelestiaWithMask();
    void switchButtonIsInactiveInBatch2a();
    void bannerAndSeen();
    void closingWithoutDismissMarksSeenOnce();
    void noBannerNoSeenCallback();
};

void TestGui::startsOnTheLoadingPage()
{
    MainWindow w{cs::ResultView()};
    QCOMPARE(w.page(), MainWindow::Page::Loading);
    QCOMPARE(w.windowTitle(), QStringLiteral("Caelestia Switch"));
    w.showModel(switchModel(cs::Direction::ToStock), QStringLiteral("x"));
    w.showLoading();                                   // a reload goes back to the loading page
    QCOMPARE(w.page(), MainWindow::Page::Loading);
}

void TestGui::blockedPage()
{
    MainWindow w{cs::ResultView()};
    cs::ScreenModel m;
    m.kind = cs::ScreenModel::Kind::Blocked;
    m.message = QStringLiteral("Switching is not available right now: neither shell is running.");
    w.showModel(m, QStringLiteral("Consistency : INCONSISTENT\n"));
    QCOMPARE(w.page(), MainWindow::Page::Blocked);
    QVERIFY(w.blockedText().contains(QStringLiteral("neither shell is running")));
    QVERIFY(w.detailsText().contains(QStringLiteral("INCONSISTENT")));
}

void TestGui::needBackupPage()
{
    int pressed = 0;
    MainWindow w{cs::ResultView()};
    w.onBackup = [&pressed]() { ++pressed; };
    w.show();

    cs::ScreenModel m;
    m.kind = cs::ScreenModel::Kind::NeedBackup;
    m.canBackup = true;
    m.message = QStringLiteral("There is no Caelestia-side backup yet.");
    w.showModel(m, QString());
    QCOMPARE(w.page(), MainWindow::Page::NeedBackup);
    QVERIFY(w.backupButton()->isVisibleTo(&w));
    w.backupButton()->click();
    QCOMPARE(pressed, 1);
    w.setBackupStatus(QStringLiteral("Backup failed: x"));
    QCOMPARE(w.backupStatus(), QStringLiteral("Backup failed: x"));

    // In stock mode the button is not offered at all: the explanation only.
    m.canBackup = false;
    m.message = QStringLiteral("it can only be taken while Caelestia is the running shell");
    w.showModel(m, QString());
    QVERIFY(!w.backupButton()->isVisibleTo(&w));
    QVERIFY(w.needBackupText().contains(QStringLiteral("running shell")));
    QVERIFY(w.backupStatus().isEmpty());                // the old status does not linger
}

void TestGui::switchPageToStock()
{
    MainWindow w{cs::ResultView()};
    w.show();
    cs::ScreenModel m = switchModel(cs::Direction::ToStock);
    w.showModel(m, QStringLiteral("details"));
    QCOMPARE(w.page(), MainWindow::Page::Switch);
    QCOMPARE(w.directionText(), QStringLiteral("Switch to stock Plasma"));
    QVERIFY(w.infoText().contains(QStringLiteral("Current mode: Caelestia")));
    QVERIFY(w.infoText().contains(QStringLiteral("ladybug-me, v2.5.0")));
    QCOMPARE(w.backupCombo()->count(), 2);
    QCOMPARE(w.backupCombo()->currentData().toString(), QStringLiteral("stock/20261006_114338"));   // the newest is preselected
    QVERIFY(!w.maskCheck()->isVisibleTo(&w));            // the checkbox only applies when switching to Caelestia
    QCOMPARE(w.detailsText(), QStringLiteral("details"));

    // No backup to restore: the combo is empty and disabled, and the note says why.
    m.backups.clear();
    m.canSwitch = false;
    m.message = QStringLiteral("There is no stock-side backup");
    w.showModel(m, QString());
    QCOMPARE(w.backupCombo()->count(), 0);
    QVERIFY(!w.backupCombo()->isEnabled());
    QVERIFY(w.noteText().contains(QStringLiteral("no stock-side backup")));
}

void TestGui::switchPageToCaelestiaWithMask()
{
    MainWindow w{cs::ResultView()};
    w.show();
    cs::ScreenModel m = switchModel(cs::Direction::ToCaelestia);
    m.maskShown = true;
    m.maskWeak = {QStringLiteral("WantedBy=plasma-core.target")};
    w.showModel(m, QString());
    QCOMPARE(w.directionText(), QStringLiteral("Switch to Caelestia"));
    QVERIFY(w.maskCheck()->isVisibleTo(&w));
    QVERIFY(w.maskCheck()->isEnabled());
    QVERIFY(!w.maskCheck()->isChecked());
    QVERIFY(w.maskNote().contains(QStringLiteral("plasma-core.target")));   // the weak-dependent warning (D1)
    QVERIFY(w.maskNote().contains(QStringLiteral("allowed")));

    // A strong dependent: greyed out, with the blocking units named.
    m.maskAllowed = false;
    m.maskBlockers = {QStringLiteral("RequiredBy=something.service")};
    w.showModel(m, QString());
    QVERIFY(!w.maskCheck()->isEnabled());
    QVERIFY(w.maskNote().contains(QStringLiteral("RequiredBy=something.service")));
}

void TestGui::switchButtonIsInactiveInBatch2a()
{
    MainWindow w{cs::ResultView()};
    w.showModel(switchModel(cs::Direction::ToStock), QString());
    QVERIFY(!w.switchButton()->isEnabled());             // batch 2b turns it on, together with the warning dialog
}

void TestGui::bannerAndSeen()
{
    cs::ResultView v;
    v.present = true;
    v.kind = QStringLiteral("done");
    v.title = QStringLiteral("Switch complete");
    v.text = QStringLiteral("You are now in Caelestia mode.");
    int seen = 0;
    MainWindow w(v);
    w.onSeen = [&seen]() { ++seen; };
    w.show();
    QVERIFY(w.bannerShown());
    QCOMPARE(w.bannerTitle(), QStringLiteral("Switch complete"));
    QCOMPARE(w.bannerText(), QStringLiteral("You are now in Caelestia mode."));
    w.dismissButton()->click();
    QVERIFY(!w.bannerShown());
    QCOMPARE(seen, 1);
    w.close();                                           // closing afterwards must not call back again
    QCOMPARE(seen, 1);
}

void TestGui::closingWithoutDismissMarksSeenOnce()
{
    cs::ResultView v;
    v.present = true;
    v.title = QStringLiteral("Switch complete");
    v.text = QStringLiteral("You are now in stock Plasma mode.");
    int seen = 0;
    MainWindow w(v);
    w.onSeen = [&seen]() { ++seen; };
    w.show();
    w.close();
    QCOMPARE(seen, 1);
    w.close();
    QCOMPARE(seen, 1);
}

void TestGui::noBannerNoSeenCallback()
{
    int seen = 0;
    MainWindow w{cs::ResultView()};
    w.onSeen = [&seen]() { ++seen; };
    w.show();
    QVERIFY(!w.bannerShown());
    w.close();
    QCOMPARE(seen, 0);                                   // nothing was pending: the state file is left alone
}

QTEST_MAIN(TestGui)
#include "test_gui.moc"
