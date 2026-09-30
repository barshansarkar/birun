find_package(Catch2 3 REQUIRED)

add_executable(birun_tests
    test_executor.cpp
    test_builtins.cpp
    test_watch.cpp
    test_extras.cpp
    test_cache.cpp
)
target_link_libraries(birun_tests PRIVATE
    birun_core
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(birun_tests)