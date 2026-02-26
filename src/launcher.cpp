#include "launcher.h"
#include "ui.h"
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <SDL2/SDL.h>

extern SDL_Window *sdl_window;
extern Uint32      resume_time;
extern bool        touch_pressed;

void launch_game(const Game *game)
{
    printf("Launching: %s\n", game->binary);
    SDL_HideWindow(sdl_window);

    int capture_fd = -1;
    if (game->capture_output) {
        capture_fd = open(OUTPUT_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (capture_fd < 0) perror("Could not open capture file");
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        if (capture_fd >= 0) close(capture_fd);
        SDL_ShowWindow(sdl_window);
        return;
    }

    if (pid == 0) {
        if (capture_fd >= 0) {
            dup2(capture_fd, STDOUT_FILENO);
            dup2(capture_fd, STDERR_FILENO);
            close(capture_fd);
        }
        const char *argv[MAX_ARGS + 2];
        int argc = 0;
        argv[argc++] = game->binary;
        for (int i = 0; i < game->num_args && argc < MAX_ARGS + 1; i++)
            argv[argc++] = game->args[i];
        argv[argc] = nullptr;
        execv(game->binary, const_cast<char *const *>(argv));
        perror("execv failed");
        _exit(1);
    }

    if (capture_fd >= 0) close(capture_fd);

    /* Flush the button/touch event that launched this game so it
     * doesn't appear as a spurious kill event in the wait loop */
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_Delay(200);

    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);

    printf("Entering wait loop for pid %d (killable=%d, kill_button=%d)\n",
           pid, game->killable, game->kill_button);

    bool child_running = true;
    while (child_running) {
        int   status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            child_running = false;
            printf("Game exited naturally (status %d)\n", WEXITSTATUS(status));
            break;
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                printf("Controller button DOWN: %d\n", ev.cbutton.button);
            } else if (ev.type == SDL_JOYBUTTONDOWN) {
                printf("Joystick button DOWN: %d\n", ev.jbutton.button);
            }

            /* Controller path */
            if (ev.type == SDL_CONTROLLERBUTTONDOWN && game->killable) {
                auto btn = static_cast<SDL_GameControllerButton>(ev.cbutton.button);
                if (btn == SDL_CONTROLLER_BUTTON_START) {
                    printf("Controller START — killing pid %d\n", pid);
                    goto do_kill;
                }
            }

            /* Joystick path — used when SDL maps buttons as joystick only */
            if (ev.type == SDL_JOYBUTTONDOWN && game->killable) {
                printf("Joystick button %d pressed (kill_button=%d)\n",
                       ev.jbutton.button, game->kill_button);
                if (game->kill_button >= 0 &&
                    ev.jbutton.button == (Uint8)game->kill_button) {
                    printf("Kill button matched — killing pid %d\n", pid);
                    goto do_kill;
                }
            }
            continue;

            do_kill:
            kill(pid, SIGTERM);
            SDL_Delay(2000);
            if (waitpid(pid, &status, WNOHANG) != pid) {
                printf("No graceful exit, sending SIGKILL\n");
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
            child_running = false;
            break;
        }

        if (child_running) SDL_Delay(100);
    }

    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    touch_pressed = false;
    resume_time   = SDL_GetTicks();

    SDL_Delay(300);
    SDL_ShowWindow(sdl_window);
    SDL_RaiseWindow(sdl_window);

    if (game->capture_output) {
        char output[8192] = {0};
        FILE *f = fopen(OUTPUT_TMP, "r");
        if (f) {
            size_t bytes = fread(output, 1, sizeof(output) - 1, f);
            output[bytes] = '\0';
            fclose(f);
        }
        show_results(game->name, output);
    } else {
        redraw_ui();
    }
}
