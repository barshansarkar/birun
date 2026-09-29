# CMakeLists.txt update
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(birun VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(birun src/main.cpp)

target_include_directories(birun PRIVATE vendor)
target_link_libraries(birun PRIVATE Threads::Threads)

target_compile_options(birun PRIVATE -Wall -Wextra -O2)

install(TARGETS birun RUNTIME DESTINATION bin)
EOF

./build.sh