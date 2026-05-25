#include "AppController.hpp"
#include "ui/AudienceWindow.hpp"
#include "ui/PresenterWindow.hpp"

#include <QApplication>
#include <QGuiApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("uil"));
    QApplication::setOrganizationName(QStringLiteral("uil"));

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
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        audienceWindow.show();
    }
    presenterWindow.show();

    return app.exec();
}
