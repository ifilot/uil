cmake_minimum_required(VERSION 3.24)

project(uil VERSION 0.2.0 LANGUAGES C CXX)

include(CTest)

option(UIL_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(UIL_APP_AUTHOR "Ivo Filot")
set(UIL_APP_EMAIL "ivo@ivofilot.nl")
set(UIL_APP_COPYRIGHT "Copyright (C) 2026 Ivo Filot")
set(UIL_APP_LICENSE "GNU Lesser General Public License v3.0 only")
set(UIL_REPOSITORY_URL "https://github.com/ifilot/uil")

set(UIL_GIT_COMMIT "unknown")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD"
        "${CMAKE_CURRENT_SOURCE_DIR}/.git/index"
    )
endif()
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE UIL_GIT_RESULT
        OUTPUT_VARIABLE UIL_GIT_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(UIL_GIT_RESULT EQUAL 0 AND NOT UIL_GIT_OUTPUT STREQUAL "")
        set(UIL_GIT_COMMIT "${UIL_GIT_OUTPUT}")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Pdf
    Svg
)
find_package(ZLIB REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
    )
    pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi)
endif()

qt_standard_project_setup()

# Applies exploit mitigations supported by the active Windows toolchain to UIL's
# own binaries. Third-party libraries retain the protections they were built with.
function(uil_enable_windows_hardening target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /GS /guard:cf)
        target_link_options(${target} PRIVATE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf /CETCOMPAT)
    elseif(MINGW)
        target_compile_options(${target} PRIVATE
            -fstack-protector-strong
            $<$<COMPILE_LANGUAGE:CXX>:-fcf-protection=full>
        )
        target_compile_definitions(${target} PRIVATE
            $<$<CONFIG:Release>:_FORTIFY_SOURCE=2>
        )
        target_link_options(${target} PRIVATE
            -Wl,--dynamicbase
            -Wl,--nxcompat
            -Wl,--high-entropy-va
        )
    endif()
endfunction()

