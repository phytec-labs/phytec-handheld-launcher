cmake_minimum_required(VERSION 3.16)
project(phytec-launcher C)

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# LVGL include location
# Passed in by the Yocto recipe as -DLVGL_DIR=${STAGING_INCDIR}/lvgl
# ---------------------------------------------------------------------------
if(NOT DEFINED LVGL_DIR)
    set(LVGL_DIR "${CMAKE_SOURCE_DIR}/lvgl")
endif()

message(STATUS "LVGL_DIR = ${LVGL_DIR}")

# Derive parent (e.g. usr/include) so #include "lvgl/src/drivers/sdl/..."
# resolves correctly.
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_DIR}" DIRECTORY)

# ---------------------------------------------------------------------------
# Launcher sources only — LVGL (including SDL driver) is a pre-built
# shared library rebuilt with PACKAGECONFIG:append:pn-lvgl = " sdl"
# ---------------------------------------------------------------------------
set(LAUNCHER_SOURCES
    main.c
    input.c
    launcher.c
)

add_executable(phytec-launcher ${LAUNCHER_SOURCES})

target_include_directories(phytec-launcher PRIVATE
    "${CMAKE_SOURCE_DIR}"        # lv_conf.h, apps.h, launcher.h, input.h
    "${LVGL_INCLUDE_PARENT}"     # so #include "lvgl/src/drivers/sdl/..." resolves
    "${LVGL_DIR}"                # so #include "lvgl/lvgl.h" resolves
)

target_compile_definitions(phytec-launcher PRIVATE
    LV_CONF_INCLUDE_SIMPLE=1
)

target_compile_options(phytec-launcher PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g -O0>
)

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

target_include_directories(phytec-launcher PRIVATE ${SDL2_INCLUDE_DIRS})

target_link_libraries(phytec-launcher PRIVATE
    lvgl              # liblvgl.so rebuilt with SDL driver via PACKAGECONFIG
    drm               # liblvgl.so was built with DRM support
    ${SDL2_LIBRARIES}
    m
    pthread
)

target_link_directories(phytec-launcher PRIVATE
    "${CMAKE_SYSROOT}/usr/lib"
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS phytec-launcher
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
