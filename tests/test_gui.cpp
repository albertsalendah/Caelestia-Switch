#include <QPushButton>
#include <QtTest>

#include "resultwindow.h"

using namespace csgui;

class TestGui : public QObject
{
    Q_OBJECT

private slots:
    void showsAResult();
    void showsTheStatusPageWithoutAResult();
    void closeCallsBackOnce();
};

void TestGui::showsAResult()
{
    cs::ResultView v;
    v.present = true;
    v.kind = QStringLiteral("done");
    v.title = QStringLiteral("Switch complete");
    v.text = QStringLiteral("You are now in Caelestia mode.");
    ResultWindow w(v);
    w.setStatusText(QStringLiteral("Bar/panel provider   : Caelestia\n"));
    QCOMPARE(w.shownTitle(), QStringLiteral("Switch complete"));
    QCOMPARE(w.shownText(), QStringLiteral("You are now in Caelestia mode."));
    QVERIFY(w.shownStatus().contains(QStringLiteral("Caelestia")));
    QCOMPARE(w.windowTitle(), QStringLiteral("Caelestia Switch"));
}

void TestGui::showsTheStatusPageWithoutAResult()
{
    ResultWindow w{cs::ResultView()};
    QCOMPARE(w.shownTitle(), QStringLiteral("Caelestia Switch"));
    QVERIFY(w.shownText().contains(QStringLiteral("No switch result")));
}

void TestGui::closeCallsBackOnce()
{
    int closed = 0;
    ResultWindow w{cs::ResultView()};
    w.onClose = [&closed]() { ++closed; };
    w.show();
    w.closeButton()->click();
    QVERIFY(!w.isVisible());
    QCOMPARE(closed, 1);
    w.close();   // closing again must not call back again
    QCOMPARE(closed, 1);
}

QTEST_MAIN(TestGui)
#include "test_gui.moc"
