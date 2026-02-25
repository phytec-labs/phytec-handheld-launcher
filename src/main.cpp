#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <SDL2/SDL.h>

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */
#define MAX_GAMES       12
#define MAX_ARGS        8
#define MAX_STR         256
#define CONFIG_PATH     "/etc/phytec-launcher/launcher.conf"
#define COLS            3
#define ROWS            2
#define HEADER_H        54
#define PAD             20
#define GAP             16

/* ------------------------------------------------------------------ */
/*  Modern minimal color palette                                        */
/* ------------------------------------------------------------------ */
#define COL_BG          0x0d1117   /* page background                 */
#define COL_HEADER      0x161b22   /* header bar                      */
#define COL_CARD        0x1c2333   /* card face                       */
#define COL_CARD_BORDER 0x30363d   /* card border (unselected)        */
#define COL_ACCENT      0x58a6ff   /* selected border / highlight     */
#define COL_TEXT        0xe6edf3   /* primary text                    */
#define COL_SUBTEXT     0x8b949e   /* secondary text / hints          */
#define COL_PRESSED     0x0d419d   /* card tint when pressed          */

/* ------------------------------------------------------------------ */
/*  Game entry                                                          */
/* ------------------------------------------------------------------ */
struct Game {
    char name[MAX_STR];
    char binary[MAX_STR];
    char args[MAX_ARGS][MAX_STR];
    int  num_args;
};

static Game  games[MAX_GAMES];
static int   num_games    = 0;

/* ------------------------------------------------------------------ */
/*  Display & renderer globals                                          */
/* ------------------------------------------------------------------ */
static SDL_Window        *sdl_window   = nullptr;
static SDL_Renderer      *sdl_renderer = nullptr;
static SDL_Texture       *sdl_texture  = nullptr;
static SDL_GameController *sdl_gamepad = nullptr;
static int                win_w        = 800;
static int                win_h        = 480;

/* ------------------------------------------------------------------ */
/*  Navigation state                                                    */
/* ------------------------------------------------------------------ */
static int       selected_index  = 0;
static lv_obj_t *cards[MAX_GAMES];

/* Touch debounce — ignore touches briefly after returning from a game */
static Uint32    resume_time     = 0;
#define TOUCH_DEBOUNCE_MS  600

/* ------------------------------------------------------------------ */
/*  Forward declarations                                                */
/* ------------------------------------------------------------------ */
static void launch_game(const Game *game);
static void update_selection(int new_index);

/* ------------------------------------------------------------------ */
/*  Config file parser                                                  */
/* ------------------------------------------------------------------ */
static void write_default_config()
{
    /* Create directory if needed */
    system("mkdir -p /etc/phytec-launcher");

    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        fprintf(stderr, "Could not write default config to %s\n", CONFIG_PATH);
        return;
    }
    fprintf(f,
        "# PHYTEC Game Launcher Configuration\n"
        "# Each [game] block defines one application entry.\n"
        "# args= is optional. Use space-separated arguments.\n"
        "\n"
        "[game]\n"
        "name=SuperTuxKart\n"
        "binary=/usr/bin/supertuxkart\n"
        "args=--fullscreen\n"
        "\n"
        "[game]\n"
        "name=Neverball\n"
        "binary=/usr/bin/neverball\n"
        "args=-f\n"
        "\n"
        "[game]\n"
        "name=Neverputt\n"
        "binary=/usr/bin/neverputt\n"
        "args=-f\n"
        "\n"
        "[game]\n"
        "name=RetroArch\n"
        "binary=/usr/bin/retroarch\n"
        "args=-f\n"
    );
    fclose(f);
    printf("Default config written to %s\n", CONFIG_PATH);
}

static void trim(char *s)
{
    /* Strip leading whitespace */
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    /* Strip trailing whitespace and newline */
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                        s[len-1] == ' '  || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}

