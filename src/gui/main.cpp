#include <QApplication>

#include "MainWindow.hpp"
#include "ScribeEvent.hpp"
#include "ThemeQt.hpp"
#include "gpu_runtime.hpp"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Locke Werks"));
    QCoreApplication::setApplicationName(QStringLiteral("Scribble"));

    // Before any widget exists: the bundled faces have to be registered before
    // the application font is set, and the base style before the palette.
    scribble::gui::theme::apply(app);

    // Put any already-downloaded GPU libraries on the process search path before
    // a pipeline or database is built, so onnxruntime can resolve cuDNN the first
    // time an ONNX engine is created. Safe to call when nothing is installed.
    scribble::activate_gpu_runtime();

    // The pipeline emits events from a worker thread across a queued connection,
    // so the boxed variant type has to be known to Qt's metatype system first.
    scribble::gui::registerEventMetatype();

    scribble::gui::MainWindow window;
    window.show();
    return app.exec();
}
