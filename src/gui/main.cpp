#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <functional>

#include "backup.h"
#include "fsutil.h"
#include "mainwindow.h"
#include "readings.h"
#include "screens.h"
#include "settings.h"
#include "state.h"
#include "switch.h"
#include "switchops.h"
#include "version.h"

namespace {

// Same log as the CLI (~/.local/share/caelestia-switch/switch.log), without the echo to stderr.
void logLine(const QString &msg)
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/caelestia-switch/switch.log");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write((QDateTime::currentDateTime().toString(Qt::ISODate) + QLatin1Char(' ') + msg).toUtf8() + '\n');
    }
}

// "Nothing was started: <reason>." without a doubled period.
QString notStarted(QString why)
{
    why = why.trimmed();
    while (why.endsWith(QLatin1Char('.'))) {
        why.chop(1);
    }
    return QStringLiteral("Nothing was started: %1.").arg(why);
}

// The user-approved warning (cs::logoutWarningText). Cancel is the default button.
bool confirmLogout(QWidget *parent)
{
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Switch and log out"), cs::logoutWarningText(), QMessageBox::NoButton, parent);
    QPushButton *go = box.addButton(QStringLiteral("Log out and switch"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == go;
}

// Masking plasmashell when it is only weakly wanted (architecture D1): explain, offer "don't ask again".
bool confirmWeakMask(QWidget *parent, const QStringList &weak, bool *dontAskAgain)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("Disable plasmashell?"),
                    QStringLiteral("plasmashell will be stopped and masked. It is only weakly wanted by %1; "
                                   "that does not stop a masked plasmashell (checked in testing). Continue?")
                        .arg(weak.join(QStringLiteral(", "))),
                    QMessageBox::NoButton, parent);
    auto *again = new QCheckBox(QStringLiteral("Don't ask again"));
    box.setCheckBox(again);
    QPushButton *yes = box.addButton(QStringLiteral("Disable plasmashell"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    *dontAskAgain = again->isChecked();
    return box.clickedButton() == yes;
}

} // namespace

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

    // The Switch button (architecture D24): re-read the state (the window may be old), check the choice against it,
    // ask the questions, then hand the switch to the background service. The CLI binary starts it, not this one.
    window.onSwitch = [&window](const cs::SwitchChoice &shown) {
        cs::RealOps ops;
        const cs::Readings readings = cs::gatherReadings();
        const cs::ScreenModel fresh = cs::buildScreenModel(readings, cs::BackupPaths::defaults(), &ops);
        const cs::ChoiceCheck check = cs::checkChoice(fresh, shown);
        if (!check.ok) {
            window.showModel(fresh, cs::toText(readings));   // show what is true now, then say why nothing started
            window.setSwitchStatus(notStarted(check.problem));
            return;
        }
        if (check.needsWeakMaskConfirm) {
            cs::AppSettings settings = cs::readSettings(cs::appConfigDir());
            if (settings.confirmWeakMask) {
                bool dontAsk = false;
                if (!confirmWeakMask(&window, check.weak, &dontAsk)) {
                    return;
                }
                if (dontAsk) {
                    settings.confirmWeakMask = false;
                    QString err;
                    if (!cs::writeSettings(cs::appConfigDir(), settings, &err)) {
                        logLine(QStringLiteral("warning: %1").arg(err));
                    }
                }
            }
        }
        if (!confirmLogout(&window)) {
            return;
        }

        cs::SwitchRequest req;
        req.direction = shown.direction;
        req.targetRef = shown.targetRef;
        req.maskPlasmashell = shown.direction == cs::Direction::ToCaelestia && shown.mask;
        req.logout = true;
        const QString cli = cs::findCliBinary(QCoreApplication::applicationDirPath());
        if (cli.isEmpty()) {
            window.setSwitchStatus(notStarted(QStringLiteral("the caelestia-switch command-line tool was not found next to this app")));
            return;
        }
        cs::SwitchContext ctx;
        ctx.ops = &ops;
        ctx.paths = cs::BackupPaths::defaults();
        ctx.stateFile = cs::appConfigDir() + QStringLiteral("/state");
        ctx.helpersFile = cs::appConfigDir() + QStringLiteral("/helpers");
        ctx.log = logLine;
        const cs::OpResult res = cs::launchSwitch(req, ctx, cli);
        if (!res.ok) {
            window.setSwitchStatus(notStarted(res.error));
            return;
        }
        if (!res.warnings.isEmpty()) {   // already in the requested mode: nothing to do
            window.setSwitchStatus(res.warnings.join(QLatin1Char(' ')));
            return;
        }
        window.showStarted(QStringLiteral("The switch has started and runs in the background. You will be logged out in a few "
                                          "seconds. After you log in again this window opens with the result."));
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
