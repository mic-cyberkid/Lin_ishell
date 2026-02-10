cmake_minimum_required(VERSION 3.18...3.28)

project(ishell_standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Main executable
add_executable(ishell-test
    src/main.cpp
    src/InteractiveShell.cpp
)

target_include_directories(ishell-test PRIVATE src)

# Linux / POSIX specific
if(UNIX)
    target_compile_definitions(ishell-test PRIVATE _GNU_SOURCE)
    target_link_libraries(ishell-test PRIVATE util)   # for forkpty
endif()

# Optional: more aggressive optimization / stripping for implant-like binary
# target_compile_options(ishell-test PRIVATE -O3 -march=native -flto)
# target_link_options(ishell-test PRIVATE -s -Wl,--gc-sections)

# Simple test target (just checks it builds and runs a few seconds)
enable_testing()
add_test(NAME SmokeTest COMMAND ishell-test)
set_tests_properties(SmokeTest PROPERTIES TIMEOUT 8)
