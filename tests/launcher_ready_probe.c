#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#include "launcher/launcher_protocol.h"

/** @brief Returns whether a command-line argument begins with a prefix. */
static BOOL starts_with(const wchar_t* argument, const wchar_t* prefix) {
    return wcsncmp(argument, prefix, wcslen(prefix)) == 0;
}

/** @brief Validates argument forwarding and signals the launcher's readiness event. */
int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previous_instance,
    LPSTR command_line_argument,
    int show_command) {
    static const wchar_t event_prefix[] = L"--uil-ready-event=";
    static const wchar_t window_prefix[] = L"--uil-launcher-window=";
    static const wchar_t forwarded_argument[] = L"--probe-forwarded=hello world";
    wchar_t** arguments;
    const wchar_t* event_name = NULL;
    HWND launcher_window = NULL;
    BOOL forwarded_argument_found = FALSE;
    HANDLE event_handle;
    int argument_count;
    int index;

    (void)instance;
    (void)previous_instance;
    (void)command_line_argument;
    (void)show_command;

    arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (!arguments) {
        return 2;
    }

    for (index = 1; index < argument_count; ++index) {
        if (starts_with(arguments[index], event_prefix)) {
            event_name = arguments[index] + ARRAYSIZE(event_prefix) - 1;
        } else if (starts_with(arguments[index], window_prefix)) {
            launcher_window = (HWND)(ULONG_PTR)wcstoull(
                arguments[index] + ARRAYSIZE(window_prefix) - 1,
                NULL,
                10);
        } else if (wcscmp(arguments[index], forwarded_argument) == 0) {
            forwarded_argument_found = TRUE;
        }
    }

    if (!event_name || !IsWindow(launcher_window) || !forwarded_argument_found) {
        LocalFree(arguments);
        return 3;
    }

    event_handle = OpenEventW(
        EVENT_MODIFY_STATE,
        FALSE,
        event_name);
    if (!event_handle) {
        LocalFree(arguments);
        return 4;
    }

    Sleep(200);
    PostMessageW(
        launcher_window,
        UIL_LAUNCHER_PROGRESS_MESSAGE,
        UIL_LAUNCHER_STAGE_CONTROLLER_READY,
        0);
    PostMessageW(
        launcher_window,
        UIL_LAUNCHER_PROGRESS_MESSAGE,
        UIL_LAUNCHER_STAGE_FIRST_PAINT_READY,
        0);
    Sleep(80);
    if (!SetEvent(event_handle)) {
        CloseHandle(event_handle);
        LocalFree(arguments);
        return 5;
    }

    CloseHandle(event_handle);
    LocalFree(arguments);
    return 0;
}
