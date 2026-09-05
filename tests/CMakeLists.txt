function(add_uil_unit_test target)
    add_executable(${target} ${ARGN})
    target_sources(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/util/performance_log.cpp
    )
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    uil_enable_windows_hardening(${target})
    uil_enable_sanitizers(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS unit TIMEOUT 60)
endfunction()

add_uil_unit_test(image_util_test
    image_util_test.cpp
    ${PROJECT_SOURCE_DIR}/src/util/image_util.cpp
)

add_uil_unit_test(document_hash_test
    document_hash_test.cpp
    ${PROJECT_SOURCE_DIR}/src/util/document_hash.cpp
)

add_uil_unit_test(example_presentations_test
    example_presentations_test.cpp
    ${PROJECT_SOURCE_DIR}/src/util/example_presentations.cpp
)

add_uil_unit_test(qt_pdf_backend_test
    qt_pdf_backend_test.cpp
    ${PROJECT_SOURCE_DIR}/src/pdf/qt_pdf_backend.cpp
)
target_compile_definitions(qt_pdf_backend_test PRIVATE
    UIL_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(qt_pdf_backend_test PRIVATE Qt6::Pdf)

add_uil_unit_test(slide_cache_test
    slide_cache_test.cpp
    ${PROJECT_SOURCE_DIR}/src/cache/slide_cache.cpp
)

add_uil_unit_test(spotlight_detector_test
    spotlight_detector_test.cpp
    ${PROJECT_SOURCE_DIR}/src/util/spotlight_detector.cpp
)

add_uil_unit_test(deck_overview_visibility_test
    deck_overview_visibility_test.cpp
)
target_link_libraries(deck_overview_visibility_test PRIVATE Qt6::Widgets)

add_uil_unit_test(audience_window_test
    audience_window_test.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/audience_window.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/bluecurve.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/font_awesome.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/molecule_widget.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/interactive_figure_widget.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/interactive_figure.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/harmonic_wavepacket.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/particle_in_box_basis.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_camera.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_geometry.cpp
)
target_link_libraries(audience_window_test PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Svg
)

add_uil_unit_test(app_controller_test
    app_controller_test.cpp
    ${PROJECT_SOURCE_DIR}/src/app_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/pdf/qt_pdf_backend.cpp
    ${PROJECT_SOURCE_DIR}/src/cache/slide_cache.cpp
    ${PROJECT_SOURCE_DIR}/src/render/render_scheduler.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/audience_window.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/bluecurve.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/font_awesome.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/molecule_widget.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/interactive_figure_widget.cpp
    ${PROJECT_SOURCE_DIR}/src/util/document_hash.cpp
    ${PROJECT_SOURCE_DIR}/src/util/image_util.cpp
    ${PROJECT_SOURCE_DIR}/src/util/pdf_media_detector.cpp
    ${PROJECT_SOURCE_DIR}/src/media/video_frame_extractor.cpp
    ${PROJECT_SOURCE_DIR}/src/media/video_frame_buffer.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_geometry.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_camera.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/interactive_figure.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/harmonic_wavepacket.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/particle_in_box_basis.cpp
    ${PROJECT_SOURCE_DIR}/src/package/uil_package.cpp
)
target_compile_definitions(app_controller_test PRIVATE
    UIL_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(app_controller_test PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Pdf
    Qt6::Svg
    ZLIB::ZLIB
)
if(FFMPEG_FOUND)
    target_compile_definitions(app_controller_test PRIVATE UIL_HAVE_FFMPEG=1)
    target_include_directories(app_controller_test PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_options(app_controller_test PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

add_uil_unit_test(molecule_camera_test
    molecule_camera_test.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_camera.cpp
)

add_uil_unit_test(molecule_geometry_test
    molecule_geometry_test.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_geometry.cpp
)

add_uil_unit_test(harmonic_wavepacket_test
    harmonic_wavepacket_test.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/harmonic_wavepacket.cpp
)

add_uil_unit_test(particle_in_box_basis_test
    particle_in_box_basis_test.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/particle_in_box_basis.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/interactive_figure.cpp
)

add_uil_unit_test(molecule_widget_test
    molecule_widget_test.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/font_awesome.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/molecule_widget.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_camera.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_geometry.cpp
)
target_link_libraries(molecule_widget_test PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Svg
)

add_uil_unit_test(render_scheduler_test
    render_scheduler_test.cpp
    ${PROJECT_SOURCE_DIR}/src/render/render_scheduler.cpp
    ${PROJECT_SOURCE_DIR}/src/pdf/qt_pdf_backend.cpp
)
target_link_libraries(render_scheduler_test PRIVATE Qt6::Pdf)

add_uil_unit_test(performance_log_test
    performance_log_test.cpp
    ${PROJECT_SOURCE_DIR}/src/util/launcher_readiness.cpp
)

add_uil_unit_test(uil_package_test
    uil_package_test.cpp
    ${PROJECT_SOURCE_DIR}/src/package/uil_package.cpp
)
target_link_libraries(uil_package_test PRIVATE ZLIB::ZLIB)

add_uil_unit_test(pdf_media_detector_test
    pdf_media_detector_test.cpp
    ${PROJECT_SOURCE_DIR}/src/util/pdf_media_detector.cpp
    ${PROJECT_SOURCE_DIR}/src/media/video_frame_extractor.cpp
    ${PROJECT_SOURCE_DIR}/src/molecule/molecule_geometry.cpp
    ${PROJECT_SOURCE_DIR}/src/figure/interactive_figure.cpp
)
target_compile_definitions(pdf_media_detector_test PRIVATE
    UIL_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(pdf_media_detector_test PRIVATE ZLIB::ZLIB)
if(FFMPEG_FOUND)
    target_compile_definitions(pdf_media_detector_test PRIVATE UIL_HAVE_FFMPEG=1)
    target_include_directories(pdf_media_detector_test PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_options(pdf_media_detector_test PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

add_uil_unit_test(video_frame_buffer_test
    video_frame_buffer_test.cpp
    ${PROJECT_SOURCE_DIR}/src/media/video_frame_buffer.cpp
    ${PROJECT_SOURCE_DIR}/src/media/video_frame_extractor.cpp
)
if(FFMPEG_FOUND)
    target_compile_definitions(video_frame_buffer_test PRIVATE UIL_HAVE_FFMPEG=1)
    target_include_directories(video_frame_buffer_test PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_options(video_frame_buffer_test PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

if(WIN32)
    add_executable(launcher_ready_probe WIN32 launcher_ready_probe.c)
    set_target_properties(launcher_ready_probe PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
        LINKER_LANGUAGE C
        OUTPUT_NAME "uil-viewer"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/launcher_fixture"
    )
    target_compile_definitions(launcher_ready_probe PRIVATE
        UNICODE
        _UNICODE
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    target_include_directories(launcher_ready_probe PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(launcher_ready_probe PRIVATE shell32)
    uil_enable_windows_hardening(launcher_ready_probe)
    uil_enable_sanitizers(launcher_ready_probe)
    add_dependencies(launcher_ready_probe uil)
    add_custom_command(TARGET launcher_ready_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:uil>
            $<TARGET_FILE_DIR:launcher_ready_probe>/uil.exe
    )
    add_test(
        NAME native_launcher_test
        COMMAND $<TARGET_FILE_DIR:launcher_ready_probe>/uil.exe
                "--probe-forwarded=hello world"
    )
    set_tests_properties(native_launcher_test PROPERTIES LABELS unit TIMEOUT 30)
endif()
