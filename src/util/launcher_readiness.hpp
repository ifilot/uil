#pragma once

/**
 * @brief Viewer side of the native-launcher startup protocol (see launcher/launcher_protocol.h).
 *
 * When `uil-viewer.exe` is started by the Windows launcher, these functions report startup
 * progress to the launcher's splash window and signal its readiness event. When the viewer is
 * started directly, or on non-Windows platforms, every function except argument parsing is a
 * no-op.
 *
 * The state is process-wide and unsynchronized: call these functions from the main (GUI)
 * thread only.
 */
namespace launcher_readiness {

/**
 * @brief Reads native-launcher readiness metadata from the process arguments.
 *
 * Recognizes the `--uil-ready-event=`, `--uil-launcher-window=`,
 * `--uil-launcher-process-start-filetime=` and `--uil-launcher-splash-visible-filetime=`
 * arguments and, on Windows, opens the named readiness event. Call once at startup; main()
 * does so before constructing QApplication. The arguments are left in place for Qt.
 */
void initialize(int argc, char* argv[]);

/** @brief Returns whether a non-empty `--uil-ready-event=` argument was supplied. */
bool is_active();

/**
 * @brief Records native-launcher timing metrics after logging is initialized.
 *
 * Logs the launcher-process-to-splash and launcher-process-to-viewer-logger durations. Does
 * nothing unless launched by the launcher on Windows with a process start time.
 */
void record_startup_metrics();

/**
 * @brief Reports a confirmed viewer startup stage to the native launcher.
 *
 * Posts #UIL_LAUNCHER_PROGRESS_MESSAGE with @p stage as WPARAM and 0 as LPARAM to the launcher
 * window and records a `startup.launcher_progress` event. Does nothing unless launched by the
 * launcher on Windows; posting fails harmlessly once the launcher has exited.
 *
 * @param stage One of the `UIL_LAUNCHER_STAGE_*` values, reported in increasing order.
 */
void report_progress(int stage);

/**
 * @brief Signals that the presenter has completed its first paint.
 *
 * Sets and closes the launcher's readiness event so the launcher removes its splash and exits.
 * Only the first successful call has an effect; does nothing unless launched by the launcher
 * on Windows.
 */
void signal_ready();

}  // namespace launcher_readiness