# Enables runtime memory and undefined-behavior checks for UIL's own targets.
function(uil_enable_sanitizers target)
    if(UIL_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
    endif()
endfunction()

set(FONT_AWESOME_SVG_FILES
    resources/fontawesome/svgs/regular/eye.svg
    resources/fontawesome/svgs/regular/eye-slash.svg
    resources/fontawesome/svgs/solid/angles-left.svg
    resources/fontawesome/svgs/solid/angles-right.svg
    resources/fontawesome/svgs/solid/arrow-pointer.svg
    resources/fontawesome/svgs/solid/chevron-left.svg
    resources/fontawesome/svgs/solid/chevron-right.svg
    resources/fontawesome/svgs/solid/door-open.svg
    resources/fontawesome/svgs/solid/eraser.svg
    resources/fontawesome/svgs/solid/location-crosshairs.svg
    resources/fontawesome/svgs/solid/pencil.svg
    resources/fontawesome/svgs/solid/satellite-dish.svg
    resources/fontawesome/svgs/solid/table-cells-large.svg
    resources/fontawesome/svgs/solid/trash-can.svg
    resources/fontawesome/svgs/solid/window-maximize.svg
    resources/fontawesome/svgs/solid/window-minimize.svg
    resources/fontawesome/svgs/solid/window-restore.svg
    resources/fontawesome/svgs/solid/xmark.svg
)

set(BLUECURVE_ACTION_ICONS
    stock-about
    stock-clear
    stock-fullscreen
    stock-go-back
    stock-go-forward
    stock-goto-first
    stock-goto-last
    stock-history
    stock-jump-to
    stock-media-pause
    stock-media-play
    stock-open
    stock-quit
    stock-save-as
    stock-save
    stock_display-grid
    stock_save-pdf
)
set(BLUECURVE_SVG_FILES)
foreach(BLUECURVE_SIZE 16 20)
    foreach(BLUECURVE_ICON IN LISTS BLUECURVE_ACTION_ICONS)
        list(APPEND BLUECURVE_SVG_FILES
            "resources/bluecurve/${BLUECURVE_SIZE}x${BLUECURVE_SIZE}/actions/${BLUECURVE_ICON}.svg"
        )
    endforeach()
endforeach()
list(APPEND BLUECURVE_SVG_FILES
    resources/bluecurve/16x16/actions/icon-resize-screen.svg
)

set(UIL_LAUNCHER_WINDOWS_RESOURCES)
set(UIL_VIEWER_WINDOWS_RESOURCES)
if(WIN32)
    set(UIL_WINDOWS_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/uil.ico")
    set(UIL_WINDOWS_SPLASH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/uil-splash.bmp")
    set(UIL_WINDOWS_MANIFEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/uil.manifest")
    set(UIL_WINDOWS_VERSION_TWEAK "${PROJECT_VERSION_TWEAK}")
    if(UIL_WINDOWS_VERSION_TWEAK STREQUAL "")
        set(UIL_WINDOWS_VERSION_TWEAK 0)
    endif()
    set(UIL_WINDOWS_FILE_VERSION
        "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${UIL_WINDOWS_VERSION_TWEAK}"
    )
    set(UIL_WINDOWS_FILE_DESCRIPTION "uil native startup launcher")
    set(UIL_WINDOWS_INTERNAL_NAME "uil")
    set(UIL_WINDOWS_ORIGINAL_FILENAME "uil.exe")
    configure_file(
        resources/windows/uil.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/uil.rc"
        @ONLY
    )
    list(APPEND UIL_LAUNCHER_WINDOWS_RESOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/uil.rc"
    )

    set(UIL_WINDOWS_FILE_DESCRIPTION "uil Qt PDF presentation viewer")
    set(UIL_WINDOWS_INTERNAL_NAME "uil-viewer")
    set(UIL_WINDOWS_ORIGINAL_FILENAME "uil-viewer.exe")
    configure_file(
        resources/windows/uil.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/uil-viewer.rc"
        @ONLY
    )
    list(APPEND UIL_VIEWER_WINDOWS_RESOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/uil-viewer.rc"
    )
endif()

if(WIN32)
    set(UIL_VIEWER_TARGET uil_viewer)
else()
    set(UIL_VIEWER_TARGET uil)
endif()

qt_add_executable(${UIL_VIEWER_TARGET}
    src/main.cpp
    src/app_controller.hpp
    src/app_controller.cpp
    src/pdf/pdf_backend.hpp
    src/pdf/qt_pdf_backend.hpp
    src/pdf/qt_pdf_backend.cpp
    src/cache/slide_cache_key.hpp
    src/cache/slide_cache.hpp
    src/cache/slide_cache.cpp
    src/render/render_scheduler.hpp
    src/render/render_scheduler.cpp
    src/ui/audience_window.hpp
    src/ui/audience_window.cpp
    src/ui/bluecurve.hpp
    src/ui/bluecurve.cpp
    src/ui/font_awesome.hpp
    src/ui/font_awesome.cpp
    src/ui/deck_overview_visibility.hpp
    src/ui/presenter_window.hpp
    src/ui/presenter_window.cpp
    src/util/document_hash.hpp
    src/util/document_hash.cpp
    src/util/example_presentations.hpp
    src/util/example_presentations.cpp
    src/util/image_util.hpp
    src/util/image_util.cpp
    src/util/performance_log.hpp
    src/util/performance_log.cpp
    src/util/launcher_readiness.hpp
    src/util/launcher_readiness.cpp
    src/util/spotlight_detector.hpp
    src/util/spotlight_detector.cpp
    src/util/pdf_media_detector.hpp
    src/util/pdf_media_detector.cpp
    src/media/video_frame_extractor.hpp
    src/media/video_frame_extractor.cpp
    src/media/video_frame_buffer.hpp
    src/media/video_frame_buffer.cpp
    src/molecule/molecule_geometry.hpp
    src/molecule/molecule_geometry.cpp
    src/molecule/molecule_camera.hpp
    src/molecule/molecule_camera.cpp
    src/ui/molecule_widget.hpp
    src/ui/molecule_widget.cpp
    src/package/uil_package.hpp
    src/package/uil_package.cpp
    ${UIL_VIEWER_WINDOWS_RESOURCES}
)

qt_add_resources(${UIL_VIEWER_TARGET} "uil_resources"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/icons/uil.png
        resources/icons/uil-splash.bmp
        resources/icons/spinbox-chevron-down.svg
        resources/icons/spinbox-chevron-up.svg
        resources/bluecurve/LICENSE.txt
        resources/bluecurve/README.md
        ${BLUECURVE_SVG_FILES}
        resources/fontawesome/LICENSE.txt
        resources/fontawesome/package.json
        ${FONT_AWESOME_SVG_FILES}
        resources/styles/vscode.qss
)

target_include_directories(${UIL_VIEWER_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${UIL_VIEWER_TARGET} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Pdf
    Qt6::Svg
    ZLIB::ZLIB
)

target_compile_definitions(${UIL_VIEWER_TARGET} PRIVATE
    UIL_VERSION="${PROJECT_VERSION}"
    UIL_VERSION_DISPLAY="v${PROJECT_VERSION}"
    UIL_APP_AUTHOR="${UIL_APP_AUTHOR}"
    UIL_APP_EMAIL="${UIL_APP_EMAIL}"
    UIL_APP_COPYRIGHT="${UIL_APP_COPYRIGHT}"
    UIL_APP_LICENSE="${UIL_APP_LICENSE}"
    UIL_REPOSITORY_URL="${UIL_REPOSITORY_URL}"
    UIL_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    UIL_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    UIL_CMAKE_VERSION="${CMAKE_VERSION}"
    UIL_BUILD_CONFIG="$<CONFIG>"
    UIL_GIT_COMMIT="${UIL_GIT_COMMIT}"
)

set_target_properties(${UIL_VIEWER_TARGET} PROPERTIES
    WIN32_EXECUTABLE TRUE
)
uil_enable_windows_hardening(${UIL_VIEWER_TARGET})
uil_enable_sanitizers(${UIL_VIEWER_TARGET})

if(WIN32)
    set_target_properties(${UIL_VIEWER_TARGET} PROPERTIES
        OUTPUT_NAME "uil-viewer"
    )
    add_executable(uil WIN32
        src/launcher/windows_launcher.c
        ${UIL_LAUNCHER_WINDOWS_RESOURCES}
    )
    set_target_properties(uil PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
        LINKER_LANGUAGE C
    )
    target_compile_definitions(uil PRIVATE
        UNICODE
        _UNICODE
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    target_link_libraries(uil PRIVATE
        user32
        gdi32
        shell32
    )
    uil_enable_windows_hardening(uil)
    uil_enable_sanitizers(uil)
endif()

if(FFMPEG_FOUND)
    target_compile_definitions(${UIL_VIEWER_TARGET} PRIVATE UIL_HAVE_FFMPEG=1)
    target_include_directories(${UIL_VIEWER_TARGET} PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_options(${UIL_VIEWER_TARGET} PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

if(HIDAPI_FOUND)
    target_compile_definitions(${UIL_VIEWER_TARGET} PRIVATE UIL_HAVE_HIDAPI=1)
    target_link_libraries(${UIL_VIEWER_TARGET} PRIVATE PkgConfig::HIDAPI)
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
