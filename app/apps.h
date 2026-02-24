/**
 * apps.h
 * Define the applications shown in the launcher.
 *
 * To add a new app:
 *   1. Add an entry to LAUNCHER_APPS below.
 *   2. Set .binary_path to the full path of the executable.
 *   3. Set .argv[] to the argument list (argv[0] must be the binary path,
 *      last element must be NULL).
 *   4. Optionally set .icon_symbol to an LV_SYMBOL_* string, or NULL.
 *
 * The launcher supports up to LAUNCHER_MAX_APPS entries.
 */

#ifndef APPS_H
#define APPS_H

#include "lvgl/lvgl.h"

#define LAUNCHER_MAX_APPS 8

typedef struct {
    const char  *name;           /* Display name on the card          */
    const char  *icon_symbol;    /* LVGL built-in symbol or NULL       */
    const char  *binary_path;    /* Full path to the executable        */
    const char  *argv[8];        /* Argument list, NULL-terminated     */
} launcher_app_t;

/*
 * --- App List ---
 * Add or remove entries here to change what appears in the launcher.
 * Keep the final sentinel entry { NULL } at the end.
 */
static const launcher_app_t LAUNCHER_APPS[] = {
    {
        .name        = "Doom",
        .icon_symbol = LV_SYMBOL_PLAY,
        .binary_path = "/usr/bin/chocolate-doom",
        .argv        = { "/usr/bin/chocolate-doom", NULL },
    },
    {
        .name        = "Neverball",
        .icon_symbol = LV_SYMBOL_LOOP,
        .binary_path = "/usr/bin/neverball",
        .argv        = { "/usr/bin/neverball", NULL },
    },
    {
        .name        = "SuperTuxKart",
        .icon_symbol = LV_SYMBOL_DRIVE,
        .binary_path = "/usr/bin/supertuxkart",
        .argv        = { "/usr/bin/supertuxkart", NULL },
    },

    /* Sentinel — do not remove */
    { NULL }
};

#endif /* APPS_H */
