/**
 * input.c
 * Input setup for the PHYTEC launcher running under a Wayland compositor.
 *
 * The LVGL Wayland driver automatically registers four input devices when
 * a window is created:
 *   - Keyboard   (LV_INDEV_TYPE_KEYPAD)   via lv_wayland_get_indev_keyboard()
 *   - Touchscreen(LV_INDEV_TYPE_POINTER)  via lv_wayland_get_indev_touchscreen()
 *   - Pointer    (LV_INDEV_TYPE_POINTER)  via lv_wayland_get_indev_pointer()
 *   - Scroll     (LV_INDEV_TYPE_ENCODER)  via lv_wayland_get_indev_pointeraxis()
 *
 * We only need to assign the keyboard indev to the navigation group so
 * that LVGL focus movement (arrow keys, Enter) works on the launcher cards.
 * Touch works automatically with no group assignment needed.
 *
 * Key mappings handled internally by the LVGL Wayland driver:
 *   Arrow keys    → LV_KEY_UP / DOWN / LEFT / RIGHT
 *   Enter         → LV_KEY_ENTER
 *   Escape        → LV_KEY_ESC
 *   Tab           → LV_KEY_NEXT
 *
 * TODO (MSPM0 I2C joystick):
 *   Implement input_joystick_init() here. The joystick should be registered
 *   as a new LV_INDEV_TYPE_KEYPAD indev using lv_indev_create(), with a
 *   read callback that consults a mutex-protected mspm0_state_t struct
 *   updated by your I2C polling thread.
 *
 *   D-pad bits  → LV_KEY_UP / DOWN / LEFT / RIGHT
 *   A button    → LV_KEY_ENTER
 *   B button    → LV_KEY_ESC
 *
 *   Call lv_indev_set_group(joystick_indev, nav_group) so the joystick
 *   navigates the launcher alongside the Wayland keyboard.
 */

#include "input.h"
#include <stdio.h>

void input_setup(lv_display_t *disp, lv_group_t *nav_group)
{
    if (!disp) {
        fprintf(stderr, "[input] input_setup() called with NULL display\n");
        return;
    }

    /*
     * Retrieve the keyboard indev registered by the Wayland driver and
     * assign it to the nav group. This enables arrow-key and Enter
     * navigation between the launcher's app cards.
     */
    lv_indev_t *kb = lv_wayland_get_indev_keyboard(disp);
    if (kb && nav_group) {
        lv_indev_set_group(kb, nav_group);
        printf("[input] Wayland keyboard indev assigned to nav group\n");
    } else {
        fprintf(stderr, "[input] WARNING: Wayland keyboard indev not available\n");
    }

    /*
     * Touch and pointer indevs are registered by the Wayland driver and
     * work automatically as LV_INDEV_TYPE_POINTER — no group assignment needed.
     * Cards respond to tap events directly via LV_EVENT_CLICKED.
     */
    lv_indev_t *ts = lv_wayland_get_indev_touchscreen(disp);
    if (ts) {
        printf("[input] Wayland touchscreen indev active\n");
    } else {
        fprintf(stderr, "[input] WARNING: Wayland touchscreen indev not available "
                        "(touch may not be wired through Weston on this build)\n");
    }

    /*
     * TODO (MSPM0 I2C joystick):
     * Call input_joystick_init() here after the Wayland indevs are set up.
     * Example:
     *   if (!input_joystick_init("/dev/i2c-1", 0x42, nav_group)) {
     *       fprintf(stderr, "[input] WARNING: MSPM0 joystick unavailable\n");
     *   }
     */
}