static void parse_args(Game *game, const char *args_str)
{
    /* Split args_str on spaces into game->args array */
    char buf[MAX_STR];
    strncpy(buf, args_str, MAX_STR - 1);
    buf[MAX_STR - 1] = '\0';

    game->num_args = 0;
    char *token = strtok(buf, " ");
    while (token && game->num_args < MAX_ARGS) {
        strncpy(game->args[game->num_args], token, MAX_STR - 1);
        game->num_args++;
        token = strtok(nullptr, " ");
    }
}

static void load_config()
{
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        printf("No config found, generating default at %s\n", CONFIG_PATH);
        write_default_config();
        f = fopen(CONFIG_PATH, "r");
        if (!f) {
            fprintf(stderr, "Failed to open config, using hardcoded defaults\n");
            return;
        }
    }

    char     line[MAX_STR];
    Game    *current  = nullptr;
    bool     in_game  = false;

    while (fgets(line, sizeof(line), f)) {
        trim(line);

        /* Skip blank lines and comments */
        if (line[0] == '\0' || line[0] == '#') continue;

        if (strcmp(line, "[game]") == 0) {
            if (num_games < MAX_GAMES) {
                current = &games[num_games];
                memset(current, 0, sizeof(Game));
                num_games++;
                in_game = true;
            } else {
                fprintf(stderr, "Max games (%d) reached, skipping\n", MAX_GAMES);
                in_game = false;
            }
            continue;
        }

        if (!in_game || !current) continue;

        /* Parse key=value */
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "name") == 0) {
            strncpy(current->name, val, MAX_STR - 1);
        } else if (strcmp(key, "binary") == 0) {
            strncpy(current->binary, val, MAX_STR - 1);
        } else if (strcmp(key, "args") == 0) {
            parse_args(current, val);
        }
    }

    fclose(f);
    printf("Loaded %d game(s) from config\n", num_games);

    /* Validate — remove entries with missing binary */
    for (int i = 0; i < num_games; ) {
        if (current->binary[0] == '\0' || access(games[i].binary, X_OK) != 0) {
            fprintf(stderr, "Skipping '%s' — binary not found or not executable: %s\n",
                    games[i].name, games[i].binary);
            /* Shift remaining entries down */
            for (int j = i; j < num_games - 1; j++)
                games[j] = games[j + 1];
            num_games--;
        } else {
            i++;
        }
    }
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
    /* Suppress all input during debounce window */
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

/* ------------------------------------------------------------------ */
/*  Gamepad                                                             */
/* ------------------------------------------------------------------ */
static void init_gamepad()
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

