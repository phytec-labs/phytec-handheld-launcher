/**
 * launcher.h
 * LVGL-based game/app launcher UI for the PHYTEC AM62P.
 *
 * The launcher displays a grid of app cards. Each card can be activated
 * by touch tap or keyboard/keypad Enter. Activating a card forks a child
 * process to run the app and hides the launcher UI until the child exits.
 */

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/wayland/lv_wayland.h"
#include "apps.h"

/**
 * launcher_create - Build the launcher UI on the active LVGL display.
 *
 * @param disp       The lv_display_t* returned by lv_wayland_create_window().
 *                   Used to minimize/restore the window around app launches.
 * @param nav_group  An already-created lv_group_t that keyboard/keypad
 *                   indevs will navigate. Cards are added to this group.
 *
 * Call once after lv_init() and lv_wayland_create_window().
 * Installs a SIGCHLD handler internally for async child exit detection.
 */
void launcher_create(lv_display_t *disp, lv_group_t *nav_group);

#endif /* LAUNCHER_H */
