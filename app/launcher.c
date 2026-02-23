/**
 * launcher.c
 * LVGL launcher UI: card grid and async app launching under Wayland.
 *
 * Process model (Wayland version):
 *   When a card is activated, the launcher minimizes its Weston window and
 *   forks a child process to run the app. Rather than blocking the main loop
 *   with waitpid(), we catch SIGCHLD and check for child exit using
 *   waitpid(WNOHANG) inside the LVGL timer loop. When the child exits,
 *   the launcher window is restored and the UI resumes.
 *
 *   This keeps the Wayland event loop alive while the app is running, which
 *   is necessary for the Weston compositor to correctly manage our surface.
 *   Blocking the loop would starve Weston of frame callbacks and could cause
 *   the compositor to kill our client.
 *
 * Layout (1280x720):
 *   - Full-screen dark background
 *   - Header bar with title
 *   - Centred flex-row card grid, wraps for > 4 apps
 *   - Each card: icon symbol + app name, highlighted on focus/hover
 */

#include "launcher.h"
#include "apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdatomic.h>

/* ------------------------------------------------------------------ */
/*  Layout constants (tuned for 1280x720)                              */
/* ------------------------------------------------------------------ */

#define CARD_W          220
#define CARD_H          260
#define CARD_RADIUS     18
#define CARD_PAD        24
#define GRID_PAD_TOP    40

/* Colours */
#define COLOR_BG        lv_color_hex(0x1A1A2E)
#define COLOR_CARD      lv_color_hex(0x16213E)
#define COLOR_CARD_FOC  lv_color_hex(0x0F3460)
#define COLOR_ACCENT    lv_color_hex(0xE94560)
#define COLOR_TEXT      lv_color_hex(0xEEEEEE)
#define COLOR_SUBTEXT   lv_color_hex(0x888888)

/* ------------------------------------------------------------------ */
/*  Child process state                                                 */
/* ------------------------------------------------------------------ */

static pid_t            s_child_pid  = -1;
static lv_display_t    *s_disp       = NULL;  /* Set by launcher_create() */
static lv_timer_t      *s_wait_timer = NULL;  /* Polls for child exit     */

/*
 * s_child_exited is set to 1 by the SIGCHLD handler and read by the
 * lv_timer callback on the main thread. atomic_int avoids a data race
 * without needing a mutex for this single-flag pattern.
 */
static atomic_int s_child_exited = 0;

/* ------------------------------------------------------------------ */
/*  SIGCHLD handler                                                     */
/* ------------------------------------------------------------------ */

static void sigchld_handler(int sig)
{
    (void)sig;
    atomic_store(&s_child_exited, 1);
}

/* ------------------------------------------------------------------ */
/*  Child exit timer callback                                           */
/*  Runs in the LVGL main loop — safe to call LVGL APIs here.          */
/* ------------------------------------------------------------------ */

