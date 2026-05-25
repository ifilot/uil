cmake_minimum_required(VERSION 3.24)

project(uil VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(UIL_APP_AUTHOR "Ivo Filot")
set(UIL_APP_EMAIL "ivo@ivofilot.nl")
set(UIL_APP_COPYRIGHT "Copyright (C) 2026 Ivo Filot")
set(UIL_APP_LICENSE "GNU Lesser General Public License v3.0 only")
set(UIL_REPOSITORY_URL "https://github.com/ifilot/uil")

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
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
endif()

qt_standard_project_setup()

file(GLOB_RECURSE FONT_AWESOME_SVG_FILES CONFIGURE_DEPENDS
    resources/fontawesome/svgs/*.svg
)

set(UIL_WINDOWS_RESOURCES)
if(WIN32)
    set(UIL_WINDOWS_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/uil.ico")
    set(UIL_WINDOWS_VERSION_TWEAK "${PROJECT_VERSION_TWEAK}")
    if(UIL_WINDOWS_VERSION_TWEAK STREQUAL "")
        set(UIL_WINDOWS_VERSION_TWEAK 0)
    endif()
    set(UIL_WINDOWS_FILE_VERSION
        "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${UIL_WINDOWS_VERSION_TWEAK}"
    )
    configure_file(
        resources/windows/uil.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/uil.rc"
        @ONLY
    )
    list(APPEND UIL_WINDOWS_RESOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/uil.rc"
    )
endif()

qt_add_executable(uil
    src/main.cpp
    src/AppController.hpp
    src/AppController.cpp
    src/pdf/PdfBackend.hpp
    src/pdf/QtPdfBackend.hpp
    src/pdf/QtPdfBackend.cpp
    src/cache/SlideCacheKey.hpp
    src/cache/SlideCache.hpp
    src/cache/SlideCache.cpp
    src/render/RenderScheduler.hpp
    src/render/RenderScheduler.cpp
    src/ui/AudienceWindow.hpp
    src/ui/AudienceWindow.cpp
    src/ui/FontAwesome.hpp
    src/ui/FontAwesome.cpp
    src/ui/PresenterWindow.hpp
    src/ui/PresenterWindow.cpp
    src/util/DocumentHash.hpp
    src/util/DocumentHash.cpp
    src/util/ImageUtil.hpp
    src/util/ImageUtil.cpp
    src/util/PdfMediaDetector.hpp
    src/util/PdfMediaDetector.cpp
    src/media/VideoFrameExtractor.hpp
    src/media/VideoFrameExtractor.cpp
    src/media/VideoFrameBuffer.hpp
    src/media/VideoFrameBuffer.cpp
    src/package/UilPackage.hpp
    src/package/UilPackage.cpp
    ${UIL_WINDOWS_RESOURCES}
)

qt_add_resources(uil "uil_resources"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/icons/uil.svg
        resources/icons/uil-white.svg
        resources/icons/uil.ico
        resources/fontawesome/LICENSE.txt
        resources/fontawesome/package.json
        ${FONT_AWESOME_SVG_FILES}
        resources/styles/vscode.qss
)

target_include_directories(uil PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(uil PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Pdf
    Qt6::Svg
    ZLIB::ZLIB
)

target_compile_definitions(uil PRIVATE
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
)

set_target_properties(uil PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(FFMPEG_FOUND)
    target_compile_definitions(uil PRIVATE UIL_HAVE_FFMPEG=1)
    target_link_libraries(uil PRIVATE PkgConfig::FFMPEG)
endif()
