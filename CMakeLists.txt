cmake_minimum_required(VERSION 3.16)

project(lume_static_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(lume_core
    src/buffer.c
    src/config.c
    src/connection.c
    src/event_loop.c
    src/http.c
    src/log.c
    src/mime.c
    src/server.c
    src/static_file.c
)

target_include_directories(lume_core PUBLIC include)
target_compile_definitions(lume_core PUBLIC _GNU_SOURCE)
target_compile_options(lume_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(lume_server src/main.c)
target_link_libraries(lume_server PRIVATE lume_core)
target_compile_options(lume_server PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

add_executable(test_config tests/test_config.c)
target_link_libraries(test_config PRIVATE lume_core)
add_test(NAME test_config COMMAND test_config)

add_executable(test_http tests/test_http.c)
target_link_libraries(test_http PRIVATE lume_core)
add_test(NAME test_http COMMAND test_http)

add_executable(test_mime tests/test_mime.c)
target_link_libraries(test_mime PRIVATE lume_core)
add_test(NAME test_mime COMMAND test_mime)

add_executable(test_static_path tests/test_static_path.c)
target_link_libraries(test_static_path PRIVATE lume_core)
add_test(NAME test_static_path COMMAND test_static_path)
