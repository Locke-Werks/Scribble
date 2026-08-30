#include <QApplication>

#include "MainWindow.hpp"
#include "ScribeEvent.hpp"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Locke Werks"));
    QCoreApplication::setApplicationName(QStringLiteral("ScribeEveryone"));

    // The pipeline emits events from a worker thread across a queued connection,
    // so the boxed variant type has to be known to Qt's metatype system first.
    scribe::gui::registerEventMetatype();

    scribe::gui::MainWindow window;
    window.show();
    return app.exec();
}
