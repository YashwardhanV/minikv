cmake_minimum_required(VERSION 3.16)

project(minikv VERSION 1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Debug build for single-config generators when nothing was given,
# so a bare `cmake -S . -B build` still gets sanitizers and debug info.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MINIKV_WERROR "Treat compiler warnings as errors (CI-style builds)" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_minikv_sanitizers_default ON)
else()
    set(_minikv_sanitizers_default OFF)
endif()
option(MINIKV_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer"
       ${_minikv_sanitizers_default})

option(MINIKV_BUILD_TESTS "Build the unit and end-to-end tests" ON)

# Sanitizers are applied globally (including GoogleTest) so every object in the
# test binary is instrumented the same way.
if(MINIKV_SANITIZERS)
    message(STATUS "MiniKV: sanitizers enabled (address, undefined)")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# Warning flags are applied only to our own targets, not to third-party code.
add_library(minikv_warnings INTERFACE)
target_compile_options(minikv_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
if(MINIKV_WERROR)
    target_compile_options(minikv_warnings INTERFACE -Werror)
endif()

# ---------------------------------------------------------------------------
# Core library: everything except main.cpp
# ---------------------------------------------------------------------------
add_library(minikv_core STATIC
    src/commands.cpp
    src/glob.cpp
    src/persistence.cpp
    src/store.cpp
    src/tokenizer.cpp
    src/undo.cpp
)
target_include_directories(minikv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(minikv_core PRIVATE minikv_warnings)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(minikv src/main.cpp)
target_link_libraries(minikv PRIVATE minikv_core minikv_warnings)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MINIKV_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Treat GoogleTest headers as system headers so our strict warning flags
    # do not report issues inside third-party code.
    foreach(_gtest_target gtest gtest_main)
        get_target_property(_gtest_includes ${_gtest_target} INTERFACE_INCLUDE_DIRECTORIES)
        set_target_properties(${_gtest_target} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_gtest_includes}")
    endforeach()

    add_executable(minikv_tests
        tests/test_commands.cpp
        tests/test_glob.cpp
        tests/test_persistence.cpp
        tests/test_store.cpp
        tests/test_tokenizer.cpp
        tests/test_ttl.cpp
        tests/test_undo.cpp
    )
    target_link_libraries(minikv_tests PRIVATE minikv_core minikv_warnings GTest::gtest_main)

    include(GoogleTest)
    # halt_on_error makes any UBSan report fail the test instead of just printing.
    gtest_discover_tests(minikv_tests
        PROPERTIES
            ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
            TIMEOUT 30
    )

    add_test(NAME e2e_session1
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/run_e2e.sh
                $<TARGET_FILE:minikv> ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e)
    set_tests_properties(e2e_session1 PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
        TIMEOUT 30)
endif()
