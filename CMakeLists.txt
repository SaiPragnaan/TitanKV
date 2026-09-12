cmake_minimum_required(VERSION 3.20)
project(TitanKV VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(titankv
  src/wal.cpp
  src/sstable.cpp
  src/engine.cpp
  src/server.cpp
)
target_include_directories(titankv PUBLIC include)
target_compile_options(titankv PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(titankv PUBLIC pthread)

add_executable(titankvd app/main.cpp)
target_link_libraries(titankvd PRIVATE titankv)

add_executable(titankv_baseline app/baseline_main.cpp)

add_executable(titankv_tests tests/engine_test.cpp)
target_link_libraries(titankv_tests PRIVATE titankv)
enable_testing()
add_test(NAME titankv_tests COMMAND titankv_tests)

foreach(mode write_test read_test mixed_workload concurrency_test recovery_test)
  add_executable(${mode} benchmarks/benchmark.cpp)
  target_link_libraries(${mode} PRIVATE titankv)
  target_compile_definitions(${mode} PRIVATE TITANKV_BENCH_MODE="${mode}")
endforeach()
