#pragma once
#include <SDL2/SDL.h>

void init_gamepad();
void handle_gamepad_button(SDL_GameControllerButton btn);

extern SDL_GameController *sdl_gamepad;
extern int32_t touch_x;
extern int32_t touch_y;
extern bool    touch_pressed;
extern Uint32  resume_time;

#define TOUCH_DEBOUNCE_MS 600

void read_cb(lv_indev_t *indev, lv_indev_data_t *data);
