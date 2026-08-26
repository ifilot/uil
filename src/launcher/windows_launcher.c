#include <windows.h>
#include <shellapi.h>
#include <stddef.h>
#include <wchar.h>

#include "launcher_protocol.h"

#define UIL_ICON_RESOURCE_ID 101
#define UIL_COMMAND_LINE_CAPACITY 32768
#define UIL_SPLASH_WIDTH 420
#define UIL_SPLASH_HEIGHT 176
#define UIL_ANIMATION_TIMER_ID 1
#define UIL_ANIMATION_INTERVAL_MS 40

static int loading_stage = UIL_LAUNCHER_STAGE_STARTING_VIEWER;
static int displayed_progress = 0;
static int target_progress = 8;
static int animation_tick = 0;

/** @brief Returns the display percentage associated with a confirmed startup stage. */
static int stage_progress(int stage) {
    switch (stage) {
    case UIL_LAUNCHER_STAGE_STARTING_VIEWER:
        return 8;
    case UIL_LAUNCHER_STAGE_QT_RUNTIME_READY:
        return 20;
    case UIL_LAUNCHER_STAGE_THEME_READY:
        return 30;
    case UIL_LAUNCHER_STAGE_CONTROLLER_READY:
        return 42;
    case UIL_LAUNCHER_STAGE_AUDIENCE_READY:
        return 52;
    case UIL_LAUNCHER_STAGE_PRESENTER_READY:
        return 72;
    case UIL_LAUNCHER_STAGE_CONNECTIONS_READY:
        return 82;
    case UIL_LAUNCHER_STAGE_SHOWING_PRESENTER:
        return 92;
    case UIL_LAUNCHER_STAGE_FIRST_PAINT_READY:
        return 100;
    default:
        return target_progress;
    }
}

/** @brief Returns user-facing text for a confirmed startup stage. */
static const wchar_t* stage_description(int stage) {
    switch (stage) {
    case UIL_LAUNCHER_STAGE_STARTING_VIEWER:
        return L"Starting viewer\x2026";
    case UIL_LAUNCHER_STAGE_QT_RUNTIME_READY:
        return L"Qt runtime ready";
    case UIL_LAUNCHER_STAGE_THEME_READY:
        return L"Applying interface theme\x2026";
    case UIL_LAUNCHER_STAGE_CONTROLLER_READY:
        return L"Preparing document engine\x2026";
    case UIL_LAUNCHER_STAGE_AUDIENCE_READY:
        return L"Preparing presentation display\x2026";
    case UIL_LAUNCHER_STAGE_PRESENTER_READY:
        return L"Building presenter interface\x2026";
    case UIL_LAUNCHER_STAGE_CONNECTIONS_READY:
        return L"Connecting application components\x2026";
    case UIL_LAUNCHER_STAGE_SHOWING_PRESENTER:
        return L"Opening presenter window\x2026";
    case UIL_LAUNCHER_STAGE_FIRST_PAINT_READY:
        return L"Presenter ready";
    default:
        return L"Loading presenter\x2026";
    }
}

/** @brief Advances the animated display toward its latest confirmed progress target. */
static void advance_loading_animation(HWND window) {
    if (displayed_progress < target_progress) {
        ++displayed_progress;
    }
    ++animation_tick;
    InvalidateRect(window, NULL, FALSE);
}

/** @brief Accepts a monotonic confirmed startup stage from the viewer. */
static void update_loading_stage(HWND window, int stage) {
    if (stage < loading_stage || stage > UIL_LAUNCHER_STAGE_FIRST_PAINT_READY) {
        return;
    }
    loading_stage = stage;
    target_progress = stage_progress(stage);
    InvalidateRect(window, NULL, FALSE);
}

