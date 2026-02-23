cmake_minimum_required(VERSION 3.16)
project(phytec-launcher C)

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# LVGL source location
#
# In LVGL v9 the Wayland driver lives inside the main LVGL repo at
# src/drivers/wayland/ — lv_drivers is no longer needed for Wayland.
#
# In the Yocto recipe we pass -DLVGL_DIR pointing at the installed sources.
# For local development, clone as a submodule:
#   git submodule add https://github.com/lvgl/lvgl.git lvgl
# ---------------------------------------------------------------------------
if(NOT DEFINED LVGL_DIR)
    set(LVGL_DIR "${CMAKE_SOURCE_DIR}/lvgl")
endif()

message(STATUS "LVGL_DIR = ${LVGL_DIR}")

# ---------------------------------------------------------------------------
# Wayland protocol file generation
#
# The LVGL Wayland driver requires C source/header files generated from the
# XDG shell Wayland protocol XML using wayland-scanner. These must be
# regenerated each build to match the exact wayland-client library version
# installed on the target system.
#
# wayland-scanner is a host tool — in Yocto cross-builds it is provided by
# the STAGING_DIR_NATIVE and referenced via the wayland-native recipe.
# ---------------------------------------------------------------------------
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
message(STATUS "wayland-scanner = ${WAYLAND_SCANNER}")

# Locate the XDG shell protocol XML.
# In Yocto the sysroot path is set via -DWAYLAND_PROTOCOLS_DIR.
if(NOT DEFINED WAYLAND_PROTOCOLS_DIR)
    set(WAYLAND_PROTOCOLS_DIR "/usr/share/wayland-protocols")
endif()

set(XDG_SHELL_XML
    "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

if(NOT EXISTS "${XDG_SHELL_XML}")
    message(FATAL_ERROR
        "XDG shell protocol XML not found at ${XDG_SHELL_XML}. "
        "Install wayland-protocols or set -DWAYLAND_PROTOCOLS_DIR=<path>.")
endif()

set(XDG_SHELL_HEADER "${CMAKE_BINARY_DIR}/wayland_xdg_shell.h")
set(XDG_SHELL_SOURCE "${CMAKE_BINARY_DIR}/wayland_xdg_shell.c")

add_custom_command(
    OUTPUT  "${XDG_SHELL_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" client-header
            "${XDG_SHELL_XML}" "${XDG_SHELL_HEADER}"
    DEPENDS "${XDG_SHELL_XML}"
    COMMENT "Generating wayland_xdg_shell.h"
)

add_custom_command(
    OUTPUT  "${XDG_SHELL_SOURCE}"
    COMMAND "${WAYLAND_SCANNER}" private-code
            "${XDG_SHELL_XML}" "${XDG_SHELL_SOURCE}"
    DEPENDS "${XDG_SHELL_XML}"
    COMMENT "Generating wayland_xdg_shell.c"
)

# ---------------------------------------------------------------------------
# Collect LVGL sources
# Exclude examples, demos, and tests to keep the build lean.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE LVGL_SOURCES
    "${LVGL_DIR}/src/*.c"
)

# Exclude test and demo directories
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/examples/.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/demos/.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/tests/.*")

# ---------------------------------------------------------------------------
# Launcher sources
# ---------------------------------------------------------------------------
set(LAUNCHER_SOURCES
    main.c
    input.c
    launcher.c
    "${XDG_SHELL_SOURCE}"   # Generated Wayland XDG shell protocol glue
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(phytec-launcher ${LAUNCHER_SOURCES} ${LVGL_SOURCES})

# Generated header must exist before compilation starts
add_custom_target(wayland_protocols DEPENDS "${XDG_SHELL_HEADER}")
add_dependencies(phytec-launcher wayland_protocols)

get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_DIR}" DIRECTORY)

target_include_directories(phytec-launcher PRIVATE
    "${CMAKE_SOURCE_DIR}"          # lv_conf.h
    "${LVGL_INCLUDE_PARENT}"       # so #include "lvgl/src/..." resolves correctly
    "${LVGL_DIR}"                  # so #include "lvgl.h" also works (flat includes)
    "${CMAKE_BINARY_DIR}"          # generated wayland_xdg_shell.h
)

target_compile_definitions(phytec-launcher PRIVATE
    LV_CONF_INCLUDE_SIMPLE=1
)

# ---------------------------------------------------------------------------
# Find Wayland and xkbcommon via pkg-config
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(XKBCOMMON      REQUIRED xkbcommon)

target_include_directories(phytec-launcher PRIVATE
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
)

target_link_libraries(phytec-launcher PRIVATE
    ${WAYLAND_CLIENT_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    m
    pthread
)

target_compile_options(phytec-launcher PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g -O0>
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS phytec-launcher
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES apps.h
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/phytec-launcher
    RENAME apps.h.example
)
