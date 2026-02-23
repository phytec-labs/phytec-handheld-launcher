/**
 * lv_conf.h
 * LVGL v9 configuration for PHYTEC AM62P launcher
 * Display: 1280x720 via Wayland (Weston compositor)
 */

#if 1 /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 32

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (512U * 1024U) /* 512 KB */

/*====================
   DISPLAY
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 16  /* ~60 fps */

/*====================
   INPUT
 *====================*/
#define LV_INDEV_DEF_READ_PERIOD 16

/*====================
   WAYLAND DRIVER
 *====================*/
#define LV_USE_WAYLAND 1

/*
 * Use SHM (shared memory) rendering — compatible with all Wayland compositors
 * including Weston without requiring EGL/GPU setup.
 *
 * Set LV_WAYLAND_USE_DMABUF 1 if your BSP supports DMA-BUF and you want
 * GPU-accelerated rendering. Requires additional wayland-scanner protocol
 * generation in CMakeLists.txt (linux-dmabuf-v1.xml).
 */
#define LV_WAYLAND_USE_DMABUF 0

/*
 * Window decorations (title bar, borders) — not required for Weston kiosk mode
 * since we run fullscreen. Only needed for GNOME/Mutter compositors.
 */
#define LV_WAYLAND_WINDOW_DECORATIONS 0

/* Disable fbdev — we are using Wayland exclusively */
#define LV_USE_LINUX_FBDEV 0

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/*====================
   FEATURE CONFIGURATION
 *====================*/
#define LV_USE_ANIMATION 1
#define LV_USE_SHADOW    1

/* Widget set */
#define LV_USE_BTN    1
#define LV_USE_LABEL  1
#define LV_USE_IMG    1
#define LV_USE_MSGBOX 1

/* Group support (required for keyboard/keypad navigation) */
#define LV_USE_GROUP  1

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/*====================
   ASSERT
 *====================*/
#define LV_USE_ASSERT_NULL    1
#define LV_USE_ASSERT_MALLOC  1

#endif /* LV_CONF_H */
#endif /* End of "1" enable */
