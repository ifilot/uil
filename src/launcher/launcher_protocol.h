#pragma once

/**
 * @file launcher_protocol.h
 * @brief Cross-process startup protocol between the native Windows launcher (`uil.exe`) and
 *        the Qt viewer (`uil-viewer.exe`).
 *
 * This header is shared by C and C++ code and deliberately does not include `<windows.h>`, so
 * it can also be included on platforms without the Win32 API (where the stage constants are
 * still referenced but no messages are sent).
 *
 * The launcher starts the viewer with these leading arguments, followed by its own arguments:
 * - `--uil-ready-event=<name>`: a manual-reset named event the viewer sets once the presenter
 *   window has painted for the first time.
 * - `--uil-launcher-window=<decimal HWND>`: the splash window that receives progress messages.
 * - `--uil-launcher-process-start-filetime=<FILETIME>` and
 *   `--uil-launcher-splash-visible-filetime=<FILETIME>`: timestamps for startup metrics.
 *
 * Progress is reported with `PostMessageW(window, UIL_LAUNCHER_PROGRESS_MESSAGE, stage, 0)`:
 * WPARAM carries one of the `UIL_LAUNCHER_STAGE_*` values and LPARAM is always 0. Stages are
 * monotonic; the launcher ignores a stage lower than the last one it accepted and any value
 * above #UIL_LAUNCHER_STAGE_FIRST_PAINT_READY. Messages are best effort: the launcher may have
 * stopped waiting (see its readiness timeout), in which case posting simply fails.
 */

/**
 * @brief Private window message carrying a startup stage, equal to `WM_APP + 0x155`.
 *
 * Spelled with the numeric value of `WM_APP` (0x8000) so the header stays self-contained;
 * windows_launcher.c statically checks that both spellings agree.
 */
#define UIL_LAUNCHER_PROGRESS_MESSAGE (0x8000 + 0x155)

/** @brief The launcher is creating the viewer process (initial splash state). */
#define UIL_LAUNCHER_STAGE_STARTING_VIEWER 1
/** @brief The viewer has constructed its QApplication and set up logging. */
#define UIL_LAUNCHER_STAGE_QT_RUNTIME_READY 2
/** @brief The application style and theme have been applied. */
#define UIL_LAUNCHER_STAGE_THEME_READY 3
/** @brief The application controller has been constructed. */
#define UIL_LAUNCHER_STAGE_CONTROLLER_READY 4
/** @brief The audience window has been constructed. */
#define UIL_LAUNCHER_STAGE_AUDIENCE_READY 5
/** @brief The presenter window has been constructed. */
#define UIL_LAUNCHER_STAGE_PRESENTER_READY 6
/** @brief The windows are connected to the application controller. */
#define UIL_LAUNCHER_STAGE_CONNECTIONS_READY 7
/** @brief The presenter window is being shown. */
#define UIL_LAUNCHER_STAGE_SHOWING_PRESENTER 8
/** @brief The presenter has painted its first frame; the ready event is set next. */
#define UIL_LAUNCHER_STAGE_FIRST_PAINT_READY 9
