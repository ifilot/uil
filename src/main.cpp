#include "app_controller.hpp"
#include "launcher/launcher_protocol.h"
#include "ui/audience_window.hpp"
#include "ui/presenter_window.hpp"
#include "util/launcher_readiness.hpp"
#include "util/performance_log.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSplashScreen>
#include <QStyleFactory>
#include <QSysInfo>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
/** @brief Displays a non-dismissible loading surface during presenter initialization. */
class LoadingSplash final : public QSplashScreen {
public:
    /** @brief Constructs the loading surface from a lightweight raster pixmap. */
    explicit LoadingSplash(const QPixmap& pixmap)
        : QSplashScreen(
              pixmap,
              Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    }

protected:
    /** @brief Keeps the loading surface visible when it is clicked. */
    void mousePressEvent(QMouseEvent* event) override {
        event->ignore();
    }
};

/** @brief Creates the raster-only startup image without loading icon plugins. */
QPixmap create_loading_splash_pixmap() {
    constexpr int kSplashWidth = 420;
    constexpr int kSplashHeight = 150;
    QPixmap pixmap(kSplashWidth, kSplashHeight);
    pixmap.fill(QColor(0x18, 0x18, 0x18));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(QRect(0, 0, 6, kSplashHeight), QColor(0x00, 0x8c, 0x8c));
    painter.drawPixmap(QPoint(24, 20), QPixmap(QStringLiteral(":/icons/uil-splash.bmp")));
    painter.setPen(QColor(0xee, 0xee, 0xee));

    QFont title_font = QApplication::font();
    title_font.setPointSize(24);
    title_font.setBold(true);
    painter.setFont(title_font);
    painter.drawText(QRect(150, 24, kSplashWidth - 182, 48),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("uil"));

    QFont message_font = QApplication::font();
    message_font.setPointSize(10);
    painter.setFont(message_font);
    painter.setPen(QColor(0xb8, 0xb8, 0xb8));
    painter.drawText(QRect(152, 80, kSplashWidth - 220, 36),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Loading presenter…"));

    painter.setPen(QColor(0x38, 0x38, 0x38));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    return pixmap;
}

#ifdef Q_OS_WIN
/** @brief Restricts subsequent Windows DLL resolution to trusted search locations. */
void harden_windows_dll_search() {
    if (!SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_APPLICATION_DIR
            | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        SetDllDirectoryW(L"");
    }
}

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
/** @brief Performs no DLL search configuration on non-Windows platforms. */
void harden_windows_dll_search() {
}

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
    harden_windows_dll_search();
    launcher_readiness::initialize(argc, argv);
    QApplication app(argc, argv);
    const qint64 application_construction_ms = process_start_timer.elapsed();
    QApplication::setApplicationName(QStringLiteral("uil"));
    QApplication::setOrganizationName(QStringLiteral(UIL_APP_AUTHOR));
    QApplication::setOrganizationDomain(QStringLiteral("ivofilot.nl"));
    QApplication::setApplicationVersion(QStringLiteral(UIL_VERSION));
    performance_log::initialize();
    performance_log::set_process_start_offset_ms(
        process_to_main_ms + process_start_timer.elapsed());
    performance_log::record_duration(
        QStringLiteral("startup.process_to_main"),
        process_to_main_ms);
    performance_log::record_duration(
        QStringLiteral("startup.qt_application"),
        application_construction_ms);
    launcher_readiness::record_startup_metrics();
    launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_QT_RUNTIME_READY);

    QElapsedTimer loading_window_visible_timer;
    std::unique_ptr<LoadingSplash> loading_splash;
    if (!launcher_readiness::is_active()) {
        loading_splash = std::make_unique<LoadingSplash>(create_loading_splash_pixmap());
        loading_splash->show();
        app.processEvents(QEventLoop::ExcludeUserInputEvents);
        loading_window_visible_timer.start();
        performance_log::record_duration(
            QStringLiteral("startup.process_to_loading_window_visible"),
            performance_log::process_elapsed_ms());
    }

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/uil.png")));
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
    launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_THEME_READY);

    AppController controller;
    record_startup_checkpoint(QStringLiteral("construct_controller"));
    launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_CONTROLLER_READY);
    AudienceWindow audience_window;
    record_startup_checkpoint(QStringLiteral("construct_audience_window"));
    launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_AUDIENCE_READY);
    PresenterWindow presenterWindow(&controller);
    record_startup_checkpoint(QStringLiteral("construct_presenter_window"));
    launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_PRESENTER_READY);

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
    launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_CONNECTIONS_READY);

    audience_window.set_audience_screen(controller.selected_audience_screen());
    launcher_readiness::report_progress(UIL_LAUNCHER_STAGE_SHOWING_PRESENTER);
    presenterWindow.show();
    if (loading_splash) {
        loading_splash->finish(&presenterWindow);
        performance_log::record_duration(
            QStringLiteral("startup.loading_window_visible"),
            loading_window_visible_timer.elapsed());
        loading_splash.reset();
    }
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
