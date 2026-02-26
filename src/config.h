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
};

extern Game games[MAX_GAMES];
extern int  num_games;

void load_config();
