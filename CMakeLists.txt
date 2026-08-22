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
    src/core/annex_b.cpp
    src/core/h264_syntax.cpp
    src/core/nal.cpp
    src/core/parse_error.cpp
    src/core/rbsp.cpp
)

target_include_directories(semi_stream_probe_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(semi_stream_probe_core PUBLIC cxx_std_23)
ssp_enable_warnings(semi_stream_probe_core)

add_library(semi_stream_probe_application STATIC
    src/application/inspect.cpp
)

target_include_directories(semi_stream_probe_application
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(semi_stream_probe_application
    PUBLIC
        semi_stream_probe_core
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
endif()

