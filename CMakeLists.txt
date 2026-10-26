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
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-packed-not-aligned)
    endif()
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

# Background Service Daemon
add_executable(titanrefs_svc src/service/titanrefs_svc.cpp)
target_link_libraries(titanrefs_svc PRIVATE titanrefs_core)
if (WIN32)
    target_link_libraries(titanrefs_svc PRIVATE advapi32)
endif()

if (WIN32)
    # WinFsp Mount Driver Bridge
    add_executable(titanrefs_mount src/mount/winfsp_mount.cpp)
    target_link_libraries(titanrefs_mount PRIVATE titanrefs_core)

    # Standalone Installer Packager
    add_executable(titanrefs_packager src/tools/titanrefs_packager.cpp)
    target_link_libraries(titanrefs_packager PRIVATE titanrefs_core)

    # Standalone 1-Click GUI Installer
    add_executable(titanrefs_setup WIN32 src/installer/titanrefs_setup.cpp)
    target_link_libraries(titanrefs_setup PRIVATE titanrefs_core user32 gdi32 gdiplus comctl32 dwmapi uxtheme ole32 oleaut32 shell32 advapi32)
    add_dependencies(titanrefs_setup titanrefs_format titanrefs_check titanrefs_mount titanrefs_svc titanrefs_packager)

    set(WINFSP_MSI "${CMAKE_CURRENT_SOURCE_DIR}/redist/winfsp-2.1.25156.msi")
    set(PACKAGER_EXTRA_ARGS "")
    if (EXISTS "${WINFSP_MSI}")
        set(PACKAGER_EXTRA_ARGS "${WINFSP_MSI}")
    endif()

    add_custom_command(
        TARGET titanrefs_setup POST_BUILD
        COMMAND titanrefs_packager $<TARGET_FILE:titanrefs_setup>
                $<TARGET_FILE:titanrefs_format>
                $<TARGET_FILE:titanrefs_check>
                $<TARGET_FILE:titanrefs_mount>
                $<TARGET_FILE:titanrefs_svc>
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/titanrefs_hero.jpg
                ${PACKAGER_EXTRA_ARGS}
        COMMENT "Embedding all binaries, WinFsp MSI, and assets into standalone titanrefs_setup.exe..."
    )
endif()

# ============================================================================
# CPack Packaging Configuration
# ============================================================================
set(CPACK_PACKAGE_NAME "TitanReFS")
set(CPACK_PACKAGE_VENDOR "MicaNT-Kernel")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Clean-Room ReFS v3.12 Storage Engine and WinFsp Mount Bridge for Windows NT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

install(TARGETS titanrefs_tests titanrefs_format titanrefs_check titanrefs_svc RUNTIME DESTINATION bin)
if (WIN32)
    install(TARGETS titanrefs_mount titanrefs_setup RUNTIME DESTINATION bin)
    install(DIRECTORY assets/ DESTINATION assets)
    install(DIRECTORY redist/ DESTINATION redist OPTIONAL)
endif()

set(CPACK_GENERATOR "ZIP")
include(CPack)

# CTest registration
enable_testing()
add_test(NAME TitanRefsCoreTests COMMAND titanrefs_tests)
