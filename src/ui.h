#pragma once

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

/* Layout constants shared across files */
#define COLS      3
#define ROWS      2
#define HEADER_H  54
#define PAD       20
#define GAP       16

/* Color palette */
#define COL_BG          0x0d1117
#define COL_HEADER      0x161b22
#define COL_CARD        0x1c2333
#define COL_CARD_BORDER 0x30363d
#define COL_ACCENT      0x58a6ff
#define COL_TEXT        0xe6edf3
#define COL_SUBTEXT     0x8b949e
#define COL_PRESSED     0x0d419d

extern int selected_index;

void build_ui();
void update_selection(int new_index);
void show_results(const char *app_name, const char *output);
void redraw_ui();
