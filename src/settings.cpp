#include "settings.h"
#include "ui.h"
#include "input.h"
#include "config.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

extern int win_w;
extern int win_h;
extern SDL_GameController *sdl_gamepad;

/* ── State flags ─────────────────────────────────────────────── */
bool settings_active       = false;
bool controller_cfg_active = false;

/* ── Overlay pointers (same pattern as results_overlay) ──────── */
static lv_obj_t *settings_overlay      = nullptr;
static lv_obj_t *controller_cfg_overlay = nullptr;

/* ── Raw joystick opened for the controller config screen ────── */
static SDL_Joystick *cfg_joystick = nullptr;

/* ── Controller config display state ─────────────────────────── */
struct ControllerDisplayState {
    int  num_buttons;
    int  num_axes;
    int  num_hats;
    bool is_game_controller;

    lv_obj_t *button_indicators[MAX_DISPLAY_BUTTONS];
    lv_obj_t *button_labels[MAX_DISPLAY_BUTTONS];
    lv_obj_t *axis_bars[MAX_DISPLAY_AXES];
    lv_obj_t *axis_value_labels[MAX_DISPLAY_AXES];
    lv_obj_t *event_log_label;
    lv_obj_t *toggle_label;    /* label inside the Index/Symbols toggle button */

    /* Event log ring buffer */
    char event_log[MAX_EVENT_LOG_LINES][128];
    int  event_log_head;
    int  event_log_count;
};

static ControllerDisplayState ds;
static bool show_symbols = true; /* true = named labels, false = numeric indices */

/* ── Helpers ─────────────────────────────────────────────────── */

static const char *gc_labels[] = {
    "A", "B", "X", "Y",        /* 0-3  */
    "BK", "G", "ST",            /* 4-6  BACK, GUIDE, START */
    "LS", "RS",                 /* 7-8  stick clicks */
    "LB", "RB",                 /* 9-10 shoulders */
    LV_SYMBOL_UP, LV_SYMBOL_DOWN,     /* 11-12 D-pad */
    LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT   /* 13-14 D-pad */
};
static const int n_gc_labels = (int)(sizeof(gc_labels) / sizeof(gc_labels[0]));

static const char *gc_axis_labels[] = {
    "LX", "LY", "LT", "RX", "RY", "RT"  /* SDL_CONTROLLER_AXIS_* */
};
static const int n_gc_axis_labels = (int)(sizeof(gc_axis_labels) / sizeof(gc_axis_labels[0]));

static void refresh_button_labels()
{
    for (int i = 0; i < ds.num_buttons; i++) {
        lv_obj_t *lbl = ds.button_labels[i];
        if (!lbl) continue;

        if (show_symbols && ds.is_game_controller && i < n_gc_labels) {
            lv_label_set_text(lbl, gc_labels[i]);
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", i);
            lv_label_set_text(lbl, buf);
        }
    }
}

static void label_toggle_cb(lv_event_t * /*e*/)
{
    show_symbols = !show_symbols;
    refresh_button_labels();

    /* Update the toggle button's own label to show opposite action */
    if (ds.toggle_label) {
        lv_label_set_text(ds.toggle_label,
                          show_symbols ? LV_SYMBOL_LOOP " Index"
                                       : LV_SYMBOL_LOOP " Symbols");
    }
}

static void append_event_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    int idx = ds.event_log_head;
    strncpy(ds.event_log[idx], buf, sizeof(ds.event_log[idx]) - 1);
    ds.event_log[idx][sizeof(ds.event_log[idx]) - 1] = '\0';
    ds.event_log_head = (idx + 1) % MAX_EVENT_LOG_LINES;
    if (ds.event_log_count < MAX_EVENT_LOG_LINES)
        ds.event_log_count++;

    /* Rebuild the label text from oldest to newest */
    if (!ds.event_log_label) return;

    char full_text[MAX_EVENT_LOG_LINES * 128];
    full_text[0] = '\0';
    int start = (ds.event_log_count < MAX_EVENT_LOG_LINES)
                ? 0 : ds.event_log_head;
    for (int i = 0; i < ds.event_log_count; i++) {
        int ri = (start + i) % MAX_EVENT_LOG_LINES;
        strcat(full_text, ds.event_log[ri]);
        if (i < ds.event_log_count - 1) strcat(full_text, "\n");
    }
    lv_label_set_text(ds.event_log_label, full_text);
}

