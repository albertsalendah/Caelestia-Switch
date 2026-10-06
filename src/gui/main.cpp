#include <QApplication>
#include <QIcon>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <functional>

#include "backup.h"
#include "fsutil.h"
#include "mainwindow.h"
#include "readings.h"
#include "screens.h"
#include "state.h"
#include "switch.h"
#include "switchops.h"
#include "version.h"

// The app's window: the pending result of the last switch as a banner (also when started by hand), the loading
// page while the readings are read, then Screen A (backup needed), Screen B (switch) or a "blocked" message.
// The post-login service starts this after every switch (architecture D22, D23).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("caelestia-switch"));
    QApplication::setApplicationVersion(cs::version());
    QGuiApplication::setDesktopFileName(QStringLiteral("caelestia-switch"));   // matches data/caelestia-switch.desktop: right icon in the title bar
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop")));

    const QString stateFile = cs::appConfigDir() + QStringLiteral("/state");
    cs::ResultView pending;
    cs::SwitchState st;
    if (cs::readState(stateFile, &st)) {
        pending = cs::describeResult(st);
    }

    csgui::MainWindow window(pending);
    window.onSeen = [stateFile]() { cs::markSeen(stateFile); };

    // The readings can take seconds when the bus is slow, so they are read on a worker thread and the loading
    // page keeps animating; the result is handed back to the window through the event loop.
    QThread *worker = nullptr;
    const auto load = [&window, &worker]() {
        window.showLoading();
        if (worker) {
            worker->wait();
            delete worker;
        }
        worker = QThread::create([&window]() {
            cs::RealOps ops;
            const cs::Readings readings = cs::gatherReadings();
            const cs::ScreenModel model = cs::buildScreenModel(readings, cs::BackupPaths::defaults(), &ops);
            const QString text = cs::toText(readings);
            QMetaObject::invokeMethod(&window, [&window, model, text]() { window.showModel(model, text); }, Qt::QueuedConnection);
        });
        worker->start();
    };

    window.onBackup = [&window, load]() {
        // Only while Caelestia runs: in stock mode the "Caelestia-side" backup would hold the stock config.
        if (cs::gatherReadings().provider != cs::Provider::Caelestia) {
            window.setBackupStatus(QStringLiteral("Not created: Caelestia is no longer the running shell."));
            return;
        }
        QString ref;
        const cs::OpResult res = cs::createBackup(cs::Side::Caelestia, QStringLiteral("manual"), cs::BackupPaths::defaults(), &ref);
        if (!res.ok) {
            window.setBackupStatus(QStringLiteral("Backup failed: %1").arg(res.error));
            return;
        }
        load();   // reads everything again: Screen B appears now
    };

    window.show();
    QTimer::singleShot(100, &window, load);   // let the window paint first

    const int rc = app.exec();
    if (worker) {
        worker->wait();
        delete worker;
    }
    return rc;
}