/** @brief Paints a smoothly rotating twelve-segment activity spinner. */
static void paint_loading_spinner(HDC device_context) {
    static const int x_offsets[12] = {0, 7, 12, 14, 12, 7, 0, -7, -12, -14, -12, -7};
    static const int y_offsets[12] = {-14, -12, -7, 0, 7, 12, 14, 12, 7, 0, -7, -12};
    const int center_x = UIL_SPLASH_WIDTH - 52;
    const int center_y = 98;
    const int active_segment = (animation_tick / 2) % 12;
    HGDIOBJ previous_pen = SelectObject(device_context, GetStockObject(NULL_PEN));
    int segment;

    for (segment = 0; segment < 12; ++segment) {
        const int trail_distance = (active_segment - segment + 12) % 12;
        const int radius = trail_distance == 0 ? 4 : 3;
        COLORREF color;
        HBRUSH brush;
        HGDIOBJ previous_brush;

        if (trail_distance == 0) {
            color = RGB(0x00, 0xd0, 0xd0);
        } else if (trail_distance <= 2) {
            color = RGB(0x00, 0xa8, 0xa8);
        } else if (trail_distance <= 4) {
            color = RGB(0x00, 0x78, 0x78);
        } else if (trail_distance <= 7) {
            color = RGB(0x3e, 0x58, 0x58);
        } else {
            color = RGB(0x38, 0x38, 0x38);
        }

        brush = CreateSolidBrush(color);
        previous_brush = SelectObject(device_context, brush);
        Ellipse(
            device_context,
            center_x + x_offsets[segment] - radius,
            center_y + y_offsets[segment] - radius,
            center_x + x_offsets[segment] + radius,
            center_y + y_offsets[segment] + radius);
        SelectObject(device_context, previous_brush);
        DeleteObject(brush);
    }

    SelectObject(device_context, previous_pen);
}

/** @brief Appends one character to a bounded Windows command line. */
static BOOL append_command_character(wchar_t** cursor, size_t* remaining, wchar_t character) {
    if (*remaining <= 1) {
        return FALSE;
    }
    **cursor = character;
    ++(*cursor);
    --(*remaining);
    **cursor = L'\0';
    return TRUE;
}

/** @brief Appends one argument using the Windows command-line quoting rules. */
static BOOL append_quoted_argument(
    wchar_t** cursor,
    size_t* remaining,
    const wchar_t* argument) {
    size_t backslash_count = 0;
    const wchar_t* character = argument;

    if (!append_command_character(cursor, remaining, L'"')) {
        return FALSE;
    }

    while (*character != L'\0') {
        if (*character == L'\\') {
            ++backslash_count;
            ++character;
            continue;
        }

        if (*character == L'"') {
            size_t index;
            for (index = 0; index < backslash_count * 2 + 1; ++index) {
                if (!append_command_character(cursor, remaining, L'\\')) {
                    return FALSE;
                }
            }
            if (!append_command_character(cursor, remaining, L'"')) {
                return FALSE;
            }
        } else {
            size_t index;
            for (index = 0; index < backslash_count; ++index) {
                if (!append_command_character(cursor, remaining, L'\\')) {
                    return FALSE;
                }
            }
            if (!append_command_character(cursor, remaining, *character)) {
                return FALSE;
            }
        }

        backslash_count = 0;
        ++character;
    }

    while (backslash_count > 0) {
        if (!append_command_character(cursor, remaining, L'\\') ||
            !append_command_character(cursor, remaining, L'\\')) {
            return FALSE;
        }
        --backslash_count;
    }

    if (!append_command_character(cursor, remaining, L'"') ||
        !append_command_character(cursor, remaining, L' ')) {
        return FALSE;
    }
    return TRUE;
}

/** @brief Returns a 64-bit integer representation of a Windows file time. */
static ULONGLONG file_time_value(FILETIME file_time) {
    ULARGE_INTEGER value;
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}

