#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <SDL2/SDL.h>

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

/* ------------------------------------------------------------------ */
/*  Game definitions                                                    */
/* ------------------------------------------------------------------ */
struct Game {
    const char *name;
    const char *binary;
    lv_color_t  card_color;
};

static const Game games[] = {
    { "SuperTuxKart", "/usr/bin/supertuxkart", lv_color_hex(0x1565C0) },
    { "Neverball",    "/usr/bin/neverball",    lv_color_hex(0x2E7D32) },
    { "Neverputt",    "/usr/bin/neverputt",    lv_color_hex(0x00695C) },
    { "RetroArch",    "/usr/bin/retroarch",    lv_color_hex(0x6A1B9A) },
};
static const int NUM_GAMES = sizeof(games) / sizeof(games[0]);

/* ------------------------------------------------------------------ */
/*  Display & renderer globals                                          */
/* ------------------------------------------------------------------ */
static SDL_Window   *sdl_window   = nullptr;
static SDL_Renderer *sdl_renderer = nullptr;
static SDL_Texture  *sdl_texture  = nullptr;
static int           win_w        = 800;
static int           win_h        = 480;

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
/*  Input                                                               */
/* ------------------------------------------------------------------ */
static int32_t touch_x      = 0;
static int32_t touch_y      = 0;
static bool    touch_pressed = false;

static void read_cb(lv_indev_t * /*indev*/, lv_indev_data_t *data)
{
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state   = touch_pressed ? LV_INDEV_STATE_PRESSED
                                  : LV_INDEV_STATE_RELEASED;
}

/* ------------------------------------------------------------------ */
/*  Game launcher                                                       */
/* ------------------------------------------------------------------ */
static void launch_game(const Game *game)
{
    printf("Launching: %s\n", game->binary);

    /* Hide the SDL window while the game runs */
    SDL_HideWindow(sdl_window);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        SDL_ShowWindow(sdl_window);
        return;
    }

    if (pid == 0) {
        /* Child process — replace ourselves with the game */
        execl(game->binary, game->binary, nullptr);
        /* If execl returns, something went wrong */
        perror("execl failed");
        _exit(1);
    }

    /* Parent process — wait for the game to exit */
    int status;
    waitpid(pid, &status, 0);
    printf("Game exited with status %d, returning to launcher\n",
           WEXITSTATUS(status));

    /* Restore the launcher window */
    SDL_ShowWindow(sdl_window);
    SDL_RaiseWindow(sdl_window);

    /* Force LVGL to redraw everything */
    lv_obj_invalidate(lv_screen_active());
}

/* ------------------------------------------------------------------ */
/*  Card click event                                                    */
/* ------------------------------------------------------------------ */
static void card_click_cb(lv_event_t *e)
{
    const Game *game = static_cast<const Game *>(lv_event_get_user_data(e));
    launch_game(game);
}

/* ------------------------------------------------------------------ */
/*  UI construction                                                     */
/* ------------------------------------------------------------------ */
static void build_ui()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* ---- Header bar ---- */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, win_w, 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 10, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "PHYTEC Game Launcher");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 5, 0);

    /* ---- Grid container ---- */
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, win_w, win_h - 50);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x0f0f1a), 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 20, 0);
    lv_obj_set_style_pad_gap(grid, 20, 0);

    /* Use flex layout — wrap into rows automatically */
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_CENTER,
                                LV_FLEX_ALIGN_START);

    /* Card size — fits 4 per row on 800px, 2 per row on smaller */
    const int CARD_W = (win_w - 60) / 4 - 5;   /* 4 columns */
    const int CARD_H = (win_h - 50 - 60) / 2;  /* 2 rows */

    for (int i = 0; i < NUM_GAMES; i++) {
        /* ---- Card ---- */
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_bg_color(card, games[i].card_color, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x3a3a5c), 0);
        lv_obj_set_style_shadow_width(card, 10, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
        lv_obj_set_style_pad_all(card, 8, 0);

        /* Pressed state — lighten slightly */
        lv_obj_set_style_bg_opa(card, LV_OPA_80, LV_STATE_PRESSED);

        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED,
                            const_cast<Game *>(&games[i]));

        /* ---- Game name label ---- */
        lv_obj_t *name_label = lv_label_create(card);
        lv_label_set_text(name_label, games[i].name);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(name_label, CARD_W - 16);
        lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
        lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                         */
/* ------------------------------------------------------------------ */
int main(int /*argc*/, char ** /*argv*/)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

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
    printf("Window size: %dx%d\n", win_w, win_h);

    sdl_texture = SDL_CreateTexture(
        sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        win_w, win_h
    );

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
