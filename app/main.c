/**
 * main.c
 * Entry point for the PHYTEC AM62P LVGL launcher (Wayland backend).
 *
 * Responsibilities:
 *   1. Initialise LVGL
 *   2. Create a fullscreen Wayland window via lv_wayland_create_window()
 *   3. Assign Wayland-provided input devices to the navigation group
 *   4. Create the launcher UI
 *   5. Run the Weston-compatible event loop
 *
 * Event loop note:
 *   We use lv_wayland_timer_handler() instead of lv_timer_handler() and
 *   sleep with poll() on the Wayland socket fd. This is the pattern
 *   explicitly required by the LVGL Wayland driver for correct behaviour
 *   on Weston — calling lv_timer_handler() directly can cause dropped
 *   frames because Weston does not allow resizing shared memory buffers
 *   during a commit.
 *
 *   lv_wayland_timer_handler() returns false when the window is minimized
 *   or hidden (i.e. while a child app is running). In that state the
 *   process sleeps indefinitely on poll() until Weston sends an event,
 *   keeping CPU usage near zero while idle.
 *
 * Environment variables:
 *   WAYLAND_DISPLAY  — Wayland socket name (default: wayland-0)
 *                      Inherited automatically; set explicitly if needed.
 */

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/wayland/lv_wayland.h"

#include "input.h"
#include "launcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <poll.h>
#include <limits.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

#define DISPLAY_W 1280
#define DISPLAY_H 720

/* ------------------------------------------------------------------ */
/*  Globals                                                             */
/* ------------------------------------------------------------------ */

static volatile bool s_running       = true;
static bool          s_window_closed = false;

/* ------------------------------------------------------------------ */
/*  Window close callback                                               */
/* ------------------------------------------------------------------ */

static void window_close_cb(lv_event_t *e)
{
    (void)e;
    s_window_closed = true;
}

/* ------------------------------------------------------------------ */
/*  Signal handler — clean shutdown on Ctrl-C / SIGTERM                */
/* ------------------------------------------------------------------ */

static void sig_handler(int sig)
{
    (void)sig;
    s_running = false;
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* 1. Initialise LVGL */
    lv_init();

    /*
     * 2. Initialise the Wayland driver and create a fullscreen window.
     *
     * lv_wayland_create_window() connects to the Wayland compositor,
     * registers display and input drivers with LVGL, and returns an
     * lv_display_t handle. The window_close_cb fires if the compositor
     * asks us to close (e.g. user kills the window from a task manager).
     *
     * lv_wayland_window_set_fullscreen() enters Weston's fullscreen mode
     * so the launcher covers the whole screen without decorations.
     */
    lv_display_t *disp = lv_wayland_create_window(DISPLAY_W, DISPLAY_H,
                                                   "PHYTEC Launcher",
                                                   window_close_cb);
    if (!disp) {
        fprintf(stderr, "[main] Failed to create Wayland window. "
                        "Is WAYLAND_DISPLAY set and Weston running?\n");
        return 1;
    }

    lv_wayland_window_set_fullscreen(disp, true);
    printf("[main] Wayland window created (%dx%d, fullscreen)\n",
           DISPLAY_W, DISPLAY_H);

    /* 3. Create navigation group and set up input devices */
    lv_group_t *nav_group = lv_group_create();
    lv_group_set_default(nav_group);

    input_setup(disp, nav_group);

    /* 4. Build the launcher UI */
    launcher_create(disp, nav_group);

    /*
     * 5. Main event loop — Weston-compatible.
     *
     * lv_wayland_timer_handler() wraps lv_timer_handler() and gives the
     * Wayland driver control over when flush callbacks are issued. It
     * returns the number of ms until the next scheduled LVGL task, or
     * false if the current frame was not rendered (window hidden/minimized).
     *
     * We sleep using poll() on the Wayland socket fd so that any incoming
     * compositor event (input, frame callback, configure) wakes us
     * immediately. This avoids busy-waiting while keeping latency low.
     */
    struct pollfd pfd = {
        .fd     = lv_wayland_get_fd(),
        .events = POLLIN,
    };

    printf("[main] Launcher running.\n");

    while (s_running && !s_window_closed) {
        uint32_t time_till_next = lv_wayland_timer_handler();

        /* Stop if all Wayland windows have been closed */
        if (!lv_wayland_window_is_open(NULL))
            break;

        /*
         * Calculate how long to sleep.
         * - LV_NO_TIMER_READY: no pending tasks → sleep until Wayland event
         * - 0:                 task due immediately → don't sleep
         * - otherwise:         sleep for that many ms, capped at INT_MAX
         */
        int sleep_ms;
        if (time_till_next == LV_NO_TIMER_READY)
            sleep_ms = -1;          /* Indefinite — wake on Wayland event  */
        else if (time_till_next == 0)
            sleep_ms = 0;           /* Run again immediately               */
        else if (time_till_next > (uint32_t)INT_MAX)
            sleep_ms = INT_MAX;
        else
            sleep_ms = (int)time_till_next;

        /* Sleep until the Wayland socket has data or the timeout expires */
        while (poll(&pfd, 1, sleep_ms) < 0 && errno == EINTR)
            /* Retry on EINTR (signals like SIGCHLD interrupt poll) */ ;

        /*
         * TODO (MSPM0 I2C joystick — single-threaded polling):
         * If the I2C poll runs in the main thread rather than a pthread,
         * call it here after poll() returns:
         *     mspm0_poll();   // non-blocking I2C read, update shared state
         * Keep it fast. If polling takes > a few ms, move it to a pthread
         * and use a mutex to share state with the LVGL keypad read callback.
         */
    }

    /* Cleanup */
    printf("[main] Shutting down.\n");
    lv_group_del(nav_group);
    lv_wayland_deinit();

    return 0;
}
