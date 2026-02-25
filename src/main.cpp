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
    const char  *name;
    const char  *binary;
    lv_color_t   card_color;
    const char  *args[8];
};

static const Game games[] = {
    { "SuperTuxKart", "/usr/bin/supertuxkart", lv_color_hex(0x1565C0),
      { "--fullscreen", nullptr } },
    { "Neverball",    "/usr/bin/neverball",    lv_color_hex(0x2E7D32),
      { "-f", nullptr } },
    { "Neverputt",    "/usr/bin/neverputt",    lv_color_hex(0x00695C),
      { "-f", nullptr } },
    { "RetroArch",    "/usr/bin/retroarch",    lv_color_hex(0x6A1B9A),
      { "-f", nullptr } },
};
static const int NUM_GAMES = sizeof(games) / sizeof(games[0]);
static const int COLS      = 2;
static const int ROWS      = 2;

/* ------------------------------------------------------------------ */
/*  Display & renderer globals                                          */
/* ------------------------------------------------------------------ */
static SDL_Window        *sdl_window    = nullptr;
static SDL_Renderer      *sdl_renderer  = nullptr;
static SDL_Texture       *sdl_texture   = nullptr;
static SDL_GameController *sdl_gamepad  = nullptr;
static int                win_w         = 800;
static int                win_h         = 480;

/* ------------------------------------------------------------------ */
/*  Navigation state                                                    */
/* ------------------------------------------------------------------ */
static int       selected_index = 0;
static lv_obj_t *cards[NUM_GAMES];   /* keep refs to update highlight */

