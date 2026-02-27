#include "ui.h"
#include "config.h"
#include "launcher.h"
#include "input.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <SDL2/SDL.h>

extern int win_w;
extern int win_h;

int       selected_index = 0;
lv_obj_t *cards[MAX_GAMES];
bool      results_active = false;
static lv_obj_t *results_overlay = nullptr;

void redraw_ui()
{
    lv_obj_invalidate(lv_screen_active());
}

void close_results()
{
    if (results_overlay) {
        lv_obj_delete(results_overlay);
        results_overlay = nullptr;
    }
    results_active = false;
    lv_obj_invalidate(lv_screen_active());
}

static void close_results_cb(lv_event_t *e)
{
    close_results();
}

void show_results(const char *app_name, const char *output)
{
    results_active  = true;
    lv_obj_t *scr   = lv_screen_active();

    results_overlay = lv_obj_create(scr);
    lv_obj_t *overlay = results_overlay;

    lv_obj_set_size(overlay, win_w, win_h);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, PAD, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(overlay);
    char title_buf[MAX_STR];
    snprintf(title_buf, sizeof(title_buf), "%s — Results", app_name);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *hint = lv_label_create(overlay);
    lv_label_set_text(hint, "A: Close");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *ta = lv_textarea_create(overlay);
    lv_obj_set_size(ta, win_w - PAD * 2, win_h - HEADER_H - PAD * 3 - 50);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_bg_color(ta, lv_color_hex(COL_HEADER), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_textarea_set_text(ta, output && output[0] ? output : "(no output captured)");
    lv_textarea_set_cursor_click_pos(ta, false);
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);

    /* Close button — highlighted to show it has focus */
    lv_obj_t *btn = lv_button_create(overlay);
    lv_obj_set_size(btn, 160, 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(btn, 20, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
    lv_obj_add_event_cb(btn, close_results_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Back to Launcher");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);

    lv_timer_handler();
}

void update_selection(int new_index)
{
    if (new_index < 0 || new_index >= num_games) return;

    lv_obj_set_style_border_width(cards[selected_index], 1, 0);
    lv_obj_set_style_border_color(cards[selected_index],
                                  lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_shadow_width(cards[selected_index], 0, 0);
    lv_obj_set_style_bg_color(cards[selected_index],
                              lv_color_hex(COL_CARD), 0);

    selected_index = new_index;

    lv_obj_set_style_border_width(cards[selected_index], 3, 0);
    lv_obj_set_style_border_color(cards[selected_index],
                                  lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_width(cards[selected_index], 30, 0);
    lv_obj_set_style_shadow_color(cards[selected_index],
                                  lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_opa(cards[selected_index], LV_OPA_30, 0);
    lv_obj_set_style_bg_color(cards[selected_index],
                              lv_color_hex(0x1a2744), 0);

    /* Scroll the screen so the selected card is visible (for rows beyond the viewport) */
    lv_obj_scroll_to_view(cards[selected_index], LV_ANIM_ON);
}

static void card_click_cb(lv_event_t *e)
{
    if (SDL_GetTicks() - resume_time < TOUCH_DEBOUNCE_MS) return;
    const Game *game = static_cast<const Game *>(lv_event_get_user_data(e));
    launch_game(game);
}

void build_ui()
{
    const int CARD_W = (win_w - PAD * 2 - GAP * (COLS - 1)) / COLS;
    const int CARD_H = (win_h - HEADER_H - PAD * 2 - GAP * (ROWS - 1)) / ROWS;
    printf("Card size: %dx%d — pre-scale cover art to this resolution\n", CARD_W, CARD_H);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, win_w, HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COL_HEADER), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, PAD, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "PHYTEC Handheld-One");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *hint = lv_label_create(header);
    lv_label_set_text(hint, "D-Pad: Navigate     A: Launch");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_SUBTEXT), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, 0, 0);

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
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(card, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, &games[i]);

        bool has_cover = (games[i].icon[0] != '\0' && access(games[i].icon, R_OK) == 0);

        if (has_cover) {
            /* Full-bleed cover art with a name strip at the bottom */
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_set_style_clip_corner(card, true, 0);

            char lvgl_path[MAX_STR + 2];
            snprintf(lvgl_path, sizeof(lvgl_path), "A:%s", games[i].icon);

            lv_obj_t *img = lv_image_create(card);
            lv_image_set_src(img, lvgl_path);
            lv_obj_center(img);

            /* Semi-transparent name strip (scales with card height) */
            int32_t strip_h = CARD_H / 6;
            if (strip_h < 28) strip_h = 28;

            lv_obj_t *name_bg = lv_obj_create(card);
            lv_obj_set_size(name_bg, CARD_W, strip_h);
            lv_obj_align(name_bg, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_bg_color(name_bg, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(name_bg, LV_OPA_70, 0);
            lv_obj_set_style_border_width(name_bg, 0, 0);
            lv_obj_set_style_radius(name_bg, 0, 0);
            lv_obj_set_style_pad_all(name_bg, 4, 0);
            lv_obj_clear_flag(name_bg, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *name_lbl = lv_label_create(name_bg);
            lv_label_set_text(name_lbl, games[i].name);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
            lv_obj_set_width(name_lbl, CARD_W - 8);
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(name_lbl, LV_ALIGN_CENTER, 0, 0);
        } else {
            /* Text-only fallback */
            lv_obj_t *name_lbl = lv_label_create(card);
            lv_label_set_text(name_lbl, games[i].name);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(name_lbl, CARD_W - 24);
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(name_lbl, LV_ALIGN_CENTER, 0, 0);
        }
    }

    update_selection(0);
}
