cmake_minimum_required(VERSION 3.20)

project(ndktrace_cli VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_library(
    ndktrace-core
    src/crash_artifact_parser.cpp
    src/elf_build_id.cpp
    src/parser.cpp
    src/platform.cpp
    src/services.cpp
    src/json_output.cpp
    src/symbol_file_resolver.cpp
)

target_include_directories(
    ndktrace-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(
    ndktrace-cli
    src/main.cpp
)

target_link_libraries(ndktrace-cli PRIVATE ndktrace-core)

add_executable(
    ndktrace-cli-tests
    tests/test_parser.cpp
)

target_link_libraries(ndktrace-cli-tests PRIVATE ndktrace-core)
target_compile_definitions(
    ndktrace-cli-tests
    PRIVATE
        NDKTRACE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)

add_executable(
    ndktrace-artifact-tests
    tests/test_artifact_parser.cpp
)

target_link_libraries(ndktrace-artifact-tests PRIVATE ndktrace-core)
target_compile_definitions(
    ndktrace-artifact-tests
    PRIVATE
        NDKTRACE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)

add_executable(
    ndktrace-symbol-tests
    tests/test_symbol_file_resolver.cpp
)

target_link_libraries(ndktrace-symbol-tests PRIVATE ndktrace-core)

add_test(NAME ndktrace-cli-tests COMMAND ndktrace-cli-tests)
add_test(NAME ndktrace-artifact-tests COMMAND ndktrace-artifact-tests)
add_test(NAME ndktrace-symbol-tests COMMAND ndktrace-symbol-tests)

if(MSVC)
    target_compile_options(ndktrace-core PRIVATE /W4 /permissive-)
    target_compile_options(ndktrace-cli PRIVATE /W4 /permissive-)
    target_compile_options(ndktrace-cli-tests PRIVATE /W4 /permissive-)
    target_compile_options(ndktrace-artifact-tests PRIVATE /W4 /permissive-)
    target_compile_options(ndktrace-symbol-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(ndktrace-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ndktrace-cli PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ndktrace-cli-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ndktrace-artifact-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ndktrace-symbol-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
