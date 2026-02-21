cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# Simple build configuration for the runcam utility sources.
project(majestic_manager LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(MAJESTIC_MANAGER_SOURCES
    runcam/majestic_manager.c
    runcam/majestic_config.c
    runcam/mavlink.c
)

add_executable(majestic_manager ${MAJESTIC_MANAGER_SOURCES})
target_include_directories(majestic_manager PRIVATE runcam)

target_compile_options(majestic_manager PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-DNDEBUG>
    -Wall
    -Wextra
    -Wpedantic
)
