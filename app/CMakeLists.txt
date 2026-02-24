cmake_minimum_required(VERSION 3.16)
project(phytec-launcher C)

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# LVGL source location
# Passed in by the Yocto recipe as -DLVGL_DIR=${STAGING_INCDIR}/lvgl
# For local development clone lvgl as a submodule at ./lvgl
# ---------------------------------------------------------------------------
if(NOT DEFINED LVGL_DIR)
    set(LVGL_DIR "${CMAKE_SOURCE_DIR}/lvgl")
endif()

message(STATUS "LVGL_DIR = ${LVGL_DIR}")

# Derive the parent directory (e.g. usr/include) so that
# #include "lvgl/src/drivers/sdl/lv_sdl_window.h" resolves correctly.
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_DIR}" DIRECTORY)

# ---------------------------------------------------------------------------
# Collect LVGL sources — exclude examples, demos, tests
# ---------------------------------------------------------------------------
file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/examples/.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/demos/.*")
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/tests/.*")

# ---------------------------------------------------------------------------
# Launcher sources — no wayland-scanner generated files needed
# ---------------------------------------------------------------------------
set(LAUNCHER_SOURCES
    main.c
    input.c
    launcher.c
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(phytec-launcher ${LAUNCHER_SOURCES} ${LVGL_SOURCES})

target_include_directories(phytec-launcher PRIVATE
    "${CMAKE_SOURCE_DIR}"        # lv_conf.h
    "${LVGL_INCLUDE_PARENT}"     # so #include "lvgl/src/drivers/sdl/..." resolves
    "${LVGL_DIR}"                # so #include "lvgl/lvgl.h" resolves
)

target_compile_definitions(phytec-launcher PRIVATE
    LV_CONF_INCLUDE_SIMPLE=1
)

# ---------------------------------------------------------------------------
# SDL2 via pkg-config
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

target_include_directories(phytec-launcher PRIVATE ${SDL2_INCLUDE_DIRS})

target_link_libraries(phytec-launcher PRIVATE
    ${SDL2_LIBRARIES}
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