/* ── Settings Menu (Level 1) ─────────────────────────────────── */

static void settings_back_cb(lv_event_t * /*e*/)
{
    close_settings_menu();
}

static void controller_cfg_item_cb(lv_event_t * /*e*/)
{
    close_settings_menu();
    open_controller_config();
}

void open_settings_menu()
{
    if (settings_active || controller_cfg_active) return;

    settings_active = true;
    lv_obj_t *scr = lv_screen_active();

    settings_overlay = lv_obj_create(scr);
    lv_obj_t *ov = settings_overlay;

    lv_obj_set_size(ov, win_w, win_h);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_radius(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, PAD, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(ov);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Hint */
    lv_obj_t *hint = lv_label_create(ov);
    lv_label_set_text(hint, "B: Back");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_42, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, 8);

    /* ── Menu item: Controller Configuration ───────────────── */
    int item_w = win_w - PAD * 4;
    int item_h = 120;
    int item_y = 140;

    lv_obj_t *item = lv_obj_create(ov);
    lv_obj_set_size(item, item_w, item_h);
    lv_obj_align(item, LV_ALIGN_TOP_MID, 0, item_y);
    lv_obj_set_style_bg_color(item, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(item, 12, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_pad_hor(item, PAD, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(item, lv_color_hex(COL_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(item, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_add_event_cb(item, controller_cfg_item_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *item_lbl = lv_label_create(item);
    lv_label_set_text(item_lbl, LV_SYMBOL_USB "  Controller Configuration");
    lv_obj_set_style_text_color(item_lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(item_lbl, &lv_font_montserrat_42, 0);
    lv_obj_align(item_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *arrow_lbl = lv_label_create(item);
    lv_label_set_text(arrow_lbl, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow_lbl, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(arrow_lbl, &lv_font_montserrat_42, 0);
    lv_obj_align(arrow_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ── Back button (bottom-right, matches results overlay) ── */
    lv_obj_t *btn = lv_button_create(ov);
    lv_obj_set_size(btn, 500, 72);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(btn, 20, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
    lv_obj_add_event_cb(btn, settings_back_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Back to Launcher");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_42, 0);
    lv_obj_center(btn_lbl);

    lv_timer_handler();
}

void close_settings_menu()
{
    if (settings_overlay) {
        lv_obj_delete(settings_overlay);
        settings_overlay = nullptr;
    }
    settings_active = false;
    lv_obj_invalidate(lv_screen_active());
}

/* ── Controller Configuration (Level 2) ──────────────────────── */

static void controller_cfg_back_cb(lv_event_t * /*e*/)
{
    close_controller_config();
    open_settings_menu();
}

void open_controller_config()
{
    if (controller_cfg_active) return;
    controller_cfg_active = true;
    memset(&ds, 0, sizeof(ds));

    /* ── Query device info ───────────────────────────────────── */
    char   dev_name[256] = "No controller detected";
    char   guid_str[64]  = "N/A";
    bool   is_gc         = false;
    int    num_btns      = 0;
    int    num_axes      = 0;
    int    num_hats      = 0;

    if (sdl_gamepad) {
        SDL_Joystick *joy = SDL_GameControllerGetJoystick(sdl_gamepad);
        snprintf(dev_name, sizeof(dev_name), "%s",
                 SDL_GameControllerName(sdl_gamepad));
        is_gc    = true;
        num_btns = SDL_JoystickNumButtons(joy);
        num_axes = SDL_JoystickNumAxes(joy);
        num_hats = SDL_JoystickNumHats(joy);

        SDL_JoystickGUID guid = SDL_JoystickGetGUID(joy);
        SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));

        /* GameController API maps D-pad hat to virtual buttons 11-14
         * (DPAD_UP/DOWN/LEFT/RIGHT).  These are beyond the raw
         * JoystickNumButtons count, so extend to cover them. */
        if (num_btns < SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1)
            num_btns = SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1;
    } else {
        /* Try to open the first available raw joystick */
        int n = SDL_NumJoysticks();
        for (int i = 0; i < n; i++) {
            if (!SDL_IsGameController(i)) {
                cfg_joystick = SDL_JoystickOpen(i);
                if (cfg_joystick) {
                    snprintf(dev_name, sizeof(dev_name), "%s",
                             SDL_JoystickName(cfg_joystick));
                    num_btns = SDL_JoystickNumButtons(cfg_joystick);
                    num_axes = SDL_JoystickNumAxes(cfg_joystick);
                    num_hats = SDL_JoystickNumHats(cfg_joystick);

                    SDL_JoystickGUID guid = SDL_JoystickGetGUID(cfg_joystick);
                    SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));

                    printf("Controller config: opened raw joystick \"%s\"\n",
                           dev_name);
                    break;
                }
            }
        }
    }

    if (num_btns > MAX_DISPLAY_BUTTONS) num_btns = MAX_DISPLAY_BUTTONS;
    if (num_axes > MAX_DISPLAY_AXES)    num_axes = MAX_DISPLAY_AXES;
    ds.num_buttons         = num_btns;
    ds.num_axes            = num_axes;
    ds.num_hats            = num_hats;
    ds.is_game_controller  = is_gc;

    /* ── Build scrollable overlay ────────────────────────────── */
    lv_obj_t *scr = lv_screen_active();
    controller_cfg_overlay = lv_obj_create(scr);
    lv_obj_t *ov = controller_cfg_overlay;

    lv_obj_set_size(ov, win_w, win_h);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_radius(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, PAD, 0);
    /* Scrollable — content extends beyond viewport with large fonts */

    /* ── Header ──────────────────────────────────────────────── */
    lv_obj_t *title = lv_label_create(ov);
    lv_label_set_text(title, LV_SYMBOL_USB "  Controller Config");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_pos(title, PAD, 0);

    lv_obj_t *hint = lv_label_create(ov);
    lv_label_set_text(hint, "Select: Back");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_42, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, 8);

    /* ── Device info with GUID ───────────────────────────────── */
    int info_y = 70;
    int content_w = win_w - PAD * 2;

    lv_obj_t *dev_lbl = lv_label_create(ov);
    char info_buf[640];
    snprintf(info_buf, sizeof(info_buf),
             "%s\n"
             "%s | Btns:%d Axes:%d Hats:%d\n"
             "GUID: %s",
             dev_name,
             is_gc ? "GameController" : "Raw Joystick",
             num_btns, num_axes, num_hats,
             guid_str);
    lv_label_set_text(dev_lbl, info_buf);
    lv_obj_set_width(dev_lbl, content_w);
    lv_obj_set_style_text_color(dev_lbl, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(dev_lbl, &lv_font_montserrat_42, 0);
    lv_obj_set_pos(dev_lbl, PAD, info_y);

    /* ── Layout calculations ─────────────────────────────────── */
    int content_y   = info_y + 160;  /* 3 lines of ~48px each + gap */
    int back_btn_h  = 72;

    int btn_panel_w = content_w * 45 / 100;
    int axis_panel_w = content_w * 50 / 100;
    int axis_panel_x = content_w - axis_panel_w;

    /* ── Button indicators panel ─────────────────────────────── */
    if (num_btns > 0) {
        lv_obj_t *btn_title = lv_label_create(ov);
        lv_label_set_text(btn_title, "Buttons");
        lv_obj_set_style_text_color(btn_title, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(btn_title, &lv_font_montserrat_42, 0);
        lv_obj_set_pos(btn_title, PAD, content_y);

        /* Index / Symbols toggle (only meaningful for GameController) */
        if (is_gc) {
            lv_obj_t *tog = lv_button_create(ov);
            lv_obj_set_size(tog, 240, 48);
            lv_obj_set_pos(tog, PAD + 220, content_y - 3);
            lv_obj_set_style_bg_color(tog, lv_color_hex(COL_CARD), 0);
            lv_obj_set_style_bg_opa(tog, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(tog, 1, 0);
            lv_obj_set_style_border_color(tog, lv_color_hex(COL_CARD_BORDER), 0);
            lv_obj_set_style_radius(tog, 6, 0);
            lv_obj_set_style_pad_all(tog, 0, 0);
            lv_obj_set_style_bg_color(tog, lv_color_hex(COL_PRESSED), LV_STATE_PRESSED);
            lv_obj_add_event_cb(tog, label_toggle_cb, LV_EVENT_CLICKED, nullptr);

            lv_obj_t *tog_lbl = lv_label_create(tog);
            lv_label_set_text(tog_lbl, show_symbols ? LV_SYMBOL_LOOP " Index"
                                                    : LV_SYMBOL_LOOP " Symbols");
            lv_obj_set_style_text_color(tog_lbl, lv_color_hex(COL_SUBTEXT), 0);
            lv_obj_set_style_text_font(tog_lbl, &lv_font_montserrat_42, 0);
            lv_obj_center(tog_lbl);
            ds.toggle_label = tog_lbl;
        }

        const int BTN_COLS = 5;
        const int BTN_GAP  = 10;
        int avail_w = btn_panel_w - 24;
        int btn_sz  = (avail_w - BTN_GAP * (BTN_COLS - 1)) / BTN_COLS;
        if (btn_sz > 100) btn_sz = 100;
        if (btn_sz < 70)  btn_sz = 70;
        int btn_rows = (num_btns + BTN_COLS - 1) / BTN_COLS;
        int btn_panel_h = btn_rows * (btn_sz + BTN_GAP) + 24;

        lv_obj_t *btn_panel = lv_obj_create(ov);
        lv_obj_set_pos(btn_panel, PAD, content_y + 52);
        lv_obj_set_size(btn_panel, btn_panel_w, btn_panel_h);
        lv_obj_set_style_bg_color(btn_panel, lv_color_hex(COL_HEADER), 0);
        lv_obj_set_style_bg_opa(btn_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn_panel, 1, 0);
        lv_obj_set_style_border_color(btn_panel, lv_color_hex(COL_CARD_BORDER), 0);
        lv_obj_set_style_radius(btn_panel, 8, 0);
        lv_obj_set_style_pad_all(btn_panel, 12, 0);
        lv_obj_clear_flag(btn_panel, LV_OBJ_FLAG_SCROLLABLE);

        for (int i = 0; i < num_btns; i++) {
            int col = i % BTN_COLS;
            int row = i / BTN_COLS;
            int bx  = col * (btn_sz + BTN_GAP);
            int by  = row * (btn_sz + BTN_GAP);

            lv_obj_t *ind = lv_obj_create(btn_panel);
            lv_obj_set_pos(ind, bx, by);
            lv_obj_set_size(ind, btn_sz, btn_sz);
            lv_obj_set_style_bg_color(ind, lv_color_hex(COL_CARD), 0);
            lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(ind, 2, 0);
            lv_obj_set_style_border_color(ind, lv_color_hex(COL_CARD_BORDER), 0);
            lv_obj_set_style_radius(ind, btn_sz / 2, 0);
            lv_obj_set_style_shadow_width(ind, 0, 0);
            lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *lbl = lv_label_create(ind);
            lv_label_set_text(lbl, "");
            lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_42, 0);
            lv_obj_center(lbl);

            ds.button_indicators[i] = ind;
            ds.button_labels[i]     = lbl;
        }

        refresh_button_labels();
    }

    /* ── Axis bars panel ─────────────────────────────────────── */
    if (num_axes > 0) {
        lv_obj_t *axis_title = lv_label_create(ov);
        lv_label_set_text(axis_title, "Axes");
        lv_obj_set_style_text_color(axis_title, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(axis_title, &lv_font_montserrat_42, 0);
        lv_obj_set_pos(axis_title, PAD + axis_panel_x, content_y);

        const int AXIS_ROW_H  = 70;
        const int LABEL_W     = 210;
        int bar_w = axis_panel_w - 24 - LABEL_W - 200;
        if (bar_w < 80) bar_w = 80;
        const int BAR_H = 40;
        int axis_panel_h = num_axes * AXIS_ROW_H + 24;

        lv_obj_t *axis_panel = lv_obj_create(ov);
        lv_obj_set_pos(axis_panel, PAD + axis_panel_x, content_y + 52);
        lv_obj_set_size(axis_panel, axis_panel_w, axis_panel_h);
        lv_obj_set_style_bg_color(axis_panel, lv_color_hex(COL_HEADER), 0);
        lv_obj_set_style_bg_opa(axis_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(axis_panel, 1, 0);
        lv_obj_set_style_border_color(axis_panel, lv_color_hex(COL_CARD_BORDER), 0);
        lv_obj_set_style_radius(axis_panel, 8, 0);
        lv_obj_set_style_pad_all(axis_panel, 12, 0);
        lv_obj_clear_flag(axis_panel, LV_OBJ_FLAG_SCROLLABLE);

        for (int i = 0; i < num_axes; i++) {
            int y_off = i * AXIS_ROW_H;

            lv_obj_t *a_lbl = lv_label_create(axis_panel);
            char axis_name[32];
            snprintf(axis_name, sizeof(axis_name), "Axis %d:", i);
            lv_label_set_text(a_lbl, axis_name);
            lv_obj_set_pos(a_lbl, 0, y_off + 4);
            lv_obj_set_style_text_color(a_lbl, lv_color_hex(COL_SUBTEXT), 0);
            lv_obj_set_style_text_font(a_lbl, &lv_font_montserrat_42, 0);

            lv_obj_t *bar_bg = lv_obj_create(axis_panel);
            lv_obj_set_pos(bar_bg, LABEL_W, y_off);
            lv_obj_set_size(bar_bg, bar_w, BAR_H);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(COL_CARD), 0);
            lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bar_bg, 1, 0);
            lv_obj_set_style_border_color(bar_bg, lv_color_hex(COL_CARD_BORDER), 0);
            lv_obj_set_style_radius(bar_bg, 4, 0);
            lv_obj_set_style_pad_all(bar_bg, 0, 0);
            lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *bar_ind = lv_obj_create(bar_bg);
            lv_obj_set_size(bar_ind, 8, BAR_H - 6);
            lv_obj_set_pos(bar_ind, bar_w / 2 - 4, 3);
            lv_obj_set_style_bg_color(bar_ind, lv_color_hex(COL_SUBTEXT), 0);
            lv_obj_set_style_bg_opa(bar_ind, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bar_ind, 0, 0);
            lv_obj_set_style_radius(bar_ind, 2, 0);
            lv_obj_clear_flag(bar_ind, LV_OBJ_FLAG_SCROLLABLE);

            ds.axis_bars[i] = bar_ind;

            lv_obj_t *val_lbl = lv_label_create(axis_panel);
            lv_label_set_text(val_lbl, "0");
            lv_obj_set_pos(val_lbl, LABEL_W + bar_w + 8, y_off + 4);
            lv_obj_set_style_text_color(val_lbl, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_42, 0);

            ds.axis_value_labels[i] = val_lbl;
        }
    }

    /* ── Event log (scrollable) ──────────────────────────────── */
    /* Position below buttons/axes — use the taller of the two panels */
    int btn_rows = (num_btns + 4) / 5;   /* 5 columns */
    int btn_panel_total = (num_btns > 0) ? content_y + 52 + btn_rows * 110 + 24 : content_y;
    int axis_panel_total = (num_axes > 0) ? content_y + 52 + num_axes * 70 + 24 : content_y;
    int log_y = ((btn_panel_total > axis_panel_total) ? btn_panel_total : axis_panel_total) + 20;
    int log_h = win_h / 4;

    lv_obj_t *log_title = lv_label_create(ov);
    lv_label_set_text(log_title, "Raw Events");
    lv_obj_set_style_text_color(log_title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(log_title, &lv_font_montserrat_42, 0);
    lv_obj_set_pos(log_title, PAD, log_y);

    lv_obj_t *log_box = lv_obj_create(ov);
    lv_obj_set_pos(log_box, PAD, log_y + 50);
    lv_obj_set_size(log_box, content_w, log_h);
    lv_obj_set_style_bg_color(log_box, lv_color_hex(COL_HEADER), 0);
    lv_obj_set_style_bg_opa(log_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(log_box, 1, 0);
    lv_obj_set_style_border_color(log_box, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_radius(log_box, 8, 0);
    lv_obj_set_style_pad_all(log_box, 8, 0);
    /* Log box is scrollable so long event history is accessible */

    ds.event_log_label = lv_label_create(log_box);
    lv_label_set_text(ds.event_log_label, "Press buttons or move axes...");
    lv_obj_set_width(ds.event_log_label, content_w - 16);
    lv_obj_set_style_text_color(ds.event_log_label, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(ds.event_log_label, &lv_font_montserrat_42, 0);
    lv_obj_align(ds.event_log_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* ── Back button ─────────────────────────────────────────── */
    lv_obj_t *btn = lv_button_create(ov);
    lv_obj_set_size(btn, 280, back_btn_h);
    lv_obj_set_pos(btn, content_w - 280 + PAD, log_y + 50 + log_h + PAD);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(btn, 20, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
    lv_obj_add_event_cb(btn, controller_cfg_back_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Back");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_42, 0);
    lv_obj_center(btn_lbl);

    lv_timer_handler();
    printf("Controller config opened: \"%s\" GUID=%s (%d btns, %d axes, %d hats)\n",
           dev_name, guid_str, num_btns, num_axes, num_hats);
}

void close_controller_config()
{
    if (controller_cfg_overlay) {
        lv_obj_delete(controller_cfg_overlay);
        controller_cfg_overlay = nullptr;
    }
    if (cfg_joystick) {
        SDL_JoystickClose(cfg_joystick);
        cfg_joystick = nullptr;
    }
    controller_cfg_active = false;
    memset(&ds, 0, sizeof(ds));
    lv_obj_invalidate(lv_screen_active());
    printf("Controller config closed\n");
}

/* ── Gamepad navigation within settings screens ──────────────── */

void settings_handle_button(SDL_GameControllerButton btn)
{
    if (controller_cfg_active) {
        /* Use SELECT (Back) to exit controller config — NOT B,
         * because the whole point of this screen is testing ALL buttons
         * including B.  B press is shown as visual feedback instead. */
        if (btn == SDL_CONTROLLER_BUTTON_BACK) {
            close_controller_config();
            open_settings_menu();
        }
        return;
    }

    if (settings_active) {
        switch (btn) {
            case SDL_CONTROLLER_BUTTON_B:
            case SDL_CONTROLLER_BUTTON_BACK:
                close_settings_menu();
                break;
            case SDL_CONTROLLER_BUTTON_A:
                close_settings_menu();
                open_controller_config();
                break;
            default:
                break;
        }
    }
}

/* ── Real-time controller config event handlers ──────────────── */

void controller_cfg_on_joy_button(int button, bool pressed)
{
    if (button < 0 || button >= MAX_DISPLAY_BUTTONS) return;

    /* Update visual indicator if one exists for this index */
    if (button < ds.num_buttons) {
        lv_obj_t *ind = ds.button_indicators[button];
        if (ind) {
            if (pressed) {
                lv_obj_set_style_bg_color(ind, lv_color_hex(COL_ACCENT), 0);
                lv_obj_set_style_border_color(ind, lv_color_white(), 0);
                lv_obj_set_style_shadow_width(ind, 12, 0);
                lv_obj_set_style_shadow_color(ind, lv_color_hex(COL_ACCENT), 0);
                lv_obj_set_style_shadow_opa(ind, LV_OPA_60, 0);
            } else {
                lv_obj_set_style_bg_color(ind, lv_color_hex(COL_CARD), 0);
                lv_obj_set_style_border_color(ind, lv_color_hex(COL_CARD_BORDER), 0);
                lv_obj_set_style_shadow_width(ind, 0, 0);
            }
        }
    }

    /* Always log, even for buttons beyond the visual grid */
    if (ds.is_game_controller && button < n_gc_labels) {
        append_event_log("GC BTN %d %s  [%s]", button,
                         pressed ? "DOWN" : "UP", gc_labels[button]);
    } else {
        append_event_log("BTN %d %s", button, pressed ? "DOWN" : "UP");
    }
}

void controller_cfg_on_axis(int axis, int16_t value)
{
    if (axis < 0 || axis >= ds.num_axes) return;

    lv_obj_t *bar = ds.axis_bars[axis];
    if (!bar) return;

    /* Reposition the indicator within the bar track */
    lv_obj_t *bar_bg = lv_obj_get_parent(bar);
    int bar_w = lv_obj_get_width(bar_bg);
    int pos = ((value + 32768) * (bar_w - 6)) / 65535;
    lv_obj_set_pos(bar, pos, 3);

    /* Color: accent when past deadzone, dim when in deadzone */
    int abs_val = (value < 0) ? -value : value;
    if (abs_val > AXIS_DEADZONE) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(COL_ACCENT), 0);
    } else {
        lv_obj_set_style_bg_color(bar, lv_color_hex(COL_SUBTEXT), 0);
    }

    /* Update numeric value label */
    lv_obj_t *val_lbl = ds.axis_value_labels[axis];
    if (val_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", value);
        lv_label_set_text(val_lbl, buf);
    }

    /* Only log significant movements to avoid flooding */
    if (abs_val > AXIS_DEADZONE) {
        if (ds.is_game_controller && axis < n_gc_axis_labels) {
            append_event_log("GC AXIS %d: %d  [%s]", axis, value,
                             gc_axis_labels[axis]);
        } else {
            append_event_log("AXIS %d: %d", axis, value);
        }
    }
}

void controller_cfg_on_hat(int hat, int value)
{
    const char *dir = "CENTER";
    switch (value) {
        case SDL_HAT_UP:        dir = "UP";        break;
        case SDL_HAT_DOWN:      dir = "DOWN";      break;
        case SDL_HAT_LEFT:      dir = "LEFT";      break;
        case SDL_HAT_RIGHT:     dir = "RIGHT";     break;
        case SDL_HAT_LEFTUP:    dir = "LEFT+UP";   break;
        case SDL_HAT_LEFTDOWN:  dir = "LEFT+DOWN";  break;
        case SDL_HAT_RIGHTUP:   dir = "RIGHT+UP";  break;
        case SDL_HAT_RIGHTDOWN: dir = "RIGHT+DOWN"; break;
        default:                dir = "CENTER";     break;
    }
    append_event_log("Hat %d: %s", hat, dir);
}

/* ── Raw event logging for external callers (main.cpp) ───────── */

void controller_cfg_log_raw_event(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    append_event_log("%s", buf);
}
