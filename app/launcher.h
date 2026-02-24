/**
 * launcher.h
 * LVGL launcher UI: card grid and async app launching (SDL2 backend).
 *
 * Window visibility while a child app is running is managed via SDL2
 * directly (SDL_MinimizeWindow / SDL_ShowWindow). The SDL_Window handle
 * is retrieved internally via SDL_GetWindowFromID(1) so callers don't
 * need to pass it in.
 *
 * TODO (custom icons):
 *   Replace LV_SYMBOL_* with per-app PNG icons loaded via lv_image_set_src().
 *
 * TODO (status bar):
 *   Add battery, Wi-Fi, and clock widgets to a top bar container.
 *
 * TODO (runtime app list):
 *   Replace the compiled-in apps.h list with a JSON/INI parser.
 */

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "lvgl/lvgl.h"

/**
 * launcher_create - Build the launcher UI and register the SIGCHLD handler.
 *
 * Must be called after lv_sdl_window_create() and input_setup().
 *
 * @param nav_group  The LVGL group for keyboard/gamepad focus navigation.
 */
void launcher_create(lv_group_t *nav_group);

#endif /* LAUNCHER_H */