/** @brief Returns the operating-system creation time of the launcher process. */
static ULONGLONG launcher_creation_file_time(void) {
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    if (!GetProcessTimes(
            GetCurrentProcess(),
            &creation_time,
            &exit_time,
            &kernel_time,
            &user_time)) {
        return 0;
    }
    return file_time_value(creation_time);
}

/** @brief Paints the lightweight launcher window using only GDI. */
static void paint_loading_window(HWND window) {
    PAINTSTRUCT paint;
    RECT client_rect;
    RECT accent_rect;
    RECT title_rect;
    RECT message_rect;
    RECT progress_track_rect;
    RECT progress_fill_rect;
    HBRUSH background_brush;
    HBRUSH accent_brush;
    HBRUSH progress_track_brush;
    HBRUSH progress_fill_brush;
    HPEN border_pen;
    HFONT title_font;
    HFONT message_font;
    HGDIOBJ previous_font;
    HGDIOBJ previous_pen;
    HDC device_context = BeginPaint(window, &paint);

    GetClientRect(window, &client_rect);
    background_brush = CreateSolidBrush(RGB(0x18, 0x18, 0x18));
    FillRect(device_context, &client_rect, background_brush);
    DeleteObject(background_brush);

    accent_rect = client_rect;
    accent_rect.right = accent_rect.left + 6;
    accent_brush = CreateSolidBrush(RGB(0x00, 0x8c, 0x8c));
    FillRect(device_context, &accent_rect, accent_brush);
    DeleteObject(accent_brush);

    SetBkMode(device_context, TRANSPARENT);
    SetTextColor(device_context, RGB(0xee, 0xee, 0xee));
    title_font = CreateFontW(
        -34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    previous_font = SelectObject(device_context, title_font);
    SetRect(&title_rect, 32, 24, UIL_SPLASH_WIDTH - 32, 72);
    DrawTextW(device_context, L"uil", -1, &title_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(device_context, RGB(0xb8, 0xb8, 0xb8));
    message_font = CreateFontW(
        -17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(device_context, message_font);
    SetRect(&message_rect, 34, 80, UIL_SPLASH_WIDTH - 84, 116);
    DrawTextW(
        device_context,
        stage_description(loading_stage),
        -1,
        &message_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    paint_loading_spinner(device_context);

    SetRect(&progress_track_rect, 34, 132, UIL_SPLASH_WIDTH - 34, 138);
    progress_track_brush = CreateSolidBrush(RGB(0x32, 0x32, 0x32));
    FillRect(device_context, &progress_track_rect, progress_track_brush);
    DeleteObject(progress_track_brush);

    progress_fill_rect = progress_track_rect;
    progress_fill_rect.right = progress_fill_rect.left +
        (progress_track_rect.right - progress_track_rect.left) * displayed_progress / 100;
    progress_fill_brush = CreateSolidBrush(RGB(0x00, 0x8c, 0x8c));
    FillRect(device_context, &progress_fill_rect, progress_fill_brush);
    DeleteObject(progress_fill_brush);

    SelectObject(device_context, previous_font);
    DeleteObject(message_font);
    DeleteObject(title_font);

    border_pen = CreatePen(PS_SOLID, 1, RGB(0x38, 0x38, 0x38));
    previous_pen = SelectObject(device_context, border_pen);
    SelectObject(device_context, GetStockObject(NULL_BRUSH));
    Rectangle(device_context, 0, 0, client_rect.right, client_rect.bottom);
    SelectObject(device_context, previous_pen);
    DeleteObject(border_pen);
    EndPaint(window, &paint);
}

/** @brief Handles messages for the native launcher loading window. */
static LRESULT CALLBACK loading_window_procedure(
    HWND window,
    UINT message,
    WPARAM word_parameter,
    LPARAM long_parameter) {
    (void)word_parameter;
    (void)long_parameter;

    switch (message) {
    case UIL_LAUNCHER_PROGRESS_MESSAGE:
        update_loading_stage(window, (int)word_parameter);
        return 0;
    case WM_TIMER:
        if (word_parameter == UIL_ANIMATION_TIMER_ID) {
            advance_loading_animation(window);
        }
        return 0;
    case WM_PAINT:
        paint_loading_window(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        return 0;
    case WM_DESTROY:
        KillTimer(window, UIL_ANIMATION_TIMER_ID);
        return 0;
    default:
        return DefWindowProcW(window, message, word_parameter, long_parameter);
    }
}

/** @brief Creates, centers, shows, and synchronously paints the loading window. */
static HWND create_loading_window(HINSTANCE instance) {
    static const wchar_t class_name[] = L"uil_native_loading_window";
    WNDCLASSEXW window_class;
    RECT work_area;
    int x;
    int y;
    HWND window;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = loading_window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(UIL_ICON_RESOURCE_ID));
    window_class.lpszClassName = class_name;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return NULL;
    }

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    x = work_area.left + (work_area.right - work_area.left - UIL_SPLASH_WIDTH) / 2;
    y = work_area.top + (work_area.bottom - work_area.top - UIL_SPLASH_HEIGHT) / 2;
    window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        class_name,
        L"uil",
        WS_POPUP,
        x,
        y,
        UIL_SPLASH_WIDTH,
        UIL_SPLASH_HEIGHT,
        NULL,
        NULL,
        instance,
        NULL);
    if (!window) {
        return NULL;
    }

    ShowWindow(window, SW_SHOWNORMAL);
    SetTimer(
        window,
        UIL_ANIMATION_TIMER_ID,
        UIL_ANIMATION_INTERVAL_MS,
        NULL);
    UpdateWindow(window);
    return window;
}

/** @brief Resolves the viewer executable and its working directory beside the launcher. */
static BOOL resolve_viewer_paths(
    wchar_t* viewer_path,
    DWORD viewer_capacity,
    wchar_t* working_directory,
    DWORD directory_capacity) {
    DWORD path_length = GetModuleFileNameW(NULL, viewer_path, viewer_capacity);
    wchar_t* separator;
    static const wchar_t viewer_name[] = L"uil-viewer.exe";

    if (path_length == 0 || path_length >= viewer_capacity) {
        return FALSE;
    }
    separator = wcsrchr(viewer_path, L'\\');
    if (!separator) {
        return FALSE;
    }

    if ((DWORD)(separator - viewer_path) + 1 >= directory_capacity) {
        return FALSE;
    }
    CopyMemory(
        working_directory,
        viewer_path,
        ((size_t)(separator - viewer_path) + 1) * sizeof(wchar_t));
    working_directory[separator - viewer_path + 1] = L'\0';

    ++separator;
    if ((size_t)(separator - viewer_path) + ARRAYSIZE(viewer_name) > viewer_capacity) {
        return FALSE;
    }
    CopyMemory(separator, viewer_name, sizeof(viewer_name));
    return GetFileAttributesW(viewer_path) != INVALID_FILE_ATTRIBUTES;
}

/** @brief Builds the viewer command line with readiness metadata and forwarded arguments. */
static BOOL build_viewer_command_line(
    wchar_t* command_line,
    size_t capacity,
    const wchar_t* viewer_path,
    const wchar_t* event_name,
    HWND launcher_window,
    ULONGLONG process_start_file_time,
    ULONGLONG splash_visible_file_time) {
    wchar_t** arguments;
    int argument_count;
    int index;
    wchar_t ready_argument[160];
    wchar_t start_argument[128];
    wchar_t splash_argument[128];
    wchar_t window_argument[128];
    wchar_t* cursor = command_line;
    size_t remaining = capacity;

    command_line[0] = L'\0';
    arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (!arguments) {
        return FALSE;
    }

    wsprintfW(ready_argument, L"--uil-ready-event=%s", event_name);
    wsprintfW(
        start_argument,
        L"--uil-launcher-process-start-filetime=%I64u",
        process_start_file_time);
    wsprintfW(
        splash_argument,
        L"--uil-launcher-splash-visible-filetime=%I64u",
        splash_visible_file_time);
    wsprintfW(
        window_argument,
        L"--uil-launcher-window=%I64u",
        (ULONGLONG)(ULONG_PTR)launcher_window);

    if (!append_quoted_argument(&cursor, &remaining, viewer_path) ||
        !append_quoted_argument(&cursor, &remaining, ready_argument) ||
        !append_quoted_argument(&cursor, &remaining, start_argument) ||
        !append_quoted_argument(&cursor, &remaining, splash_argument) ||
        !append_quoted_argument(&cursor, &remaining, window_argument)) {
        LocalFree(arguments);
        return FALSE;
    }

    for (index = 1; index < argument_count; ++index) {
        if (!append_quoted_argument(&cursor, &remaining, arguments[index])) {
            LocalFree(arguments);
            return FALSE;
        }
    }

    if (cursor > command_line && cursor[-1] == L' ') {
        cursor[-1] = L'\0';
    }
    LocalFree(arguments);
    return TRUE;
}

typedef struct ViewerLaunchRequest {
    const wchar_t* viewer_path;
    wchar_t* command_line;
    const wchar_t* working_directory;
    HANDLE completion_event;
    PROCESS_INFORMATION process_information;
    BOOL process_started;
} ViewerLaunchRequest;

/** @brief Starts the Qt viewer without blocking the launcher's animation thread. */
static DWORD WINAPI start_viewer_process(LPVOID parameter) {
    ViewerLaunchRequest* request = (ViewerLaunchRequest*)parameter;
    STARTUPINFOW startup_info;

    ZeroMemory(&startup_info, sizeof(startup_info));
    ZeroMemory(&request->process_information, sizeof(request->process_information));
    startup_info.cb = sizeof(startup_info);
    request->process_started = CreateProcessW(
        request->viewer_path,
        request->command_line,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        request->working_directory,
        &startup_info,
        &request->process_information);
    SetEvent(request->completion_event);
    return 0;
}

/** @brief Pumps animation and progress messages while Windows creates the viewer process. */
static BOOL wait_for_process_launch(HWND window, HANDLE completion_event) {
    MSG message;

    for (;;) {
        DWORD result = MsgWaitForMultipleObjects(
            1,
            &completion_event,
            FALSE,
            INFINITE,
            QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) {
            return TRUE;
        }
        if (result == WAIT_OBJECT_0 + 1) {
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }
        DestroyWindow(window);
        return FALSE;
    }
}

/** @brief Pumps launcher messages until the viewer is ready or terminates. */
static int wait_for_viewer(HWND window, HANDLE ready_event, HANDLE viewer_process) {
    HANDLE wait_handles[2] = {ready_event, viewer_process};
    MSG message;

    for (;;) {
        DWORD result = MsgWaitForMultipleObjects(2, wait_handles, FALSE, INFINITE, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) {
            DestroyWindow(window);
            return 0;
        }
        if (result == WAIT_OBJECT_0 + 1) {
            DWORD exit_code = 0;
            GetExitCodeProcess(viewer_process, &exit_code);
            DestroyWindow(window);
            MessageBoxW(
                NULL,
                exit_code == 0
                    ? L"The viewer closed before its first window was ready."
                    : L"The viewer could not start. Check the application log for details.",
                L"uil startup error",
                MB_OK | MB_ICONERROR);
            return exit_code == 0 ? 1 : (int)exit_code;
        }
        if (result == WAIT_OBJECT_0 + 2) {
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }
        DestroyWindow(window);
        return 1;
    }
}

/** @brief Shows the native splash, starts the Qt viewer, and waits for readiness. */
int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previous_instance,
    LPSTR command_line_argument,
    int show_command) {
    wchar_t viewer_path[MAX_PATH];
    wchar_t working_directory[MAX_PATH];
    wchar_t event_name[128];
    wchar_t* viewer_command_line;
    HANDLE ready_event;
    HANDLE launch_thread;
    HWND loading_window;
    FILETIME splash_visible_time;
    ViewerLaunchRequest launch_request;
    int result;

    (void)previous_instance;
    (void)command_line_argument;
    (void)show_command;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    loading_window = create_loading_window(instance);
    if (!loading_window) {
        MessageBoxW(NULL, L"Could not create the loading window.", L"uil startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!resolve_viewer_paths(
            viewer_path,
            ARRAYSIZE(viewer_path),
            working_directory,
            ARRAYSIZE(working_directory))) {
        DestroyWindow(loading_window);
        MessageBoxW(NULL, L"Could not find uil-viewer.exe beside the launcher.", L"uil startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    wsprintfW(
        event_name,
        L"Local\\uil-ready-%lu-%I64u",
        GetCurrentProcessId(),
        GetTickCount64());
    ready_event = CreateEventW(NULL, TRUE, FALSE, event_name);
    if (!ready_event) {
        DestroyWindow(loading_window);
        MessageBoxW(NULL, L"Could not create the viewer readiness event.", L"uil startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    GetSystemTimeAsFileTime(&splash_visible_time);
    viewer_command_line = HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        UIL_COMMAND_LINE_CAPACITY * sizeof(wchar_t));
    if (!viewer_command_line ||
        !build_viewer_command_line(
            viewer_command_line,
            UIL_COMMAND_LINE_CAPACITY,
            viewer_path,
            event_name,
            loading_window,
            launcher_creation_file_time(),
            file_time_value(splash_visible_time))) {
        if (viewer_command_line) {
            HeapFree(GetProcessHeap(), 0, viewer_command_line);
        }
        CloseHandle(ready_event);
        DestroyWindow(loading_window);
        MessageBoxW(NULL, L"Could not prepare the viewer command line.", L"uil startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ZeroMemory(&launch_request, sizeof(launch_request));
    launch_request.viewer_path = viewer_path;
    launch_request.command_line = viewer_command_line;
    launch_request.working_directory = working_directory;
    launch_request.completion_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!launch_request.completion_event) {
        HeapFree(GetProcessHeap(), 0, viewer_command_line);
        CloseHandle(ready_event);
        DestroyWindow(loading_window);
        MessageBoxW(NULL, L"Could not prepare viewer startup.", L"uil startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    launch_thread = CreateThread(
        NULL,
        0,
        start_viewer_process,
        &launch_request,
        0,
        NULL);
    if (!launch_thread) {
        CloseHandle(launch_request.completion_event);
        HeapFree(GetProcessHeap(), 0, viewer_command_line);
        CloseHandle(ready_event);
        DestroyWindow(loading_window);
        MessageBoxW(NULL, L"Could not create the viewer startup worker.", L"uil startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!wait_for_process_launch(loading_window, launch_request.completion_event)) {
        WaitForSingleObject(launch_thread, INFINITE);
        CloseHandle(launch_thread);
        CloseHandle(launch_request.completion_event);
        HeapFree(GetProcessHeap(), 0, viewer_command_line);
        CloseHandle(ready_event);
        return 1;
    }

    WaitForSingleObject(launch_thread, INFINITE);
    CloseHandle(launch_thread);
    CloseHandle(launch_request.completion_event);
    HeapFree(GetProcessHeap(), 0, viewer_command_line);
    if (!launch_request.process_started) {
        CloseHandle(ready_event);
        DestroyWindow(loading_window);
        MessageBoxW(NULL, L"Could not start uil-viewer.exe.", L"uil startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    CloseHandle(launch_request.process_information.hThread);
    result = wait_for_viewer(
        loading_window,
        ready_event,
        launch_request.process_information.hProcess);
    CloseHandle(launch_request.process_information.hProcess);
    CloseHandle(ready_event);
    return result;
}