static void child_wait_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!atomic_load(&s_child_exited))
        return;

    /* Reap the child without blocking */
    int status = 0;
    pid_t ret  = waitpid(s_child_pid, &status, WNOHANG);

    if (ret == 0) {
        /* SIGCHLD fired but child hasn't fully exited yet — check next tick */
        return;
    }

    /* Child has exited */
    if (ret > 0) {
        if (WIFEXITED(status))
            printf("[launcher] Child %d exited with code %d\n",
                   s_child_pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("[launcher] Child %d killed by signal %d\n",
                   s_child_pid, WTERMSIG(status));
    } else {
        /* waitpid error — log and clean up anyway */
        fprintf(stderr, "[launcher] waitpid error: %s\n", strerror(errno));
    }

    /* Reset state */
    s_child_pid = -1;
    atomic_store(&s_child_exited, 0);

    /* Stop the wait timer */
    lv_timer_del(s_wait_timer);
    s_wait_timer = NULL;

    /* Restore the launcher window */
    if (s_disp) {
        lv_wayland_window_set_minimized(s_disp, false);
        lv_wayland_window_set_fullscreen(s_disp, true);
        printf("[launcher] Window restored\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Process launching                                                   */
/* ------------------------------------------------------------------ */

static void launch_app(const launcher_app_t *app)
{
    if (s_child_pid != -1) {
        printf("[launcher] App already running (pid %d), ignoring launch\n",
               s_child_pid);
        return;
    }

    printf("[launcher] Launching: %s\n", app->binary_path);

    /*
     * Minimize our Weston surface before forking so the child's window
     * appears on top cleanly. We stay alive as a Wayland client so
     * Weston doesn't tear down our surface.
     */
    if (s_disp) {
        lv_wayland_window_set_fullscreen(s_disp, false);
        lv_wayland_window_set_minimized(s_disp, true);
    }

    /* Reset the exit flag before fork so we don't double-trigger */
    atomic_store(&s_child_exited, 0);

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "[launcher] fork() failed: %s\n", strerror(errno));
        /* Restore window if fork fails */
        if (s_disp) {
            lv_wayland_window_set_minimized(s_disp, false);
            lv_wayland_window_set_fullscreen(s_disp, true);
        }
        return;
    }

    if (pid == 0) {
        /*
         * ---- Child process ----
         * The child is a regular Wayland client — it will connect to Weston
         * using WAYLAND_DISPLAY from the environment (inherited from the
         * launcher). No special setup needed.
         *
         * execv replaces the process image with the game binary.
         */
        execv(app->binary_path, (char *const *)app->argv);

        /* execv only returns on error */
        fprintf(stderr, "[launcher] execv(%s) failed: %s\n",
                app->binary_path, strerror(errno));
        _exit(1);
    }

    /* ---- Parent ---- */
    s_child_pid = pid;
    printf("[launcher] Child pid: %d\n", pid);

    /*
     * Start a timer that checks for child exit every 250 ms.
     * The SIGCHLD handler sets s_child_exited; the timer does the
     * actual waitpid() call safely from the main loop.
     */
    s_wait_timer = lv_timer_create(child_wait_timer_cb, 250, NULL);

    /*
     * TODO (multi-app):
     * If you ever want to show a "now playing" screen with a "force quit"
     * button while the child runs, create that screen here and load it
     * instead of just minimizing. The timer callback would delete it when
     * the child exits and reload the launcher screen.
     */
}

/* ------------------------------------------------------------------ */
/*  Card event handler                                                  */
/* ------------------------------------------------------------------ */

static void card_event_cb(lv_event_t *e)
{
    lv_event_code_t      code = lv_event_get_code(e);
    const launcher_app_t *app = (const launcher_app_t *)lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED) {
        launch_app(app);
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER)
            launch_app(app);
    } else if (code == LV_EVENT_FOCUSED) {
        lv_obj_set_style_bg_color(lv_event_get_target(e), COLOR_CARD_FOC, 0);
        lv_obj_set_style_border_color(lv_event_get_target(e), COLOR_ACCENT, 0);
        lv_obj_set_style_border_width(lv_event_get_target(e), 3, 0);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_bg_color(lv_event_get_target(e), COLOR_CARD, 0);
        lv_obj_set_style_border_width(lv_event_get_target(e), 0, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  Card factory                                                        */
/* ------------------------------------------------------------------ */

static lv_obj_t *create_card(lv_obj_t          *parent,
                              const launcher_app_t *app,
                              lv_group_t        *nav_group)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (app->icon_symbol) {
        lv_obj_t *icon = lv_label_create(card);
        lv_label_set_text(icon, app->icon_symbol);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(icon, COLOR_ACCENT, 0);
        /*
         * TODO (custom icons):
         * Replace with lv_img_create() + lv_img_set_src() if you add
         * per-app PNG icons. Store the icon path in launcher_app_t.
         */
    }

    lv_obj_t *name_lbl = lv_label_create(card);
    lv_label_set_text(name_lbl, app->name);
    lv_obj_set_style_text_color(name_lbl, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_top(name_lbl, 12, 0);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_ALL,
                        (void *)(uintptr_t)app);

    if (nav_group)
        lv_group_add_obj(nav_group, card);

    /*
     * TODO (MSPM0 joystick navigation):
     * No extra work needed here. Once the MSPM0 indev is registered as
     * LV_INDEV_TYPE_KEYPAD and assigned to nav_group, LVGL's group focus
     * system handles navigation automatically.
     */

    return card;
}

/* ------------------------------------------------------------------ */
/*  Header bar                                                          */
/* ------------------------------------------------------------------ */

static void create_header(lv_obj_t *parent)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), 60);
    lv_obj_set_style_bg_color(hdr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_left(hdr, 24, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "PHYTEC Launcher");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    /*
     * TODO (status bar):
     * Add battery / Wi-Fi / clock widgets to the right side of the header.
     * Use lv_label + lv_timer to update periodically.
     */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void launcher_create(lv_display_t *disp, lv_group_t *nav_group)
{
    /* Store display handle for window minimize/restore */
    s_disp = disp;

    /* Install SIGCHLD handler for async child exit detection */
    struct sigaction sa = {0};
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    /* Screen */
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(screen);

    create_header(screen);

    /* Card grid */
    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_set_size(grid, LV_PCT(100), LV_VER_RES - 60 - GRID_PAD_TOP);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, CARD_PAD, 0);
    lv_obj_set_style_pad_row(grid, CARD_PAD, 0);
    lv_obj_set_style_pad_all(grid, CARD_PAD, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    int count = 0;
    for (int i = 0; LAUNCHER_APPS[i].name != NULL; i++) {
        if (i >= LAUNCHER_MAX_APPS) {
            fprintf(stderr, "[launcher] Warning: app list exceeds "
                    "LAUNCHER_MAX_APPS (%d), truncating\n", LAUNCHER_MAX_APPS);
            break;
        }
        create_card(grid, &LAUNCHER_APPS[i], nav_group);
        count++;
    }

    if (count == 0) {
        lv_obj_t *lbl = lv_label_create(grid);
        lv_label_set_text(lbl, "No apps configured.\nEdit apps.h and rebuild.");
        lv_obj_set_style_text_color(lbl, COLOR_SUBTEXT, 0);
        lv_obj_center(lbl);
    }

    printf("[launcher] Created launcher with %d app(s)\n", count);
}
