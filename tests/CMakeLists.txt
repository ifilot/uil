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

add_uil_unit_test(slide_cache_test
    slide_cache_test.cpp
    ${PROJECT_SOURCE_DIR}/src/cache/slide_cache.cpp
)

add_uil_unit_test(deck_overview_visibility_test
    deck_overview_visibility_test.cpp
)
target_link_libraries(deck_overview_visibility_test PRIVATE Qt6::Widgets)

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
)
target_link_libraries(pdf_media_detector_test PRIVATE ZLIB::ZLIB)
if(FFMPEG_FOUND)
    target_compile_definitions(pdf_media_detector_test PRIVATE UIL_HAVE_FFMPEG=1)
    target_include_directories(pdf_media_detector_test PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_options(pdf_media_detector_test PRIVATE ${FFMPEG_CFLAGS_OTHER})
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
