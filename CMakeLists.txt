cmake_minimum_required(VERSION 3.24)

project(uil VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Pdf
    PdfWidgets
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
)

target_include_directories(uil PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(uil PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Pdf
    Qt6::PdfWidgets
    ZLIB::ZLIB
)

if(FFMPEG_FOUND)
    target_compile_definitions(uil PRIVATE UIL_HAVE_FFMPEG=1)
    target_link_libraries(uil PRIVATE PkgConfig::FFMPEG)
endif()
