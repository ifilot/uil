#include "app_controller.hpp"
#include "ui/audience_window.hpp"
#include "ui/presenter_window.hpp"
#include "util/performance_log.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QSysInfo>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
/** @brief Returns elapsed time since Windows created the current process. */
qint64 windows_process_age_ms() {
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    FILETIME current_time;
    if (!GetProcessTimes(
            GetCurrentProcess(),
            &creation_time,
            &exit_time,
            &kernel_time,
            &user_time)) {
        return 0;
    }

    GetSystemTimeAsFileTime(&current_time);
    ULARGE_INTEGER creation_value;
    creation_value.LowPart = creation_time.dwLowDateTime;
    creation_value.HighPart = creation_time.dwHighDateTime;
    ULARGE_INTEGER current_value;
    current_value.LowPart = current_time.dwLowDateTime;
    current_value.HighPart = current_time.dwHighDateTime;
    return current_value.QuadPart >= creation_value.QuadPart
        ? qint64((current_value.QuadPart - creation_value.QuadPart) / 10'000ULL)
        : 0;
}
#else
/** @brief Returns zero when process-creation timing is unavailable. */
qint64 windows_process_age_ms() {
    return 0;
}
#endif

/** @brief Loads and applies the bundled Qt application stylesheet. */
void apply_application_theme(QApplication& app) {
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

/** @brief Initializes Qt and runs the UIL desktop application. */
int main(int argc, char* argv[]) {
    QElapsedTimer process_start_timer;
    process_start_timer.start();
    const qint64 process_to_main_ms = windows_process_age_ms();
    QApplication app(argc, argv);
    const qint64 application_construction_ms = process_start_timer.elapsed();
    QApplication::setApplicationName(QStringLiteral("uil"));
    QApplication::setOrganizationName(QStringLiteral(UIL_APP_AUTHOR));
    QApplication::setOrganizationDomain(QStringLiteral("ivofilot.nl"));
    QApplication::setApplicationVersion(QStringLiteral(UIL_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/uil.svg")));
    performance_log::initialize();
    performance_log::set_process_start_offset_ms(
        process_to_main_ms + process_start_timer.elapsed());
    performance_log::record_duration(
        QStringLiteral("startup.process_to_main"),
        process_to_main_ms);
    performance_log::record_duration(
        QStringLiteral("startup.qt_application"),
        application_construction_ms);
    performance_log::record_event(QStringLiteral("session.environment"), {
        {QStringLiteral("build_configuration"), QStringLiteral(UIL_BUILD_CONFIG)},
        {QStringLiteral("compiler"), QStringLiteral(UIL_COMPILER_ID " " UIL_COMPILER_VERSION)},
        {QStringLiteral("cpu_architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("ideal_thread_count"), QThread::idealThreadCount()},
        {QStringLiteral("kernel"), QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion()},
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
        {QStringLiteral("qt_runtime_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("thread_pool_max_count"), QThreadPool::globalInstance()->maxThreadCount()}
    });

    QElapsedTimer startup_initialize_timer;
    startup_initialize_timer.start();
    qint64 previous_startup_checkpoint_ms = 0;
    const auto record_startup_checkpoint =
        [&startup_initialize_timer, &previous_startup_checkpoint_ms](const QString& name) {
            const qint64 total_ms = startup_initialize_timer.elapsed();
            performance_log::record_duration(
                QStringLiteral("startup.initialize.") + name,
                total_ms - previous_startup_checkpoint_ms,
                {{QStringLiteral("total_ms"), total_ms}});
            previous_startup_checkpoint_ms = total_ms;
        };

    apply_application_theme(app);
    record_startup_checkpoint(QStringLiteral("apply_theme"));

    AppController controller;
    record_startup_checkpoint(QStringLiteral("construct_controller"));
    AudienceWindow audience_window;
    record_startup_checkpoint(QStringLiteral("construct_audience_window"));
    PresenterWindow presenterWindow(&controller);
    record_startup_checkpoint(QStringLiteral("construct_presenter_window"));

    controller.set_audience_window(&audience_window);
    QObject::connect(&app, &QGuiApplication::screenAdded, &controller, &AppController::refresh_screens);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &controller, &AppController::refresh_screens);
    QObject::connect(&audience_window, &AudienceWindow::next_requested, &controller, &AppController::next_page);
    QObject::connect(&audience_window, &AudienceWindow::previous_requested, &controller, &AppController::previous_page);
    QObject::connect(&audience_window, &AudienceWindow::page_requested, &controller, &AppController::go_to_page);
    QObject::connect(&audience_window, &AudienceWindow::deck_overview_renders_requested, &controller, &AppController::request_deck_overview_renders);
    QObject::connect(&audience_window, &AudienceWindow::first_requested, &controller, [&controller] {
        controller.go_to_page(0);
    });
    QObject::connect(&audience_window, &AudienceWindow::last_requested, &controller, [&controller] {
        controller.go_to_page(controller.page_count() - 1);
    });
    QObject::connect(&audience_window, &AudienceWindow::play_pause_requested, &controller, &AppController::toggle_media_playback);
    QObject::connect(&controller, &AppController::page_changed, &audience_window,
        [&audience_window](int page_index, int page_count) {
            audience_window.set_document_overview(page_count, page_index);
        });
    QObject::connect(&controller, &AppController::deck_slide_image_changed, &audience_window, &AudienceWindow::set_deck_overview_slide_image);
    record_startup_checkpoint(QStringLiteral("connect_windows"));

    audience_window.set_audience_screen(controller.selected_audience_screen());
    presenterWindow.show();
    record_startup_checkpoint(QStringLiteral("show_presenter"));
    performance_log::record_duration(
        QStringLiteral("startup.initialize"),
        startup_initialize_timer.elapsed(),
        {{QStringLiteral("outcome"), QStringLiteral("ready_for_event_loop")}});

    QTimer::singleShot(0, &app, [&process_start_timer] {
        performance_log::record_duration(
            QStringLiteral("startup.main_to_first_event_loop_turn"),
            process_start_timer.elapsed());
        performance_log::record_duration(
            QStringLiteral("startup.process_to_first_event_loop_turn"),
            performance_log::process_elapsed_ms());
    });

    return app.exec();
}
