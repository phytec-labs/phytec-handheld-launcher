#pragma once

#define MAX_GAMES   12
#define MAX_ARGS     8
#define MAX_STR    256
#define CONFIG_PATH "/etc/phytec-launcher/launcher.conf"

struct Game {
    char name[MAX_STR];
    char binary[MAX_STR];
    char args[MAX_ARGS][MAX_STR];
    int  num_args;
    bool killable;
    int  kill_button;     /* joystick button number to kill the process, -1 = unset */
    bool capture_output;
    char icon[MAX_STR];   /* absolute path to PNG cover art; empty = text fallback */
};

extern Game games[MAX_GAMES];
extern int  num_games;
extern int  home_button;  /* raw joystick button index that always kills the child, -1 = disabled */

void load_config();
