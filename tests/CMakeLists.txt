# ============================================================
#  birun · tests/CMakeLists.txt
#  Catch2 v3 based test suite.
#
#  Discovery strategy (in order):
#    1. Prefer a system-installed Catch2 v3 (find_package).
#    2. Fall back to FetchContent from GitHub if not found.
#
#  This makes the suite build on:
#    - Ubuntu/Debian (after cmake --install Catch2)
#    - macOS (Homebrew catch2)
#    - Windows (vcpkg or FetchContent fallback)
#    - CI runners without any pre-installed Catch2
# ============================================================

# ---- Locate Catch2 v3, or fetch it ------------------------------
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

    # Only build the library, not Catch2's own tests.
    set(CATCH_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS      OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS    OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(Catch2)
else()
    message(STATUS "Found Catch2 v3 at: ${Catch2_DIR}")

    # v3 installs its CMake helpers under ${Catch2_DIR}.
    # Add that to the module path so `include(Catch)` resolves.
    list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
endif()

# ---- Test executable -------------------------------------------
add_executable(birun_tests
    test_builtins.cpp
    test_cache.cpp
    test_executor.cpp
    test_extras.cpp
    test_watch.cpp
)

target_link_libraries(birun_tests PRIVATE
    birun_core
    Catch2::Catch2WithMain
)

target_compile_options(birun_tests PRIVATE -Wall -Wextra)

# ---- Test discovery --------------------------------------------
# `include(Catch)` brings in `catch_discover_tests`.
# Works for both find_package (after CMAKE_MODULE_PATH tweak)
# and FetchContent (which sets CMAKE_MODULE_PATH itself).
include(Catch)
catch_discover_tests(birun_tests
    TEST_PREFIX ""
    TEST_SUFFIX ""
    PROPERTIES
        TIMEOUT 30
)