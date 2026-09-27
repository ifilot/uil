#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTimer>

#include "ui/molecular_symmetry_widget.hpp"
#include "ui/molecule_widget.hpp"

/** @brief Renders embedded symmetry definitions to PNG posters with a live OpenGL context. */
int main(int argc, char** argv) {
  QApplication application(argc, argv);
  const auto arguments = application.arguments();
  if (arguments.size() < 3) {
    qCritical("usage: molecular_symmetry_preview_generator OUTPUT_DIR INPUT.uilsym [...]");
    return 2;
  }
  QDir output(arguments.at(1));
  if (!QDir().mkpath(output.absolutePath())) return 1;
  MolecularSymmetryWidget widget;
  widget.resize(1320, 600);
  for (int index = 2; index < arguments.size(); ++index) {
    QFile input(arguments.at(index));
    MolecularSymmetryDefinition definition;
    QString error;
    if (!input.open(QIODevice::ReadOnly) ||
        !parse_molecular_symmetry(input.readAll(), &definition, &error)) {
      qCritical().noquote() << input.fileName() << error;
      return 1;
    }
    widget.set_definition(definition);
    widget.show();
    QEventLoop frame_loop;
    QTimer::singleShot(500, &frame_loop, &QEventLoop::quit);
    frame_loop.exec();
    auto* molecule = dynamic_cast<MoleculeWidget*>(
        widget.findChild<QWidget*>(QStringLiteral("symmetryMoleculeOpenGLWidget")));
    if (!molecule || !molecule->isValid()) {
      qCritical("No OpenGL context available for the symmetry poster");
      return 1;
    }
    const QString path = output.filePath(QFileInfo(input).completeBaseName() + ".png");
    const QImage captured = widget.capture_frame();
    QImage poster(captured.size(), QImage::Format_RGB32);
    poster.fill(Qt::white);
    {
      QPainter painter(&poster);
      painter.drawImage(0, 0, captured);
      const QRect caption(0, poster.height() - 28, poster.width(), 28);
      painter.fillRect(caption, QColor("#edf5f5"));
      painter.setPen(QColor("#277d83"));
      painter.drawText(
          caption, Qt::AlignCenter,
          QStringLiteral(
              "Preview image · Start the slideshow and use the live audience slide to interact"));
    }
    if (!poster.save(path)) return 1;
    qInfo().noquote() << "Wrote" << path;
  }
  return 0;
}
