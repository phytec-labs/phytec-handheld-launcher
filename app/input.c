/**
 * input.c
 * Input setup for the PHYTEC launcher using the LVGL SDL2 driver.
 *
 * SDL2 indev mapping:
 *   lv_sdl_mouse_create()      → LV_INDEV_TYPE_POINTER (touch + mouse)
 *   lv_sdl_mousewheel_create() → LV_INDEV_TYPE_ENCODER (scroll)
 *   lv_sdl_keyboard_create()   → LV_INDEV_TYPE_KEYPAD  (keyboard nav)
 *
 * SDL translates Wayland finger touch events into SDL_FINGERDOWN/MOTION/UP
 * events, which the SDL mouse indev handles as pointer events. This is the
 * same path used by the existing 3D demo on this BSP.
 *
 * Keyboard key mapping is handled inside the LVGL SDL driver:
 *   Arrow keys → LV_KEY_UP / DOWN / LEFT / RIGHT
 *   Enter      → LV_KEY_ENTER
 *   Escape     → LV_KEY_ESC
 *   Tab        → LV_KEY_NEXT
 *
 * TODO (MSPM0 I2C joystick):
 *   Add a fourth indev here using lv_indev_create(LV_INDEV_TYPE_KEYPAD).
 *   Implement a read callback that reads from a mutex-protected mspm0_state_t
 *   struct updated by your I2C polling thread. Assign it to nav_group with
 *   lv_indev_set_group(). See input.h for the suggested state struct layout.
 */

#include "input.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mousewheel.h"
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#include <stdio.h>

bool input_setup(lv_group_t *nav_group)
{
    /* Touch / mouse pointer indev */
    lv_indev_t *mouse = lv_sdl_mouse_create();
    if (mouse) {
        printf("[input] SDL mouse/touch indev registered\n");
    } else {
        fprintf(stderr, "[input] WARNING: SDL mouse/touch indev unavailable\n");
    }

    /* Scroll wheel encoder indev */
    lv_indev_t *mousewheel = lv_sdl_mousewheel_create();
    if (mousewheel) {
        printf("[input] SDL mousewheel indev registered\n");
    }

    /* Keyboard keypad indev — assign to nav_group for focus navigation */
    lv_indev_t *kb = lv_sdl_keyboard_create();
    if (kb && nav_group) {
        lv_indev_set_group(kb, nav_group);
        printf("[input] SDL keyboard indev registered and assigned to nav group\n");
    } else {
        fprintf(stderr, "[input] WARNING: SDL keyboard indev unavailable\n");
    }

    /*
     * TODO (MSPM0 I2C joystick):
     * Call input_joystick_init() here, e.g.:
     *   if (!input_joystick_init("/dev/i2c-1", 0x42, nav_group)) {
     *       fprintf(stderr, "[input] WARNING: MSPM0 joystick unavailable\n");
     *   }
     */

    return (mouse != NULL);
}
