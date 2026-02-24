/**
 * input.h
 * Input setup for the PHYTEC launcher using the LVGL SDL2 driver.
 *
 * When using the LVGL SDL2 driver, touch and mouse input are handled
 * automatically by lv_sdl_mouse_create() and keyboard input by
 * lv_sdl_keyboard_create(). SDL routes Wayland touch events as finger
 * events internally, so no direct evdev access is needed.
 *
 * This module's only job is to create those indevs and assign the
 * keyboard to the LVGL navigation group.
 *
 * TODO (MSPM0 I2C joystick):
 *   When the I2C input driver is ready, register a custom
 *   LV_INDEV_TYPE_KEYPAD indev here and assign it to nav_group.
 *   The read callback should consult a mutex-protected state struct
 *   updated by your I2C polling thread:
 *
 *     typedef struct {
 *         pthread_mutex_t lock;
 *         uint16_t        button_mask;  // raw bitmask from MSPM0
 *         int8_t          axis_x;       // joystick X (-127..127)
 *         int8_t          axis_y;       // joystick Y (-127..127)
 *     } mspm0_state_t;
 *
 *   Map D-pad bits → LV_KEY_UP/DOWN/LEFT/RIGHT, A → LV_KEY_ENTER,
 *   B → LV_KEY_ESC. Call lv_indev_set_group(joystick_indev, nav_group).
 */

#ifndef INPUT_H
#define INPUT_H

#include "lvgl/lvgl.h"
#include <stdbool.h>

/**
 * input_setup - Create SDL2 indevs and assign keyboard to nav_group.
 *
 * Must be called after lv_sdl_window_create().
 *
 * @param nav_group  The LVGL group that keyboard navigation targets.
 * @return true on success.
 */
bool input_setup(lv_group_t *nav_group);

/*
 * TODO (MSPM0 I2C joystick):
 * bool input_joystick_init(const char *i2c_dev, uint8_t addr,
 *                          lv_group_t *nav_group);
 * void input_joystick_deinit(void);
 */

#endif /* INPUT_H */
