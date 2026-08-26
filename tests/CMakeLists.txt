function(add_uil_unit_test target)
    add_executable(${target} ${ARGN})
    target_sources(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/util/performance_log.cpp
    )
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
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

add_uil_unit_test(performance_log_test
    performance_log_test.cpp
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
