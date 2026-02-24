/**
 * main.c
 * Entry point for the PHYTEC AM62P LVGL launcher (SDL2 backend).
 *
 * SDL2 is used as the display and input abstraction layer, exactly as the
 * existing AM62P 3D demo does. SDL2's Wayland backend creates a Wayland
 * surface on the running Weston compositor automatically when
 * SDL_VIDEODRIVER=wayland is set in the environment (done in the systemd
 * unit).
 *
 * Environment variables (set in phytec-launcher.service):
 *   SDL_VIDEODRIVER=wayland   -- use Weston, not x11 or offscreen
 *   SDL_AUDIODRIVER=dummy     -- suppress audio init warnings
 */

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"

#include <SDL2/SDL.h>

#include "input.h"
#include "launcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

#define DISPLAY_W 1280
#define DISPLAY_H 720

/* ------------------------------------------------------------------ */
/*  Globals                                                             */
/* ------------------------------------------------------------------ */

static volatile bool s_running = true;

/* ------------------------------------------------------------------ */
/*  Signal handler                                                      */
/* ------------------------------------------------------------------ */

static void sig_handler(int sig)
{
    (void)sig;
    s_running = false;
}

/* ------------------------------------------------------------------ */
/*  Tick helper                                                         */
/* ------------------------------------------------------------------ */

static uint32_t get_elapsed_ms(void)
{
    static struct timespec t_prev = { 0, 0 };
    struct timespec t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_now);

    if (t_prev.tv_sec == 0 && t_prev.tv_nsec == 0) {
        t_prev = t_now;
        return 0;
    }

    uint32_t ms = (uint32_t)((t_now.tv_sec  - t_prev.tv_sec)  * 1000U
                           + (t_now.tv_nsec - t_prev.tv_nsec) / 1000000U);
    t_prev = t_now;
    return ms;
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("[main] phytec-launcher build: %s %s", __DATE__, __TIME__);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* 1. Initialise LVGL */
    lv_init();

    /*
     * 2. Create SDL2 window.
     *
     * lv_sdl_window_create() initialises SDL2, opens a window of the
     * given size, and registers the display driver with LVGL. When
     * SDL_VIDEODRIVER=wayland, SDL creates a Wayland surface on Weston
     * exactly like the 3D demo does.
     */
    lv_display_t *disp = lv_sdl_window_create(DISPLAY_W, DISPLAY_H);
    (void)disp;
    if (!disp) {
        fprintf(stderr, "[main] Failed to create SDL2 window. "
                        "Is SDL_VIDEODRIVER=wayland set and Weston running?\n");
        return 1;
    }

    /*
     * Go fullscreen via SDL directly — lv_sdl_window_set_fullscreen()
     * does not exist in LVGL v9.1. SDL_GetWindowFromID(1) is safe here
     * because lv_sdl_window_create() opens exactly one window.
     */
    SDL_Window *sdl_win = SDL_GetWindowFromID(1);
    if (sdl_win) {
        SDL_SetWindowFullscreen(sdl_win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        printf("[main] SDL2 window created (%dx%d, fullscreen)\n",
               DISPLAY_W, DISPLAY_H);
    } else {
        fprintf(stderr, "[main] Warning: could not get SDL window handle\n");
    }

    /* 3. Create navigation group and set up input devices */
    lv_group_t *nav_group = lv_group_create();
    lv_group_set_default(nav_group);

    input_setup(nav_group);

    /*
     * 4. Build the launcher UI.
     *    launcher.c retrieves the SDL window itself via SDL_GetWindowFromID(1)
     *    for hide/restore when launching child apps.
     */
    launcher_create(nav_group);

    /*
     * 5. Main loop.
     *
     * lv_timer_handler() drives all LVGL tasks including the SDL driver's
     * internal SDL_PollEvent loop. We sleep for the returned delay to avoid
     * busy-waiting, capped at 10 ms to stay responsive to signals and to
     * give the SIGCHLD handler a chance to run after a child app exits.
     *
     * TODO (MSPM0 I2C joystick -- single-threaded polling):
     * If the I2C poll runs in the main thread rather than a pthread,
     * call it here before sleeping:
     *     mspm0_poll();   // non-blocking I2C read, update shared state
     */
    printf("[main] Launcher running.\n");

    while (s_running) {
        lv_tick_inc(get_elapsed_ms());

        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 10) sleep_ms = 10;
        usleep(sleep_ms * 1000U);
    }

    /* Cleanup */
    printf("[main] Shutting down.\n");
    lv_group_del(nav_group);

    return 0;
}
