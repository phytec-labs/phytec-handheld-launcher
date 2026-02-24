/**
 * launcher.h
 * LVGL-based game/app launcher UI for the PHYTEC AM62P.
 *
 * The launcher displays a grid of app cards. Each card can be activated
 * by touch tap or keyboard/keypad Enter. Activating a card hides the
 * launcher window, forks a child process to run the app, and restores
 * the window when the child exits.
 *
 * Display backend: SDL2 (Wayland surface managed by SDL, same as the
 * existing AM62P 3D demo). SDL_HideWindow / SDL_ShowWindow are used to
 * step aside while a child app runs.
 */

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "lvgl/lvgl.h"
#include "apps.h"

/**
 * launcher_create - Build the launcher UI on the active LVGL display.
 *
 * @param nav_group  An already-created lv_group_t that keyboard/keypad
 *                   indevs will navigate. Cards are added to this group.
 *
 * Call once after lv_init() and lv_sdl_window_create().
 * Installs a SIGCHLD handler internally for async child exit detection.
 */
void launcher_create(lv_group_t *nav_group);

#endif /* LAUNCHER_H */
