/**
 * lv_conf.h
 * LVGL v9.1 configuration for PHYTEC AM62P launcher
 * Display backend: SDL2 (runs as a Wayland client via SDL's Wayland backend)
 */

#if 1

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
#define LV_MEM_SIZE (512U * 1024U)

/*====================
   DISPLAY
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 16  /* ~60 fps */

/*====================
   INPUT
 *====================*/
#define LV_INDEV_DEF_READ_PERIOD 16

/*====================
   SDL2 DRIVER
 *====================*/
#define LV_USE_SDL 1

/*
 * SDL renders into a window on the Wayland compositor — exactly the same
 * way the existing 3D demo works. SDL's Wayland backend is selected
 * automatically at runtime via the SDL_VIDEODRIVER=wayland environment
 * variable (set in the systemd unit).
 *
 * Touch events arrive as SDL finger events and are handled by the SDL
 * driver's built-in touch indev. No direct evdev access needed.
 */

/* Disable other display backends */
#define LV_USE_LINUX_FBDEV  0
#define LV_USE_WAYLAND      0

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/*====================
   FEATURES
 *====================*/
#define LV_USE_ANIMATION  1
#define LV_USE_SHADOW     1

/* Widgets */
#define LV_USE_BTN    1
#define LV_USE_LABEL  1
#define LV_USE_IMG    1
#define LV_USE_MSGBOX 1

/* Group support — required for keyboard navigation */
#define LV_USE_GROUP  1

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   1

/*====================
   ASSERT
 *====================*/
#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1

#endif /* LV_CONF_H */
#endif
