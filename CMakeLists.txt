cmake_minimum_required(VERSION 3.16)
project(birun VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---- core library (testable) ----
add_library(birun_core STATIC src/executor.cpp)
target_include_directories(birun_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(birun_core PUBLIC Threads::Threads)
target_compile_options(birun_core PRIVATE -Wall -Wextra -O2)

# ---- executable ----
add_executable(birun src/main.cpp)
target_link_libraries(birun PRIVATE birun_core)
target_compile_options(birun PRIVATE -Wall -Wextra -O2)

install(TARGETS birun RUNTIME DESTINATION bin)

# ---- tests ----
option(BIRUN_BUILD_TESTS "Build birun tests" ON)
if(BIRUN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()