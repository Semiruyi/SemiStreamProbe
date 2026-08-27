cmake_minimum_required(VERSION 3.23)

project(SemiStreamProbe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SSP_BUILD_TESTS "Build SemiStreamProbe tests" ON)

function(ssp_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    endif()
endfunction()

function(ssp_use_portable_mingw_runtime target)
    if(MINGW)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endfunction()

add_library(semi_stream_probe_core STATIC
    src/core/access_unit.cpp
    src/core/annex_b.cpp
    src/core/bit_reader.cpp
    src/core/diagnostic.cpp
    src/core/gop.cpp
    src/core/h264_rtp.cpp
    src/core/h264_rtp_stream_analyzer.cpp
    src/core/h264_stream_model.cpp
    src/core/h264_syntax.cpp
    src/core/nal.cpp
    src/core/parameter_sets.cpp
    src/core/parse_error.cpp
    src/core/rbsp.cpp
    src/core/rtp.cpp
    src/core/rtp_stream_analyzer.cpp
    src/core/slice.cpp
)

target_include_directories(semi_stream_probe_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(semi_stream_probe_core PUBLIC cxx_std_23)
ssp_enable_warnings(semi_stream_probe_core)

add_library(semi_stream_probe_infrastructure STATIC
    src/infrastructure/udp_receiver.cpp
)

target_include_directories(semi_stream_probe_infrastructure
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(semi_stream_probe_infrastructure
    PUBLIC
        semi_stream_probe_core
)

if(WIN32)
    target_link_libraries(semi_stream_probe_infrastructure PUBLIC ws2_32)
endif()

target_compile_features(semi_stream_probe_infrastructure PUBLIC cxx_std_23)
ssp_enable_warnings(semi_stream_probe_infrastructure)

add_library(semi_stream_probe_application STATIC
    src/application/inspect.cpp
    src/application/listen.cpp
    src/application/report.cpp
    src/application/rtp_analysis.cpp
)

target_include_directories(semi_stream_probe_application
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(semi_stream_probe_application
    PUBLIC
        semi_stream_probe_core
        semi_stream_probe_infrastructure
)

target_compile_features(semi_stream_probe_application PUBLIC cxx_std_23)
ssp_enable_warnings(semi_stream_probe_application)

add_executable(semistreamprobe
    src/cli/main.cpp
)

target_link_libraries(semistreamprobe
    PRIVATE
        semi_stream_probe_application
)

target_compile_features(semistreamprobe PRIVATE cxx_std_23)
ssp_enable_warnings(semistreamprobe)
ssp_use_portable_mingw_runtime(semistreamprobe)

include(CTest)

if(BUILD_TESTING AND SSP_BUILD_TESTS)
    add_executable(semi_stream_probe_smoke_tests
        tests/smoke_test.cpp
    )

    target_link_libraries(semi_stream_probe_smoke_tests
        PRIVATE
            semi_stream_probe_application
    )

    target_compile_features(semi_stream_probe_smoke_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_smoke_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_smoke_tests)

    add_test(
        NAME semi_stream_probe_smoke_tests
        COMMAND semi_stream_probe_smoke_tests
    )

    add_executable(semi_stream_probe_annex_b_tests
        tests/annex_b_test.cpp
    )
    target_link_libraries(semi_stream_probe_annex_b_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_annex_b_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_annex_b_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_annex_b_tests)
    add_test(
        NAME semi_stream_probe_annex_b_tests
        COMMAND semi_stream_probe_annex_b_tests
    )

    add_executable(semi_stream_probe_nal_tests
        tests/nal_test.cpp
    )
    target_link_libraries(semi_stream_probe_nal_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_nal_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_nal_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_nal_tests)
    add_test(
        NAME semi_stream_probe_nal_tests
        COMMAND semi_stream_probe_nal_tests
    )

    add_executable(semi_stream_probe_rbsp_tests
        tests/rbsp_test.cpp
    )
    target_link_libraries(semi_stream_probe_rbsp_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_rbsp_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_rbsp_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_rbsp_tests)
    add_test(
        NAME semi_stream_probe_rbsp_tests
        COMMAND semi_stream_probe_rbsp_tests
    )

    add_executable(semi_stream_probe_bit_reader_tests
        tests/bit_reader_test.cpp
    )
    target_link_libraries(semi_stream_probe_bit_reader_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_bit_reader_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_bit_reader_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_bit_reader_tests)
    add_test(
        NAME semi_stream_probe_bit_reader_tests
        COMMAND semi_stream_probe_bit_reader_tests
    )

    add_executable(semi_stream_probe_h264_syntax_tests
        tests/h264_syntax_test.cpp
    )
    target_link_libraries(semi_stream_probe_h264_syntax_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_h264_syntax_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_h264_syntax_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_h264_syntax_tests)
    add_test(
        NAME semi_stream_probe_h264_syntax_tests
        COMMAND semi_stream_probe_h264_syntax_tests
    )

    add_executable(semi_stream_probe_pps_tests
        tests/pps_test.cpp
    )
    target_link_libraries(semi_stream_probe_pps_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_pps_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_pps_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_pps_tests)
    add_test(
        NAME semi_stream_probe_pps_tests
        COMMAND semi_stream_probe_pps_tests
    )

    add_executable(semi_stream_probe_slice_tests
        tests/slice_test.cpp
    )
    target_link_libraries(semi_stream_probe_slice_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_slice_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_slice_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_slice_tests)
    add_test(
        NAME semi_stream_probe_slice_tests
        COMMAND semi_stream_probe_slice_tests
    )

    add_executable(semi_stream_probe_access_unit_tests
        tests/access_unit_test.cpp
    )
    target_link_libraries(semi_stream_probe_access_unit_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_access_unit_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_access_unit_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_access_unit_tests)
    add_test(
        NAME semi_stream_probe_access_unit_tests
        COMMAND semi_stream_probe_access_unit_tests
    )

    add_executable(semi_stream_probe_gop_tests
        tests/gop_test.cpp
    )
    target_link_libraries(semi_stream_probe_gop_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_gop_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_gop_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_gop_tests)
    add_test(
        NAME semi_stream_probe_gop_tests
        COMMAND semi_stream_probe_gop_tests
    )

    add_executable(semi_stream_probe_rtp_tests
        tests/rtp_test.cpp
    )
    target_link_libraries(semi_stream_probe_rtp_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_rtp_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_rtp_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_rtp_tests)
    add_test(
        NAME semi_stream_probe_rtp_tests
        COMMAND semi_stream_probe_rtp_tests
    )

    add_executable(semi_stream_probe_diagnostic_tests
        tests/diagnostic_test.cpp
    )
    target_link_libraries(semi_stream_probe_diagnostic_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_diagnostic_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_diagnostic_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_diagnostic_tests)
    add_test(
        NAME semi_stream_probe_diagnostic_tests
        COMMAND semi_stream_probe_diagnostic_tests
    )

    add_executable(semi_stream_probe_rtp_stream_analyzer_tests
        tests/rtp_stream_analyzer_test.cpp
    )
    target_link_libraries(semi_stream_probe_rtp_stream_analyzer_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_rtp_stream_analyzer_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_rtp_stream_analyzer_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_rtp_stream_analyzer_tests)
    add_test(
        NAME semi_stream_probe_rtp_stream_analyzer_tests
        COMMAND semi_stream_probe_rtp_stream_analyzer_tests
    )

    add_executable(semi_stream_probe_h264_rtp_tests
        tests/h264_rtp_test.cpp
    )
    target_link_libraries(semi_stream_probe_h264_rtp_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_h264_rtp_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_h264_rtp_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_h264_rtp_tests)
    add_test(
        NAME semi_stream_probe_h264_rtp_tests
        COMMAND semi_stream_probe_h264_rtp_tests
    )

    add_executable(semi_stream_probe_h264_rtp_stream_analyzer_tests
        tests/h264_rtp_stream_analyzer_test.cpp
    )
    target_link_libraries(semi_stream_probe_h264_rtp_stream_analyzer_tests
        PRIVATE
            semi_stream_probe_core
    )
    target_compile_features(semi_stream_probe_h264_rtp_stream_analyzer_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_h264_rtp_stream_analyzer_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_h264_rtp_stream_analyzer_tests)
    add_test(
        NAME semi_stream_probe_h264_rtp_stream_analyzer_tests
        COMMAND semi_stream_probe_h264_rtp_stream_analyzer_tests
    )

    add_executable(semi_stream_probe_application_tests
        tests/application_test.cpp
    )
    target_link_libraries(semi_stream_probe_application_tests
        PRIVATE
            semi_stream_probe_application
    )
    target_compile_features(semi_stream_probe_application_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_application_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_application_tests)
    add_test(
        NAME semi_stream_probe_application_tests
        COMMAND semi_stream_probe_application_tests
    )

    add_executable(semi_stream_probe_report_tests
        tests/report_test.cpp
    )
    target_link_libraries(semi_stream_probe_report_tests
        PRIVATE
            semi_stream_probe_application
    )
    target_compile_features(semi_stream_probe_report_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_report_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_report_tests)
    add_test(
        NAME semi_stream_probe_report_tests
        COMMAND semi_stream_probe_report_tests
    )

    add_executable(semi_stream_probe_rtp_analysis_tests
        tests/rtp_analysis_test.cpp
    )
    target_link_libraries(semi_stream_probe_rtp_analysis_tests
        PRIVATE
            semi_stream_probe_application
    )
    target_compile_features(semi_stream_probe_rtp_analysis_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_rtp_analysis_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_rtp_analysis_tests)
    add_test(
        NAME semi_stream_probe_rtp_analysis_tests
        COMMAND semi_stream_probe_rtp_analysis_tests
    )

    add_executable(semi_stream_probe_udp_listener_tests
        tests/udp_listener_test.cpp
    )
    target_link_libraries(semi_stream_probe_udp_listener_tests
        PRIVATE
            semi_stream_probe_application
    )
    target_compile_features(semi_stream_probe_udp_listener_tests PRIVATE cxx_std_23)
    ssp_enable_warnings(semi_stream_probe_udp_listener_tests)
    ssp_use_portable_mingw_runtime(semi_stream_probe_udp_listener_tests)
    add_test(
        NAME semi_stream_probe_udp_listener_tests
        COMMAND semi_stream_probe_udp_listener_tests
    )

    add_test(
        NAME semistreamprobe_version
        COMMAND semistreamprobe --version
    )
endif()

