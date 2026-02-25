#include "config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

Game games[MAX_GAMES];
int  num_games = 0;

static void trim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                        s[len-1] == ' '  || s[len-1] == '\t'))
        s[--len] = '\0';
}

static void parse_args(Game *game, const char *args_str)
{
    char buf[MAX_STR];
    strncpy(buf, args_str, MAX_STR - 1);
    buf[MAX_STR - 1] = '\0';
    game->num_args = 0;
    char *token = strtok(buf, " ");
    while (token && game->num_args < MAX_ARGS) {
        strncpy(game->args[game->num_args++], token, MAX_STR - 1);
        token = strtok(nullptr, " ");
    }
}

static void write_default_config()
{
    system("mkdir -p /etc/phytec-launcher");
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        fprintf(stderr, "Could not write default config to %s\n", CONFIG_PATH);
        return;
    }
    fprintf(f,
        "# PHYTEC Game Launcher Configuration\n"
        "# Each [game] block defines one application entry.\n"
        "# args= is optional. Use space-separated arguments.\n"
        "\n"
        "[game]\n"
        "name=SuperTuxKart\n"
        "binary=/usr/bin/supertuxkart\n"
        "args=--fullscreen\n"
        "\n"
        "[game]\n"
        "name=Neverball\n"
        "binary=/usr/bin/neverball\n"
        "args=-f\n"
        "\n"
        "[game]\n"
        "name=Neverputt\n"
        "binary=/usr/bin/neverputt\n"
        "args=-f\n"
        "\n"
        "[game]\n"
        "name=RetroArch\n"
        "binary=/usr/bin/retroarch\n"
        "args=-f\n"
    );
    fclose(f);
    printf("Default config written to %s\n", CONFIG_PATH);
}

void load_config()
{
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        printf("No config found, generating default at %s\n", CONFIG_PATH);
        write_default_config();
        f = fopen(CONFIG_PATH, "r");
        if (!f) {
            fprintf(stderr, "Failed to open config\n");
            return;
        }
    }

    char  line[MAX_STR];
    Game *current = nullptr;
    bool  in_game = false;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        if (strcmp(line, "[game]") == 0) {
            if (num_games < MAX_GAMES) {
                current = &games[num_games];
                memset(current, 0, sizeof(Game));
                num_games++;
                in_game = true;
            } else {
                fprintf(stderr, "Max games (%d) reached\n", MAX_GAMES);
                in_game = false;
            }
            continue;
        }

        if (!in_game || !current) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if      (strcmp(key, "name")           == 0) strncpy(current->name,   val, MAX_STR - 1);
        else if (strcmp(key, "binary")         == 0) strncpy(current->binary, val, MAX_STR - 1);
        else if (strcmp(key, "args")           == 0) parse_args(current, val);
        else if (strcmp(key, "killable")       == 0) current->killable       = (strcmp(val, "true") == 0);
        else if (strcmp(key, "capture_output") == 0) current->capture_output = (strcmp(val, "true") == 0);
    }
    fclose(f);
    printf("Loaded %d game(s) from config\n", num_games);

    for (int i = 0; i < num_games; ) {
        if (games[i].binary[0] == '\0' || access(games[i].binary, X_OK) != 0) {
            fprintf(stderr, "Skipping '%s' — not found: %s\n",
                    games[i].name, games[i].binary);
            for (int j = i; j < num_games - 1; j++)
                games[j] = games[j + 1];
            num_games--;
        } else {
            i++;
        }
    }
}
