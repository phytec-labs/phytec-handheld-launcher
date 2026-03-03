#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <SDL2/SDL.h>

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

#include "config.h"
#include "input.h"
#include "ui.h"
#include "launcher.h"

SDL_Window   *sdl_window   = nullptr;
SDL_Renderer *sdl_renderer = nullptr;
SDL_Texture  *sdl_texture  = nullptr;
int           win_w        = 800;
int           win_h        = 480;

/* ── input-debug logging ────────────────────────────────────────── */

static FILE *debug_log_file = nullptr;

void input_debug_log(const char *fmt, ...)
{
    if (!input_debug) return;

    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    if (debug_log_file) {
        va_start(ap, fmt);
        vfprintf(debug_log_file, fmt, ap);
        va_end(ap);
        fflush(debug_log_file);
    }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    SDL_Rect rect = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(sdl_texture, &rect,
                      reinterpret_cast<uint32_t *>(px_map),
                      w * static_cast<int>(sizeof(uint32_t)));
    if (lv_display_flush_is_last(disp)) {
        SDL_RenderCopy(sdl_renderer, sdl_texture, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);
    }
    lv_display_flush_ready(disp);
}

int main(int argc, char **argv)
{
    /* Parse CLI flags */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input-debug") == 0)
            input_debug = true;
    }

    load_config();
    if (num_games == 0) {
        fprintf(stderr, "No valid games found in config. Exiting.\n");
        return 1;
    }

    /* Open debug log file if input-debug mode is active */
    if (input_debug) {
        debug_log_file = fopen(INPUT_DEBUG_LOG, "w");
        if (!debug_log_file)
            fprintf(stderr, "Warning: could not open %s for writing\n", INPUT_DEBUG_LOG);
        printf("=== INPUT DEBUG MODE ===\n");
        printf("All SDL input events will be logged to stdout and %s\n", INPUT_DEBUG_LOG);
        printf("Press buttons/touch/move sticks to see their SDL mapping.\n");
        printf("========================\n");
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                 SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,     8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,   8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,    8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    sdl_window = SDL_CreateWindow(
        "PHYTEC Launcher",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_OPENGL
    );
    if (!sdl_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        fprintf(stderr, "HW renderer failed (%s), trying software\n", SDL_GetError());
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);
    }

    SDL_GetWindowSize(sdl_window, &win_w, &win_h);
    printf("Window: %dx%d | Games: %d\n", win_w, win_h, num_games);

    sdl_texture = SDL_CreateTexture(
        sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        win_w, win_h
    );

    init_gamepad();

    /* In input-debug mode, enumerate all SDL input devices and open any
       raw joysticks so we can see their button events in the main loop. */
    SDL_Joystick *debug_joy = nullptr;
    if (input_debug) {
        int n = SDL_NumJoysticks();
        input_debug_log("[input-debug] SDL sees %d joystick device(s):\n", n);
        for (int i = 0; i < n; i++) {
            bool is_gc = SDL_IsGameController(i);
            input_debug_log("[input-debug]   [%d] \"%s\"  GameController=%s\n",
                            i, SDL_JoystickNameForIndex(i),
                            is_gc ? "YES" : "NO");
            if (is_gc) {
                SDL_GameController *gc = SDL_GameControllerFromInstanceID(
                    SDL_JoystickGetDeviceInstanceID(i));
                if (gc)
                    input_debug_log("[input-debug]        mapping: %s\n",
                                    SDL_GameControllerMapping(gc));
            }
            /* Open first non-GameController joystick so we see its raw events */
            if (!is_gc && !debug_joy) {
                debug_joy = SDL_JoystickOpen(i);
                if (debug_joy)
                    input_debug_log("[input-debug]   Opened [%d] as raw joystick "
                                    "(%d buttons, %d axes, %d hats)\n",
                                    i, SDL_JoystickNumButtons(debug_joy),
                                    SDL_JoystickNumAxes(debug_joy),
                                    SDL_JoystickNumHats(debug_joy));
            }
        }
        input_debug_log("[input-debug] home_button=%d\n\n", home_button);
    }

    lv_init();

    lv_display_t *disp = lv_display_create(win_w, win_h);
    const size_t buf_px = static_cast<size_t>(win_w) * (win_h / 10);
    auto *buf1 = new uint32_t[buf_px];
    auto *buf2 = new uint32_t[buf_px];
    lv_display_set_buffers(disp, buf1, buf2,
                           buf_px * sizeof(uint32_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);

    build_ui();

    Uint32 last_tick = SDL_GetTicks();
    bool   running   = true;

    while (running) {
        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick  = now;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!sdl_gamepad) {
                        sdl_gamepad = SDL_GameControllerOpen(ev.cdevice.which);
                        printf("Gamepad connected: %s\n",
                               SDL_GameControllerName(sdl_gamepad));
                    }
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (sdl_gamepad &&
                        SDL_GameControllerFromInstanceID(ev.cdevice.which) == sdl_gamepad) {
                        SDL_GameControllerClose(sdl_gamepad);
                        sdl_gamepad = nullptr;
                        printf("Gamepad disconnected\n");
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    input_debug_log("[input-debug] CONTROLLERBUTTONDOWN  button=%d (%s)\n",
                                    ev.cbutton.button,
                                    SDL_GameControllerGetStringForButton(
                                        static_cast<SDL_GameControllerButton>(ev.cbutton.button)));
                    handle_gamepad_button(
                        static_cast<SDL_GameControllerButton>(ev.cbutton.button));
                    break;
                case SDL_CONTROLLERBUTTONUP:
                    input_debug_log("[input-debug] CONTROLLERBUTTONUP    button=%d (%s)\n",
                                    ev.cbutton.button,
                                    SDL_GameControllerGetStringForButton(
                                        static_cast<SDL_GameControllerButton>(ev.cbutton.button)));
                    break;
                case SDL_CONTROLLERAXISMOTION:
                    if (ev.caxis.value > AXIS_DEADZONE ||
                        ev.caxis.value < -AXIS_DEADZONE)
                        input_debug_log("[input-debug] CONTROLLERAXIS        axis=%d (%s) value=%d\n",
                                        ev.caxis.axis,
                                        SDL_GameControllerGetStringForAxis(
                                            static_cast<SDL_GameControllerAxis>(ev.caxis.axis)),
                                        ev.caxis.value);
                    handle_gamepad_axis(&ev.caxis);
                    break;
                case SDL_JOYBUTTONDOWN:
                    input_debug_log("[input-debug] JOYBUTTONDOWN         button=%d  joystick=%d\n",
                                    ev.jbutton.button, ev.jbutton.which);
                    break;
                case SDL_JOYBUTTONUP:
                    input_debug_log("[input-debug] JOYBUTTONUP           button=%d  joystick=%d\n",
                                    ev.jbutton.button, ev.jbutton.which);
                    break;
                case SDL_JOYAXISMOTION:
                    if (ev.jaxis.value > AXIS_DEADZONE ||
                        ev.jaxis.value < -AXIS_DEADZONE)
                        input_debug_log("[input-debug] JOYAXIS               axis=%d  value=%d  joystick=%d\n",
                                        ev.jaxis.axis, ev.jaxis.value, ev.jaxis.which);
                    break;
                case SDL_JOYHATMOTION:
                    input_debug_log("[input-debug] JOYHAT                hat=%d  value=%d  joystick=%d\n",
                                    ev.jhat.hat, ev.jhat.value, ev.jhat.which);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    input_debug_log("[input-debug] MOUSEBUTTONDOWN       x=%d y=%d button=%d\n",
                                    ev.button.x, ev.button.y, ev.button.button);
                    if (SDL_GetTicks() - resume_time >= TOUCH_DEBOUNCE_MS) {
                        touch_pressed = true;
                        touch_x = ev.button.x;
                        touch_y = ev.button.y;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    touch_pressed = false;
                    touch_x = ev.button.x;
                    touch_y = ev.button.y;
                    break;
                case SDL_MOUSEMOTION:
                    touch_x = ev.motion.x;
                    touch_y = ev.motion.y;
                    break;
                case SDL_FINGERDOWN:
                    input_debug_log("[input-debug] FINGERDOWN             x=%.3f y=%.3f finger=%lld\n",
                                    ev.tfinger.x, ev.tfinger.y,
                                    (long long)ev.tfinger.fingerId);
                    if (SDL_GetTicks() - resume_time >= TOUCH_DEBOUNCE_MS) {
                        touch_pressed = true;
                        touch_x = static_cast<int32_t>(ev.tfinger.x * win_w);
                        touch_y = static_cast<int32_t>(ev.tfinger.y * win_h);
                    }
                    break;
                case SDL_FINGERUP:
                    touch_pressed = false;
                    break;
                case SDL_FINGERMOTION:
                    if (SDL_GetTicks() - resume_time >= TOUCH_DEBOUNCE_MS) {
                        touch_x = static_cast<int32_t>(ev.tfinger.x * win_w);
                        touch_y = static_cast<int32_t>(ev.tfinger.y * win_h);
                    }
                    break;
                default:
                    break;
            }
        }

        uint32_t sleep_ms = lv_timer_handler();
        SDL_Delay(LV_MIN(sleep_ms, 5));
    }

    if (debug_joy) SDL_JoystickClose(debug_joy);
    if (sdl_gamepad) SDL_GameControllerClose(sdl_gamepad);
    if (debug_log_file) fclose(debug_log_file);
    delete[] buf1;
    delete[] buf2;
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    return 0;
}
