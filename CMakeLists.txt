# ============================================================
#  birun · CMakeLists.txt  (v1.0.0)
#  Cross-platform build:
#    - Linux / macOS / BSD: native POSIX
#    - Windows:             native Win32 (CreateProcess + Job Object)
#                           or cross-compile with MinGW-w64
# ============================================================

cmake_minimum_required(VERSION 3.16)
project(birun VERSION 1.0.0 LANGUAGES CXX)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Default build type ----
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================
#  Platform-specific configuration
# ============================================================

if(WIN32)
    # ---- Windows (native MSVC, clang-cl, or MinGW-w64) ----
    add_compile_definitions(
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    if(MSVC)
        add_compile_options(/utf-8 /W4 /permissive-)
    elseif(MINGW)
        add_compile_options(-Wall -Wextra -O2)
    endif()

    # Threads on Windows: no explicit linking needed (std::thread uses
    # native Win32 threads). But MinGW needs -static for a portable .exe.
    find_package(Threads QUIET)
else()
    # ---- POSIX (Linux / macOS / BSD) ----
    find_package(Threads REQUIRED)
endif()

# ============================================================
#  Core library (testable)
# ============================================================
add_library(birun_core STATIC src/executor.cpp)

target_include_directories(birun_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

if(NOT WIN32)
    target_link_libraries(birun_core PUBLIC Threads::Threads)
endif()

if(MSVC)
    target_compile_options(birun_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(birun_core PRIVATE -Wall -Wextra -O2)
endif()

# ============================================================
#  Executable
# ============================================================
add_executable(birun src/main.cpp)
target_link_libraries(birun PRIVATE birun_core)

if(MSVC)
    target_compile_options(birun PRIVATE /W4 /permissive-)
else()
    target_compile_options(birun PRIVATE -Wall -Wextra -O2)
endif()

target_compile_definitions(birun PRIVATE
    BIRUN_VERSION="${PROJECT_VERSION}"
)

# ---- MinGW static linking (self-contained .exe, no DLLs) ----
if(MINGW)
    target_link_options(birun PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
    target_link_libraries(birun PRIVATE
        -Wl,-Bstatic,--whole-archive
        -lwinpthread
        -Wl,--no-whole-archive
    )
endif()

# ---- MSVC static runtime (optional, comment out for dynamic CRT) ----
if(MSVC)
    set_property(TARGET birun PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

install(TARGETS birun RUNTIME DESTINATION bin)

# ============================================================
#  Tests
# ============================================================
option(BIRUN_BUILD_TESTS "Build birun tests" OFF)
if(BIRUN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()