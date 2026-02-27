#include "launcher.h"
#include "ui.h"
#include "input.h"
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <SDL2/SDL.h>

extern SDL_Window   *sdl_window;
extern SDL_Renderer *sdl_renderer;
extern SDL_Texture  *sdl_texture;
extern int           win_w;
extern int           win_h;
extern Uint32        resume_time;
extern bool          touch_pressed;

void launch_game(const Game *game)
{
    printf("Launching: %s\n", game->binary);
    SDL_HideWindow(sdl_window);

    int capture_fd = -1;
    if (game->capture_output) {
        capture_fd = open(OUTPUT_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (capture_fd < 0) perror("Could not open capture file");
    }

    /* Close the gamepad BEFORE fork so the child does not inherit the
     * joystick device file descriptors.  If the child (or its SDL init)
     * grabs exclusive access to the device, the parent would never see
     * button events again.  We reopen after fork. */
    if (sdl_gamepad) {
        SDL_GameControllerClose(sdl_gamepad);
        sdl_gamepad = nullptr;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        if (capture_fd >= 0) close(capture_fd);
        init_gamepad();
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

    /* Reopen the gamepad with a fresh FD that the child cannot see.
     * Also try opening any raw joystick that is not a mapped game
     * controller so the home_button index can still be detected. */
    init_gamepad();
    SDL_Joystick *wait_joy = nullptr;
    if (!sdl_gamepad) {
        int n = SDL_NumJoysticks();
        for (int i = 0; i < n; i++) {
            if (!SDL_IsGameController(i)) {
                wait_joy = SDL_JoystickOpen(i);
                if (wait_joy) {
                    printf("Opened raw joystick %d for home button\n", i);
                    break;
                }
            }
        }
    }

    /* Free GPU-accessible texture memory so the child process has full access
     * to shared CPU/GPU memory (critical for GPU benchmarks and games).
     * The LVGL image cache is also evicted — it will be repopulated naturally
     * on the first redraw after the child exits. */
    SDL_DestroyTexture(sdl_texture);
    sdl_texture = nullptr;
    for (int i = 0; i < num_games; i++) {
        if (games[i].icon[0] != '\0') {
            char lvgl_path[MAX_STR + 2];
            snprintf(lvgl_path, sizeof(lvgl_path), "A:%s", games[i].icon);
            lv_image_cache_drop(lvgl_path);
        }
    }

    printf("Entering wait loop for pid %d (home_button=%d)\n",
           pid, home_button);

    bool child_running = true;
    while (child_running) {
        int   status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            child_running = false;
            printf("Game exited naturally (status %d)\n", WEXITSTATUS(status));
            break;
        }

        bool do_kill = false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_CONTROLLERDEVICEADDED:
                    if (!sdl_gamepad) {
                        sdl_gamepad = SDL_GameControllerOpen(ev.cdevice.which);
                        if (sdl_gamepad)
                            printf("Gamepad reconnected: %s\n",
                                   SDL_GameControllerName(sdl_gamepad));
                    }
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (sdl_gamepad &&
                        SDL_GameControllerFromInstanceID(ev.cdevice.which) == sdl_gamepad) {
                        SDL_GameControllerClose(sdl_gamepad);
                        sdl_gamepad = nullptr;
                        printf("Gamepad disconnected during child\n");
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    printf("Controller button %d\n", ev.cbutton.button);
                    break;
                case SDL_JOYBUTTONDOWN:
                    printf("Joystick button %d (home_button=%d)\n",
                           ev.jbutton.button, home_button);
                    if (home_button >= 0 &&
                        ev.jbutton.button == (Uint8)home_button)
                        do_kill = true;
                    break;
                default:
                    break;
            }
            if (do_kill) break;
        }

        if (do_kill) {
            printf("Home button pressed — killing pid %d\n", pid);
            kill(pid, SIGTERM);
            SDL_Delay(2000);
            if (waitpid(pid, &status, WNOHANG) != pid) {
                printf("No graceful exit, sending SIGKILL\n");
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
            child_running = false;
        }

        if (child_running) SDL_Delay(100);
    }

    if (wait_joy) {
        SDL_JoystickClose(wait_joy);
        wait_joy = nullptr;
    }

    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    touch_pressed = false;
    resume_time   = SDL_GetTicks();

    /* Recreate the streaming texture before restoring the window so
     * flush_cb has a valid target when LVGL redraws. */
    sdl_texture = SDL_CreateTexture(
        sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        win_w, win_h
    );
    if (!sdl_texture)
        fprintf(stderr, "SDL_CreateTexture failed on resume: %s\n", SDL_GetError());

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
