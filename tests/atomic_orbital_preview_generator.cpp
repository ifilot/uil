#include "orbital/atomic_orbital.hpp"
#include "ui/atomic_orbital_widget.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <functional>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() < 3) {
        qCritical("usage: atomic_orbital_preview_generator OUTPUT_DIR INPUT.uilorb [...]");
        return 2;
    }

    QDir output_directory(arguments.at(1));
    if (!output_directory.exists() && !QDir().mkpath(output_directory.absolutePath())) {
        qCritical("could not create preview output directory");
        return 2;
    }

    AtomicOrbitalWidget widget;
    widget.resize(1200, 540);
    int input_index = 2;
    int result = 0;
    std::function<void()> render_next;
    render_next = [&]() {
        if (input_index >= arguments.size()) {
            application.exit(result);
            return;
        }

        const QString input_path = arguments.at(input_index++);
        QFile input(input_path);
        if (!input.open(QIODevice::ReadOnly)) {
            qCritical().noquote() << "could not read" << input_path;
            result = 1;
            QTimer::singleShot(0, &application, render_next);
            return;
        }
        AtomicOrbitalDefinition definition;
        QString error;
        if (!parse_atomic_orbital(input.readAll(), &definition, &error)) {
            qCritical().noquote() << input_path << ':' << error;
            result = 1;
            QTimer::singleShot(0, &application, render_next);
            return;
        }

        widget.set_definition(definition);
        widget.show();
        QTimer::singleShot(600, &application, [&, input_path]() {
            if (!widget.renderer_available()) {
                qCritical().noquote() << input_path << ':' << widget.renderer_error();
                result = 1;
            } else {
                const QImage preview = widget.capture_frame();
                const QString output_path = output_directory.filePath(
                    QFileInfo(input_path).completeBaseName() + QStringLiteral(".png"));
                if (preview.isNull() || !preview.save(output_path, "PNG")) {
                    qCritical().noquote() << "could not write" << output_path;
                    result = 1;
                } else {
                    qInfo().noquote() << "wrote" << output_path;
                }
            }
            QTimer::singleShot(0, &application, render_next);
        });
    };

    QTimer::singleShot(0, &application, render_next);
    return application.exec();
}
