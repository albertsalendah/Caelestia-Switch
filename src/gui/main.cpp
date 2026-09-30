#include <QApplication>
#include <QLabel>
#include <QMainWindow>

#include "version.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("caelestia-switch"));
    QApplication::setApplicationVersion(cs::version());

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Caelestia Switch"));
    auto *label = new QLabel(QStringLiteral("caelestia-switch %1\n(project skeleton)").arg(cs::version()));
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);
    window.resize(420, 240);
    window.show();

    return app.exec();
}
