#pragma once
#include <SDL2/SDL.h>

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

/* ── State flags (same pattern as results_active in ui.h) ──── */
extern bool settings_active;        /* true while settings menu overlay is up    */
extern bool controller_cfg_active;  /* true while controller config screen is up */

/* ── Display limits ────────────────────────────────────────── */
#define MAX_DISPLAY_BUTTONS  32
#define MAX_DISPLAY_AXES     16
#define MAX_EVENT_LOG_LINES   8

/* ── Settings menu (level 1) ───────────────────────────────── */
void open_settings_menu();
void close_settings_menu();

/* ── Controller configuration screen (level 2) ─────────────── */
void open_controller_config();
void close_controller_config();

/* Called from main.cpp to route gamepad nav while settings is open */
void settings_handle_button(SDL_GameControllerButton btn);

/* Called from main.cpp event loop when controller_cfg_active is true */
void controller_cfg_on_joy_button(int button, bool pressed);
void controller_cfg_on_axis(int axis, int16_t value);
void controller_cfg_on_hat(int hat, int value);

/* Log a raw event string to the controller config event log (for GC devices
 * where raw SDL_JOY* events are otherwise skipped by deduplication) */
void controller_cfg_log_raw_event(const char *fmt, ...);
