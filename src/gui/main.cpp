#include <QApplication>
#include <QTimer>

#include "fsutil.h"
#include "readings.h"
#include "resultwindow.h"
#include "state.h"
#include "switch.h"
#include "version.h"

// Opens the result window: the pending result of the last switch if there is one (also when started by hand),
// otherwise just the current state. The post-login service starts this after every switch (architecture D22).
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("caelestia-switch"));
    QApplication::setApplicationVersion(cs::version());

    const QString stateFile = cs::appConfigDir() + QStringLiteral("/state");
    cs::ResultView view;
    cs::SwitchState st;
    if (cs::readState(stateFile, &st)) {
        view = cs::describeResult(st);
    }

    csgui::ResultWindow window(view);
    window.setStatusText(QStringLiteral("Reading the current state..."));
    window.onClose = [view, stateFile]() {
        if (view.present) {
            cs::markSeen(stateFile);   // the result has been shown; a failed or unfinished switch keeps showing until repaired
        }
    };
    window.show();

    // The readings can take a few seconds when the bus is slow: let the window paint first.
    QTimer::singleShot(100, &window, [&window]() { window.setStatusText(cs::toText(cs::gatherReadings())); });
    return app.exec();
}
