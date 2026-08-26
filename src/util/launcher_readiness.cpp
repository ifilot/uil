#include "launcher_readiness.hpp"

#include "launcher/launcher_protocol.h"
#include "performance_log.hpp"

#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

struct LauncherReadinessState {
#ifdef Q_OS_WIN
    HANDLE event_handle = nullptr;
#endif
    QString event_name;
    quintptr launcher_window = 0;
    quint64 process_start_file_time = 0;
    quint64 splash_visible_file_time = 0;
    bool requested = false;
    bool signaled = false;
};

/** @brief Returns process-lifetime launcher readiness state. */
LauncherReadinessState& readiness_state() {
    static LauncherReadinessState* state = new LauncherReadinessState;
    return *state;
}

/** @brief Returns the value following an internal command-line prefix. */
QString argument_value(const QString& argument, const QString& prefix) {
    return argument.startsWith(prefix) ? argument.mid(prefix.size()) : QString();
}

#ifdef Q_OS_WIN
/** @brief Returns the current Windows system time as a 64-bit file-time value. */
quint64 current_file_time() {
    FILETIME file_time;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&file_time);
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}

/** @brief Opens the named launcher readiness event when possible. */
void open_readiness_event(LauncherReadinessState& state) {
    if (state.event_handle || state.event_name.isEmpty()) {
        return;
    }
    state.event_handle = OpenEventW(
        EVENT_MODIFY_STATE,
        FALSE,
        reinterpret_cast<LPCWSTR>(state.event_name.utf16()));
}
#endif

}  // namespace

namespace launcher_readiness {

void initialize(int argc, char* argv[]) {
    LauncherReadinessState& state = readiness_state();
    const QString event_prefix = QStringLiteral("--uil-ready-event=");
    const QString process_start_prefix =
        QStringLiteral("--uil-launcher-process-start-filetime=");
    const QString splash_visible_prefix =
        QStringLiteral("--uil-launcher-splash-visible-filetime=");
    const QString launcher_window_prefix =
        QStringLiteral("--uil-launcher-window=");

    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument.startsWith(event_prefix)) {
            state.event_name = argument_value(argument, event_prefix);
            state.requested = !state.event_name.isEmpty();
        } else if (argument.startsWith(process_start_prefix)) {
            state.process_start_file_time =
                argument_value(argument, process_start_prefix).toULongLong();
        } else if (argument.startsWith(splash_visible_prefix)) {
            state.splash_visible_file_time =
                argument_value(argument, splash_visible_prefix).toULongLong();
        } else if (argument.startsWith(launcher_window_prefix)) {
            state.launcher_window = quintptr(
                argument_value(argument, launcher_window_prefix).toULongLong());
        }
    }

#ifdef Q_OS_WIN
    open_readiness_event(state);
#endif
}

bool is_active() {
    return readiness_state().requested;
}

void record_startup_metrics() {
    const LauncherReadinessState& state = readiness_state();
    if (!state.requested || state.process_start_file_time == 0) {
        return;
    }

#ifdef Q_OS_WIN
    const quint64 now = current_file_time();
    if (state.splash_visible_file_time >= state.process_start_file_time) {
        performance_log::record_duration(
            QStringLiteral("startup.launcher_process_to_native_splash"),
            qint64((state.splash_visible_file_time - state.process_start_file_time) / 10'000ULL));
    }
    if (now >= state.process_start_file_time) {
        performance_log::record_duration(
            QStringLiteral("startup.launcher_process_to_viewer_logger"),
            qint64((now - state.process_start_file_time) / 10'000ULL));
    }
#endif
}

void report_progress(int stage) {
    LauncherReadinessState& state = readiness_state();
    if (!state.requested || state.launcher_window == 0) {
        return;
    }

#ifdef Q_OS_WIN
    PostMessageW(
        reinterpret_cast<HWND>(state.launcher_window),
        UIL_LAUNCHER_PROGRESS_MESSAGE,
        WPARAM(stage),
        0);
    performance_log::record_event(
        QStringLiteral("startup.launcher_progress"),
        {{QStringLiteral("stage"), stage}});
#else
    Q_UNUSED(stage)
#endif
}

void signal_ready() {
    LauncherReadinessState& state = readiness_state();
    if (!state.requested || state.signaled) {
        return;
    }

#ifdef Q_OS_WIN
    open_readiness_event(state);
    if (state.event_handle) {
        SetEvent(state.event_handle);
        CloseHandle(state.event_handle);
        state.event_handle = nullptr;
        state.signaled = true;
        performance_log::record_event(QStringLiteral("startup.native_launcher_ready_signaled"));
    }
#endif
}

}  // namespace launcher_readiness