/* ------------------------------------------------------------------ */
/*  Selection highlight                                                 */
/* ------------------------------------------------------------------ */
static void update_selection(int new_index)
{
    if (new_index < 0 || new_index >= num_games) return;

    /* Remove highlight from old selection */
    lv_obj_set_style_border_width(cards[selected_index], 1, 0);
    lv_obj_set_style_border_color(cards[selected_index],
                                  lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_shadow_width(cards[selected_index], 0, 0);
    lv_obj_set_style_bg_color(cards[selected_index],
                              lv_color_hex(COL_CARD), 0);

    selected_index = new_index;

    /* Apply highlight to new selection */
    lv_obj_set_style_border_width(cards[selected_index], 3, 0);
    lv_obj_set_style_border_color(cards[selected_index],
                                  lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_width(cards[selected_index], 30, 0);
    lv_obj_set_style_shadow_color(cards[selected_index],
                                  lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_opa(cards[selected_index], LV_OPA_30, 0);
    lv_obj_set_style_bg_color(cards[selected_index],
                              lv_color_hex(0x1a2744), 0);
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
        const char *argv[MAX_ARGS + 2];
        int argc = 0;
        argv[argc++] = game->binary;
        for (int i = 0; i < game->num_args && argc < MAX_ARGS + 1; i++)
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

    /* Flush all stale input events accumulated while game was running */
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    touch_pressed = false;

    /* Record resume time — read_cb will suppress input for TOUCH_DEBOUNCE_MS */
    resume_time = SDL_GetTicks();

    /* Let compositor settle before showing the launcher */
    SDL_Delay(300);

    SDL_ShowWindow(sdl_window);
    SDL_RaiseWindow(sdl_window);
    lv_obj_invalidate(lv_screen_active());
}

/* ------------------------------------------------------------------ */
/*  Card touch callback                                                 */
/* ------------------------------------------------------------------ */
static void card_click_cb(lv_event_t *e)
{
    /* Double-check debounce at the LVGL event level too */
    if (SDL_GetTicks() - resume_time < TOUCH_DEBOUNCE_MS) return;

    const Game *game = static_cast<const Game *>(lv_event_get_user_data(e));
    launch_game(game);
}

/* ------------------------------------------------------------------ */
/*  Gamepad button handler                                              */
/* ------------------------------------------------------------------ */
static void handle_gamepad_button(SDL_GameControllerButton btn)
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

/* ------------------------------------------------------------------ */
/*  UI                                                                  */
/* ------------------------------------------------------------------ */
static void build_ui()
{
    const int CARD_W = (win_w - PAD * 2 - GAP * (COLS - 1)) / COLS;
    const int CARD_H = (win_h - HEADER_H - PAD * 2 - GAP * (ROWS - 1)) / ROWS;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* ---- Header ---- */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, win_w, HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COL_HEADER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, PAD, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Bottom border line on header as a separator */
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(COL_CARD_BORDER), 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "PHYTEC Game Launcher");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *hint = lv_label_create(header);
    lv_label_set_text(hint, "D-Pad: Navigate     A: Launch");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ---- Cards ---- */
    for (int i = 0; i < num_games; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int x   = PAD + col * (CARD_W + GAP);
        int y   = HEADER_H + PAD + row * (CARD_H + GAP);

        lv_obj_t *card = lv_obj_create(scr);
        cards[i] = card;

        lv_obj_set_pos(card, x, y);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(COL_CARD_BORDER), 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

        /* Pressed state */
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_PRESSED),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_color(card, lv_color_hex(COL_ACCENT),
                                      LV_STATE_PRESSED);

        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED,
                            &games[i]);

        /* Game name — centered, primary text color */
        lv_obj_t *name_lbl = lv_label_create(card);
        lv_label_set_text(name_lbl, games[i].name);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(name_lbl, CARD_W - 24);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name_lbl, LV_ALIGN_CENTER, 0, 0);
    }

    /* Apply initial selection highlight */
    update_selection(0);
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                         */
/* ------------------------------------------------------------------ */
int main(int /*argc*/, char ** /*argv*/)
{
    load_config();

    if (num_games == 0) {
        fprintf(stderr, "No valid games found in config. Exiting.\n");
        return 1;
    }

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
    printf("Window: %dx%d | Games: %d\n", win_w, win_h, num_games);

    sdl_texture = SDL_CreateTexture(
        sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        win_w, win_h
    );

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

                case SDL_CONTROLLERDEVICEADDED:
                    if (!sdl_gamepad) {
                        sdl_gamepad = SDL_GameControllerOpen(ev.cdevice.which);
                        printf("Gamepad connected: %s\n",
                               SDL_GameControllerName(sdl_gamepad));
                    }
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (sdl_gamepad &&
                        SDL_GameControllerFromInstanceID(ev.cdevice.which)
                            == sdl_gamepad) {
                        SDL_GameControllerClose(sdl_gamepad);
                        sdl_gamepad = nullptr;
                        printf("Gamepad disconnected\n");
                    }
                    break;

                case SDL_CONTROLLERBUTTONDOWN:
                    handle_gamepad_button(
                        static_cast<SDL_GameControllerButton>(
                            ev.cbutton.button));
                    break;

                case SDL_MOUSEBUTTONDOWN:
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

    if (sdl_gamepad) SDL_GameControllerClose(sdl_gamepad);
    delete[] buf1;
    delete[] buf2;
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    return 0;
}
