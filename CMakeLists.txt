cmake_minimum_required(VERSION 3.24)
project(TitanReFS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler definitions and flags
if (WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-but-set-variable)
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone TitanReFS Core Library
add_library(titanrefs_core INTERFACE)
target_include_directories(titanrefs_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test Suite Executable
add_executable(titanrefs_tests tests/test_refs.cpp)
target_link_libraries(titanrefs_tests PRIVATE titanrefs_core)

# CLI Formatting Tool
add_executable(titanrefs_format src/tools/titanrefs_format.cpp)
target_link_libraries(titanrefs_format PRIVATE titanrefs_core)

# CLI Inspection & Scrubber Tool
add_executable(titanrefs_check src/tools/titanrefs_check.cpp)
target_link_libraries(titanrefs_check PRIVATE titanrefs_core)