/* ------------------------------------------------------------------ */
/*  Card highlight helper                                               */
/* ------------------------------------------------------------------ */
static void update_selection(int new_index)
{
    /* Remove highlight from old */
    lv_obj_set_style_border_width(cards[selected_index], 2, 0);
    lv_obj_set_style_border_color(cards[selected_index],
                                  lv_color_hex(0x3a3a5c), 0);
    lv_obj_set_style_shadow_width(cards[selected_index], 20, 0);
    lv_obj_set_style_shadow_opa(cards[selected_index], LV_OPA_40, 0);

    selected_index = new_index;

    /* Apply highlight to new */
    lv_obj_set_style_border_width(cards[selected_index], 4, 0);
    lv_obj_set_style_border_color(cards[selected_index],
                                  lv_color_hex(0xffffff), 0);
    lv_obj_set_style_shadow_width(cards[selected_index], 40, 0);
    lv_obj_set_style_shadow_color(cards[selected_index],
                                  lv_color_hex(0xffffff), 0);
    lv_obj_set_style_shadow_opa(cards[selected_index], LV_OPA_60, 0);
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
/*  Gamepad init — opens first available controller                    */
/* ------------------------------------------------------------------ */
static void init_gamepad()
{
    int num_joysticks = SDL_NumJoysticks();
    printf("Joysticks found: %d\n", num_joysticks);

    for (int i = 0; i < num_joysticks; i++) {
        if (SDL_IsGameController(i)) {
            sdl_gamepad = SDL_GameControllerOpen(i);
            if (sdl_gamepad) {
                printf("Gamepad opened: %s\n",
                       SDL_GameControllerName(sdl_gamepad));
                return;
            }
        }
    }
    printf("No gamepad found — touch/mouse only\n");
}

/* ------------------------------------------------------------------ */
/*  Handle gamepad button press                                         */
/* ------------------------------------------------------------------ */
static void handle_gamepad_button(SDL_GameControllerButton btn,
                                  bool &running)
{
    int col = selected_index % COLS;
    int row = selected_index / COLS;

    switch (btn) {
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (col + 1 < COLS && selected_index + 1 < NUM_GAMES)
                update_selection(selected_index + 1);
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (col - 1 >= 0)
                update_selection(selected_index - 1);
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (row + 1 < ROWS && selected_index + COLS < NUM_GAMES)
                update_selection(selected_index + COLS);
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (row - 1 >= 0)
                update_selection(selected_index - COLS);
            break;

        case SDL_CONTROLLER_BUTTON_A:
            launch_game(&games[selected_index]);
            break;

        case SDL_CONTROLLER_BUTTON_B:
            /* B to quit launcher — useful for dev, remove for production */
            running = false;
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Game launcher                                                       */
/* ------------------------------------------------------------------ */
static void launch_game(const Game *game)
{
    printf("Launching: %s\n", game->binary);
    SDL_HideWindow(sdl_window);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        SDL_ShowWindow(sdl_window);
        return;
    }

    if (pid == 0) {
        const char *argv[16];
        int argc = 0;
        argv[argc++] = game->binary;
        for (int i = 0; game->args[i] != nullptr && argc < 15; i++)
            argv[argc++] = game->args[i];
        argv[argc] = nullptr;

        execv(game->binary, const_cast<char *const *>(argv));
        perror("execv failed");
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);
    printf("Game exited (status %d), returning to launcher\n",
           WEXITSTATUS(status));

    SDL_ShowWindow(sdl_window);
    SDL_RaiseWindow(sdl_window);
    lv_obj_invalidate(lv_screen_active());
}

/* ------------------------------------------------------------------ */
/*  Card click event (touch/mouse)                                      */
/* ------------------------------------------------------------------ */
static void card_click_cb(lv_event_t *e)
{
    const Game *game = static_cast<const Game *>(lv_event_get_user_data(e));
    launch_game(game);
}

/* ------------------------------------------------------------------ */
/*  UI                                                                  */
/* ------------------------------------------------------------------ */
static void build_ui()
{
    const int HEADER_H = 50;
    const int PAD      = 24;
    const int GAP      = 20;

    const int CARD_W = (win_w - PAD * 2 - GAP * (COLS - 1)) / COLS;
    const int CARD_H = (win_h - HEADER_H - PAD * 2 - GAP * (ROWS - 1)) / ROWS;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* ---- Header ---- */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, win_w, HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "PHYTEC Game Launcher");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* ---- Controller hint ---- */
    lv_obj_t *hint = lv_label_create(header);
    lv_label_set_text(hint, "D-Pad: Navigate   A: Launch");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6666aa), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -10, 0);

    /* ---- Cards ---- */
    for (int i = 0; i < NUM_GAMES; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int x   = PAD + col * (CARD_W + GAP);
        int y   = HEADER_H + PAD + row * (CARD_H + GAP);

        lv_obj_t *card = lv_obj_create(scr);
        cards[i] = card;   /* store ref for highlight updates */

        lv_obj_set_pos(card, x, y);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_bg_color(card, games[i].card_color, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x3a3a5c), 0);
        lv_obj_set_style_shadow_width(card, 20, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED,
                            const_cast<Game *>(&games[i]));

        /* Centered game name */
        lv_obj_t *name_label = lv_label_create(card);
        lv_label_set_text(name_label, games[i].name);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(name_label, CARD_W - 24);
        lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 0);
    }

    /* Apply initial highlight to card 0 */
    update_selection(0);
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                         */
/* ------------------------------------------------------------------ */
int main(int /*argc*/, char ** /*argv*/)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0) {
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

    /* Init gamepad after window so SDL has a context */
    init_gamepad();

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

                /* Gamepad hotplug */
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

                /* Gamepad buttons */
                case SDL_CONTROLLERBUTTONDOWN:
                    handle_gamepad_button(
                        static_cast<SDL_GameControllerButton>(ev.cbutton.button),
                        running);
                    break;

                /* Touch */
                case SDL_MOUSEBUTTONDOWN:
                    touch_pressed = true;
                    touch_x = ev.button.x; touch_y = ev.button.y; break;
                case SDL_MOUSEBUTTONUP:
                    touch_pressed = false;
                    touch_x = ev.button.x; touch_y = ev.button.y; break;
                case SDL_MOUSEMOTION:
                    touch_x = ev.motion.x; touch_y = ev.motion.y; break;
                case SDL_FINGERDOWN:
                    touch_pressed = true;
                    touch_x = static_cast<int32_t>(ev.tfinger.x * win_w);
                    touch_y = static_cast<int32_t>(ev.tfinger.y * win_h); break;
                case SDL_FINGERUP:
                    touch_pressed = false; break;
                case SDL_FINGERMOTION:
                    touch_x = static_cast<int32_t>(ev.tfinger.x * win_w);
                    touch_y = static_cast<int32_t>(ev.tfinger.y * win_h); break;

                default:
                    break;
            }
        }

        uint32_t sleep_ms = lv_timer_handler();
        SDL_Delay(LV_MIN(sleep_ms, 5));
    }

    if (sdl_gamepad) SDL_GameControllerClose(sdl_gamepad);
    delete[] buf1;
    delete[] buf2;
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    return 0;
}
