#include "AppController.hpp"
#include "ui/AudienceWindow.hpp"
#include "ui/PresenterWindow.hpp"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

namespace {
void applyApplicationTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x1e, 0x1e, 0x1e));
    palette.setColor(QPalette::WindowText, QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
    palette.setColor(QPalette::AlternateBase, QColor(0x25, 0x25, 0x26));
    palette.setColor(QPalette::ToolTipBase, QColor(0x25, 0x25, 0x26));
    palette.setColor(QPalette::ToolTipText, QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::Text, QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::Button, QColor(0x3c, 0x3c, 0x3c));
    palette.setColor(QPalette::ButtonText, QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::BrightText, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Highlight, QColor(0x00, 0x8c, 0x8c));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::PlaceholderText, QColor(0x85, 0x85, 0x85));
    app.setPalette(palette);

    QFile styleFile(QStringLiteral(":/styles/vscode.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("uil"));
    QApplication::setOrganizationName(QStringLiteral("uil"));
    QApplication::setApplicationVersion(QStringLiteral(UIL_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/uil-white.svg")));
    applyApplicationTheme(app);

    AppController controller;
    AudienceWindow audienceWindow;
    PresenterWindow presenterWindow(&controller);

    controller.setAudienceWindow(&audienceWindow);
    QObject::connect(&app, &QGuiApplication::screenAdded, &controller, &AppController::refreshScreens);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &controller, &AppController::refreshScreens);
    QObject::connect(&audienceWindow, &AudienceWindow::nextRequested, &controller, &AppController::nextPage);
    QObject::connect(&audienceWindow, &AudienceWindow::previousRequested, &controller, &AppController::previousPage);
    QObject::connect(&audienceWindow, &AudienceWindow::firstRequested, &controller, [&controller] {
        controller.goToPage(0);
    });
    QObject::connect(&audienceWindow, &AudienceWindow::lastRequested, &controller, [&controller] {
        controller.goToPage(controller.pageCount() - 1);
    });
    QObject::connect(&audienceWindow, &AudienceWindow::playPauseRequested, &controller, &AppController::toggleMediaPlayback);

    audienceWindow.setAudienceScreen(controller.selectedAudienceScreen());
    presenterWindow.show();

    return app.exec();
}
