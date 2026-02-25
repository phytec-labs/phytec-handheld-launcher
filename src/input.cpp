#include "input.h"
#include "launcher.h"
#include "config.h"
#include <cstdio>

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

SDL_GameController *sdl_gamepad  = nullptr;
int32_t             touch_x      = 0;
int32_t             touch_y      = 0;
bool                touch_pressed = false;
Uint32              resume_time  = 0;

extern int selected_index;
extern int win_w;
extern int win_h;

#define COLS 3
#define ROWS 2

void init_gamepad()
{
    int n = SDL_NumJoysticks();
    printf("Joysticks found: %d\n", n);
    for (int i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            sdl_gamepad = SDL_GameControllerOpen(i);
            if (sdl_gamepad) {
                printf("Gamepad: %s\n", SDL_GameControllerName(sdl_gamepad));
                return;
            }
        }
    }
    printf("No gamepad found — touch/mouse only\n");
}

void handle_gamepad_button(SDL_GameControllerButton btn)
{
    int col = selected_index % COLS;
    int row = selected_index / COLS;

    switch (btn) {
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (col + 1 < COLS && selected_index + 1 < num_games)
                update_selection(selected_index + 1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (col - 1 >= 0)
                update_selection(selected_index - 1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (selected_index + COLS < num_games)
                update_selection(selected_index + COLS);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (row - 1 >= 0)
                update_selection(selected_index - COLS);
            break;
        case SDL_CONTROLLER_BUTTON_A:
            launch_game(&games[selected_index]);
            break;
        default:
            break;
    }
}

void read_cb(lv_indev_t * /*indev*/, lv_indev_data_t *data)
{
    if (SDL_GetTicks() - resume_time < TOUCH_DEBOUNCE_MS) {
        data->state   = LV_INDEV_STATE_RELEASED;
        data->point.x = 0;
        data->point.y = 0;
        return;
    }
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state   = touch_pressed ? LV_INDEV_STATE_PRESSED
                                  : LV_INDEV_STATE_RELEASED;
}
