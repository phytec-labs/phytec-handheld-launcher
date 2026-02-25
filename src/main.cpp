#include <cstdio>
#include <cstdint>
#include <SDL2/SDL.h>

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

/* ------------------------------------------------------------------ */
/*  Display & renderer globals                                          */
/* ------------------------------------------------------------------ */
static SDL_Window   *sdl_window   = nullptr;
static SDL_Renderer *sdl_renderer = nullptr;
static SDL_Texture  *sdl_texture  = nullptr;

static int win_w = 800;
static int win_h = 480;

/* ------------------------------------------------------------------ */
/*  Button click callback                                               */
/* ------------------------------------------------------------------ */
static void btn_click_cb(lv_event_t *e)
{
    static int count = 0;
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_obj_t *lbl = static_cast<lv_obj_t *>(lv_obj_get_user_data(btn));
    count++;
    lv_label_set_text_fmt(lbl, "Count: %d", count);
}

/* ------------------------------------------------------------------ */
/*  LVGL flush callback                                                 */
/* ------------------------------------------------------------------ */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    SDL_Rect rect = { area->x1, area->y1, w, h };

    SDL_UpdateTexture(sdl_texture, &rect,
                      reinterpret_cast<uint32_t *>(px_map),
                      w * static_cast<int>(sizeof(uint32_t)));

    SDL_RenderCopy(sdl_renderer, sdl_texture, nullptr, nullptr);
    SDL_RenderPresent(sdl_renderer);

    lv_display_flush_ready(disp);
}

/* ------------------------------------------------------------------ */
/*  LVGL input-device read callback                                     */
/* ------------------------------------------------------------------ */
static int32_t touch_x     = 0;
static int32_t touch_y     = 0;
static bool    touch_pressed = false;

static void read_cb(lv_indev_t * /*indev*/, lv_indev_data_t *data)
{
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state   = touch_pressed ? LV_INDEV_STATE_PRESSED
                                  : LV_INDEV_STATE_RELEASED;
}

/* ------------------------------------------------------------------ */
/*  UI construction                                                     */
/* ------------------------------------------------------------------ */
static void build_ui()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Hello, World!");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "AM62P | LVGL 9.1 | SDL2");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8888cc), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -35);

    /* Counter label (created before button so we can pass it in) */
    lv_obj_t *counter_label = lv_label_create(scr);
    lv_label_set_text(counter_label, "Count: 0");
    lv_obj_set_style_text_color(counter_label, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 100);

    /* Button */
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x4a4aff), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_user_data(btn, counter_label);
    lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Press Me");
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
    lv_obj_center(btn_label);
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                         */
/* ------------------------------------------------------------------ */
int main(int /*argc*/, char ** /*argv*/)
{
    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    sdl_window = SDL_CreateWindow(
        "PHYTEC Handheld Launcher",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );
    if (!sdl_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        fprintf(stderr, "HW renderer failed (%s), falling back to software\n", SDL_GetError());
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);
    }

    /* Use actual compositor-assigned size */
    SDL_GetWindowSize(sdl_window, &win_w, &win_h);
    printf("Window size: %dx%d\n", win_w, win_h);

    sdl_texture = SDL_CreateTexture(
        sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        win_w, win_h
    );

    /* LVGL init */
    lv_init();

    /* Display */
    lv_display_t *disp = lv_display_create(win_w, win_h);

    /* Draw buffers — 1/10th of screen height each */
    const size_t buf_px  = static_cast<size_t>(win_w) * (win_h / 10);
    auto *buf1 = new uint32_t[buf_px];
    auto *buf2 = new uint32_t[buf_px];
    lv_display_set_buffers(disp, buf1, buf2,
                           buf_px * sizeof(uint32_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    /* Input device */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);

    /* Build UI */
    build_ui();

    /* Main loop */
    Uint32 last_tick = SDL_GetTicks();
    bool   running   = true;

    while (running) {
        Uint32 now   = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick    = now;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    touch_pressed = true;
                    touch_x = ev.button.x;
                    touch_y = ev.button.y;
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
                    touch_pressed = true;
                    touch_x = static_cast<int32_t>(ev.tfinger.x * win_w);
                    touch_y = static_cast<int32_t>(ev.tfinger.y * win_h);
                    break;
                case SDL_FINGERUP:
                    touch_pressed = false;
                    break;
                case SDL_FINGERMOTION:
                    touch_x = static_cast<int32_t>(ev.tfinger.x * win_w);
                    touch_y = static_cast<int32_t>(ev.tfinger.y * win_h);
                    break;

                default:
                    break;
            }
        }

        uint32_t sleep_ms = lv_timer_handler();
        SDL_Delay(LV_MIN(sleep_ms, 5));
    }

    delete[] buf1;
    delete[] buf2;
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    return 0;
}
