#pragma once
#include "config.h"
#include <signal.h>

#define OUTPUT_TMP "/tmp/phytec_launcher_output.txt"

/* Set by SIGTERM/SIGINT handler; checked in main loop and wait loop */
extern volatile sig_atomic_t quit_requested;

void launch_game(const Game *game);
