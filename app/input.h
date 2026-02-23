/**
 * input.h
 * Input setup for the PHYTEC launcher running under a Wayland compositor.
 *
 * When using the LVGL Wayland driver, touch, pointer, and keyboard input
 * are handled automatically by the driver for each window created with
 * lv_wayland_create_window(). We no longer open evdev nodes manually for
 * these devices — Weston routes input through the Wayland protocol instead.
 *
 * This module's responsibility is narrowed to:
 *   1. Retrieving the Wayland-registered keyboard indev and assigning it
 *      to the LVGL navigation group so focus movement works.
 *   2. Providing stub hooks for the future MSPM0 I2C gamepad driver.
 *
 * TODO (MSPM0 I2C joystick):
 *   When the I2C input driver is ready, add:
 *     - input_joystick_init(const char *i2c_dev, uint8_t addr, lv_group_t *g)
 *     - A polling thread or lv_timer callback that reads the MSPM0 over I2C
 *       and pushes the button/axis state into LVGL via a custom
 *       LV_INDEV_TYPE_KEYPAD indev registered with lv_indev_create().
 *
 *   Suggested shared state between the I2C thread and LVGL main loop:
 *     typedef struct {
 *         pthread_mutex_t  lock;
 *         uint16_t         button_mask;   // raw bitmask from MSPM0
 *         int8_t           axis_x;        // joystick X  (-127..127)
 *         int8_t           axis_y;        // joystick Y  (-127..127)
 *     } mspm0_state_t;
 *
 *   The I2C thread writes under the mutex; the LVGL keypad read callback
 *   reads under the mutex and maps bits to LV_KEY_* values.
 *   Register the joystick indev with lv_indev_set_group(indev, nav_group)
 *   so it navigates the launcher alongside the Wayland keyboard.
 */

#ifndef INPUT_H
#define INPUT_H

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/wayland/lv_wayland.h"
#include <stdbool.h>

/**
 * input_setup - Assign Wayland-registered input devices to the nav group.
 *
 * Must be called after lv_wayland_create_window() so that the indev
 * handles are available.
 *
 * @param disp       The lv_display_t* returned by lv_wayland_create_window().
 * @param nav_group  The LVGL group that keyboard navigation should target.
 */
void input_setup(lv_display_t *disp, lv_group_t *nav_group);

/*
 * TODO (MSPM0 I2C joystick):
 * bool input_joystick_init(const char *i2c_dev, uint8_t i2c_addr,
 *                          lv_group_t *nav_group);
 * void input_joystick_deinit(void);
 */

#endif /* INPUT_H */
