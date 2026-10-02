# ============================================================
#  birun · tests/CMakeLists.txt
#  Catch2 v3 based test suite + CLI integration tests.
# ============================================================

list(APPEND CMAKE_PREFIX_PATH "/usr/local" "/opt/homebrew")

find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    message(STATUS "Catch2 v3 not found — fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
        GIT_SHALLOW    TRUE
    )
    set(CATCH_BUILD_TESTING  OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS   OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Catch2)
    set(BIRUN_CATCH_CMAKE_DIR "${Catch2_DIR}")
else()
    message(STATUS "Found Catch2 v3 at: ${Catch2_DIR}")
    set(BIRUN_CATCH_CMAKE_DIR "${Catch2_DIR}")
endif()

# ---- Test executable ---------------------------------------
add_executable(birun_tests
    test_builtins.cpp
    test_cache.cpp
    test_executor.cpp
    test_extras.cpp
    test_watch.cpp
    test_integration.cpp       # ← NEW
)

target_link_libraries(birun_tests PRIVATE
    birun_core
    Catch2::Catch2WithMain
)

target_compile_options(birun_tests PRIVATE
    -Wall -Wextra
    -Wno-missing-field-initializers
)

# ---- Integration tests need the built birun binary ----
add_dependencies(birun_tests birun)
target_compile_definitions(birun_tests PRIVATE
    BIRUN_BINARY_PATH="$<TARGET_FILE:birun>"
)

# ---- Test discovery ----------------------------------------
include("${BIRUN_CATCH_CMAKE_DIR}/Catch.cmake")
catch_discover_tests(birun_tests
    TEST_PREFIX ""
    TEST_SUFFIX ""
    PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)