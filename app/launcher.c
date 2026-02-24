/**
 * launcher.c
 * LVGL launcher UI: card grid and async app launching (SDL2 backend).
 *
 * Process model:
 *   1. User taps/presses Enter on an app card.
 *   2. SDL_MinimizeWindow() hides the launcher so the child app can take
 *      the full display via its own SDL/Wayland surface.
 *   3. Child is launched via fork() + execv().
 *   4. SIGCHLD sets an atomic flag.
 *   5. An lv_timer (250 ms) polls waitpid(WNOHANG) from the LVGL task
 *      context (safe for LVGL API calls). On child exit it calls
 *      SDL_ShowWindow() + SDL_RaiseWindow() and re-applies fullscreen.
 *
 * The SDL_Window handle is obtained via SDL_GetWindowFromID(1) which is
 * always valid after lv_sdl_window_create() opens the single app window.
 *
 * TODO (custom PNG icons): replace LV_SYMBOL_* via lv_image_set_src().
 * TODO (status bar): battery, Wi-Fi, clock widgets in a top bar.
 * TODO (runtime app list): JSON/INI parser instead of compiled apps.h.
 */

#include "launcher.h"
#include "apps.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdatomic.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/*  Colours                                                             */
/* ------------------------------------------------------------------ */

#define COLOR_BG     lv_color_hex(0x1A1A2E)
#define COLOR_CARD   lv_color_hex(0x16213E)
#define COLOR_ACCENT lv_color_hex(0xE94560)
#define COLOR_TEXT   lv_color_hex(0xEEEEEE)

/* ------------------------------------------------------------------ */
/*  State                                                               */
/* ------------------------------------------------------------------ */

static lv_group_t  *s_nav_group  = NULL;
static pid_t        s_child_pid  = -1;
static atomic_int   s_child_done = 0;
static int          s_child_idx  = -1;
static lv_timer_t  *s_wait_timer = NULL;

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static SDL_Window *get_sdl_win(void)
{
    /*
     * lv_sdl_window_create() always opens the first SDL window, so ID 1
     * is reliable for this single-window application.
     */
    return SDL_GetWindowFromID(1);
}

/* ------------------------------------------------------------------ */
/*  SIGCHLD handler                                                     */
/* ------------------------------------------------------------------ */

static void sigchld_handler(int sig)
{
    (void)sig;
    atomic_store(&s_child_done, 1);
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void launch_app(int index);
static void child_wait_timer_cb(lv_timer_t *timer);
static void card_click_cb(lv_event_t *e);

/* ------------------------------------------------------------------ */
/*  launcher_create                                                     */
/* ------------------------------------------------------------------ */

void launcher_create(lv_group_t *nav_group)
{
    s_nav_group = nav_group;

    /* Register SIGCHLD handler */
    struct sigaction sa = {0};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    /* Style the root screen */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title label */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "PHYTEC Launcher");
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    /* Card grid container */
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(82));
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(grid, 16, 0);
    lv_obj_set_style_pad_gap(grid, 16, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; LAUNCHER_APPS[i].name != NULL; i++) {
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_set_size(card, 220, 260);
        lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_color(card, COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        if (nav_group)
            lv_group_add_obj(nav_group, card);

        /* Icon */
        lv_obj_t *icon = lv_label_create(card);
        lv_label_set_text(icon, LAUNCHER_APPS[i].icon_symbol);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(icon, COLOR_ACCENT, 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -20);

        /* Name */
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, LAUNCHER_APPS[i].name);
        lv_obj_set_style_text_color(lbl, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, 190);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 30);
    }

    /* Child-wait polling timer (starts paused) */
    s_wait_timer = lv_timer_create(child_wait_timer_cb, 250, NULL);
    lv_timer_pause(s_wait_timer);

    printf("[launcher] UI ready, %d app(s) registered.\n",
           (int)(sizeof(LAUNCHER_APPS)/sizeof(LAUNCHER_APPS[0])) - 1);
}

/* ------------------------------------------------------------------ */
/*  App launch                                                          */
/* ------------------------------------------------------------------ */

static void launch_app(int index)
{
    if (s_child_pid > 0) {
        printf("[launcher] App already running (pid %d), ignoring.\n",
               (int)s_child_pid);
        return;
    }

    const launcher_app_t *app = &LAUNCHER_APPS[index];
    printf("[launcher] Launching '%s' (%s)\n", app->name, app->binary_path);

    /* Hide launcher window so child can use the full display */
    SDL_Window *win = get_sdl_win();
    if (win) {
        SDL_SetWindowFullscreen(win, 0);   /* leave fullscreen first */
        SDL_MinimizeWindow(win);
    }

    s_child_idx = index;
    s_child_pid = fork();

    if (s_child_pid < 0) {
        perror("[launcher] fork");
        if (win) {
            SDL_ShowWindow(win);
            SDL_RaiseWindow(win);
            SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        s_child_pid = -1;
        s_child_idx = -1;
        return;
    }

    if (s_child_pid == 0) {
        /* Child: inherits WAYLAND_DISPLAY / XDG_RUNTIME_DIR / SDL_VIDEODRIVER */
        execv(app->binary_path, (char *const *)app->argv);
        perror("[launcher:child] execv");
        _exit(127);
    }

    /* Parent: poll for child exit */
    atomic_store(&s_child_done, 0);
    lv_timer_resume(s_wait_timer);
}

/* ------------------------------------------------------------------ */
/*  Child-wait timer (LVGL task context — safe to call LVGL API)        */
/* ------------------------------------------------------------------ */

static void child_wait_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!atomic_load(&s_child_done))
        return;

    int   status = 0;
    pid_t reaped = waitpid(s_child_pid, &status, WNOHANG);

    if (reaped <= 0) {
        atomic_store(&s_child_done, 0);
        return;
    }

    if (WIFEXITED(status))
        printf("[launcher] '%s' exited (code %d).\n",
               LAUNCHER_APPS[s_child_idx].name, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("[launcher] '%s' killed by signal %d.\n",
               LAUNCHER_APPS[s_child_idx].name, WTERMSIG(status));

    s_child_pid = -1;
    s_child_idx = -1;
    atomic_store(&s_child_done, 0);
    lv_timer_pause(s_wait_timer);

    /* Restore launcher to foreground */
    SDL_Window *win = get_sdl_win();
    if (win) {
        SDL_ShowWindow(win);
        SDL_RaiseWindow(win);
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
}

/* ------------------------------------------------------------------ */
/*  Card click callback                                                 */
/* ------------------------------------------------------------------ */

static void card_click_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    launch_app(index);
}
