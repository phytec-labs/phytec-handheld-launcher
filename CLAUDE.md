# CLAUDE.md

## Project Overview

**phytec-handheld-launcher** is an embedded Linux application launcher for PHYTEC handheld devices. It presents a touchscreen/gamepad-navigable card grid of installed apps and launches them as child Wayland processes. While a child app runs, the launcher hides its Weston window and keeps the Wayland event loop alive; when the child exits, the window is restored.

**UI Framework**: LVGL v9 (not v8.x — incompatible)
**Graphics Backend**: SDL2
**Compositor**: Weston (Wayland)
**Language**: C++11/14
**Build System**: CMake 3.16+
**License**: MIT

---

## Source Layout

```
src/
  main.cpp      Entry point; SDL2 + LVGL init; main event loop
  config.cpp    INI config parser (/etc/phytec-launcher/launcher.conf)
  config.h      Game struct definition (MAX_GAMES=12, MAX_ARGS=8, MAX_STR=256)
  ui.cpp        LVGL card grid (3 cols × 2 rows); click callbacks; results overlay
  ui.h          UI constants (COLS, ROWS, HEADER_H, PAD, GAP) and color palette
  launcher.cpp  fork()/execv() launch; kill-button loop; output capture
  launcher.h    Declares launch_game(); OUTPUT_TMP path
  input.cpp     Gamepad init; D-pad navigation; analog stick deadzone/repeat
  input.h       AXIS_DEADZONE=16000, AXIS_REPEAT_MS=250, TOUCH_DEBOUNCE_MS=600
assets/
  loading.png   Loading screen image
```

---

## Build

This project is designed for Yocto/BitBake cross-compilation. Key CMake variables:

- `LVGL_DIR` — path to LVGL headers (e.g. `${STAGING_INCDIR}/lvgl`)
- `LVGL_INCLUDE_PARENT` — parent of LVGL include dir (e.g. `/usr/include`)

**Linked libraries**: `lvgl`, `SDL2`, `drm`, `pthread`, `m`

**Compiler flags**: `-Wall -Wextra -Wno-unused-parameter`
**Default build type**: Release (`-O2`); Debug with `-g -O0`

LVGL must be built with the SDL2 driver enabled:
```
PACKAGECONFIG:append:pn-lvgl = "sdl"
```

---

## Configuration

Config file: `/etc/phytec-launcher/launcher.conf` (auto-generated with defaults on first run)

INI format with `[game]` sections:
```ini
[game]
name=SuperTuxKart
binary=/usr/bin/supertuxkart
args=--fullscreen
killable=true
kill_button=-1       # joystick button index; -1 = unset
capture_output=false # saves stdout/stderr to /tmp/phytec_launcher_output.txt
```

`config.cpp` validates each binary with `access(binary, X_OK)` and skips invalid entries with a warning.

Environment variables (set via systemd unit):
- `WAYLAND_DISPLAY=wayland-0`
- `XDG_RUNTIME_DIR=/run/user/0`

---

## Input

| Method | How |
|--------|-----|
| Touchscreen | Via Weston/Wayland — no direct evdev needed |
| Gamepad D-pad | SDL2 `SDL_JOYBUTTONDOWN` events; navigates grid |
| Analog stick | SDL2 axis events; deadzone 16000/32767 (~50%); 250ms repeat |
| Start button | Kills running child if `killable=true` in config |
| Custom kill button | Configured via `kill_button=N` per game |
| MSPM0 I2C joystick | **Not yet integrated** — see `TODO` markers in `input.cpp`/`input.h` |

---

## Known Limitations

- LVGL v9 required (v8.3 is incompatible — different Wayland driver)
- Weston must be running before launcher starts (`Requires=weston.service`)
- No automated tests — manual integration testing against a real Weston session
- Keyboard navigation (arrow keys + Enter) is described in README but not fully integrated

---

## Coding Conventions

- `#pragma once` for header guards
- `extern` globals for SDL/LVGL state shared across modules (pragmatic for graphics frameworks)
- Static functions for module-internal helpers (e.g. `trim`, `parse_args`, `card_click_cb`)
- Dynamic allocation via `new`/`delete` for LVGL display buffers
- Process management: `fork()` + `waitpid(WNOHANG)` polling loop; `SIGTERM` then `SIGKILL` for kills
- HW renderer → SW renderer fallback in `main.cpp`
- Log to `stdout`/`stderr` (not a logging framework) at key lifecycle points
