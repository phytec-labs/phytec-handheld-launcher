# CLAUDE.md

## Project Overview

**phytec-handheld-launcher** is an embedded Linux application launcher for PHYTEC Handheld-One devices. It presents a touchscreen/gamepad-navigable card grid of installed apps and launches them as child Wayland processes. While a child app runs, the launcher hides its Weston window and keeps the Wayland event loop alive; when the child exits, the window is restored.

**UI Framework**: LVGL v9.1 (not v8.x — incompatible)
**Graphics Backend**: SDL2
**Compositor**: Weston (Wayland)
**Language**: C++11/14
**Build System**: Yocto/BitBake (direct `${CXX}` compile, no CMake)
**Target Displays**: 1920×1080, 1280×720
**License**: MIT

---

## Source Layout

```
src/
  main.cpp      Entry point; SDL2 + LVGL init; main event loop
  config.cpp    INI config parser (/etc/phytec-launcher/launcher.conf)
  config.h      Game struct (MAX_GAMES=12, MAX_ARGS=8, MAX_STR=256)
  ui.cpp        LVGL card grid (3 cols × 2 visible rows); cover art; scroll; results overlay
  ui.h          Layout constants (COLS, ROWS, HEADER_H, PAD, GAP) and color palette
  launcher.cpp  fork()/execv() launch; kill-button loop; output capture
  launcher.h    Declares launch_game(); OUTPUT_TMP path
  input.cpp     Gamepad init; D-pad navigation; analog stick deadzone/repeat
  input.h       AXIS_DEADZONE=16000, AXIS_REPEAT_MS=250, TOUCH_DEBOUNCE_MS=600
assets/
  loading.png   Loading screen image
```

---

## Build

This project is built via a Yocto BitBake recipe (`phytec-launcher_1.0.bb`). The recipe compiles directly with `${CXX}` — no CMake.

**Linked libraries**: `lvgl`, `SDL2`, `libpng`, `drm`, `m`

**Critical include path**: `-I${STAGING_INCDIR}/lvgl` is required so the compiler finds `lv_conf.h`. Without it, LVGL falls back to internal defaults and custom features (fonts, PNG decoder, FS driver) are silently unavailable.

LVGL must be built with the SDL2 driver enabled:
```
PACKAGECONFIG:append:pn-lvgl = "sdl"
```

### LVGL bbappend (`lvgl_9.1.0.bbappend`)

The launcher requires several LVGL features not enabled by default. A bbappend patches `lv_conf.h` via sed:

| Define | Value | Why |
|--------|-------|-----|
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CLIB` | Use system malloc (default 64KB pool can't hold decoded PNGs) |
| `LV_USE_FS_POSIX` | `1` | Load images from filesystem at runtime |
| `LV_FS_POSIX_LETTER` | `'A'` | Drive letter prefix for LVGL paths (e.g. `"A:/usr/share/..."`) |
| `LV_FS_POSIX_PATH` | `"/"` | Maps drive letter to filesystem root |
| `LV_USE_LIBPNG` | `1` | PNG decoder (requires `libpng` in DEPENDS) |
| `LV_FONT_MONTSERRAT_20` | `1` | Larger font for header title |
| `LV_CACHE_DEF_SIZE` | `(4 * 1024 * 1024)` | 4MB image cache (default 0 = re-decode every frame) |
| `LV_IMAGE_HEADER_CACHE_DEF_CNT` | `32` | Image header cache slots |

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
icon=/usr/share/phytec-launcher/covers/supertuxkart.png
```

`config.cpp` validates each binary with `access(binary, X_OK)` and skips invalid entries with a warning.

Environment variables (set via systemd unit):
- `WAYLAND_DISPLAY=wayland-0`
- `XDG_RUNTIME_DIR=/run/user/0`

### Cover Art

- **Config**: `icon=` field per game — absolute path to a PNG file
- **Deployment**: Covers are layer-specific, not in the launcher source repo. A `covers.inc` file handles installation from the recipe's `files/covers/` directory
- **LVGL path**: Code prepends `"A:"` to the absolute path for the POSIX FS driver
- **Scaling**: Images are scaled at runtime to fill the card using `lv_image_set_scale()`. This is performant because decoded pixel data is held in the LVGL image cache (no per-frame PNG decode)
- **Fallback**: Cards without a valid `icon=` show a centered text label instead

---

## Input

| Method | How |
|--------|-----|
| Touchscreen | Via Weston/Wayland — no direct evdev needed; scrollable grid |
| Gamepad D-pad | SDL2 `SDL_CONTROLLERBUTTONDOWN` events; navigates grid with auto-scroll |
| Analog stick | SDL2 axis events; deadzone 16000/32767 (~50%); 250ms repeat |
| Start button | Kills running child if `killable=true` in config |
| Custom kill button | Configured via `kill_button=N` per game |
| MSPM0 I2C joystick | **Not yet integrated** — see `TODO` markers in `input.cpp`/`input.h` |

When the grid has more than 6 apps (3 cols × 2 visible rows), additional rows scroll. D-pad/analog navigation calls `lv_obj_scroll_to_view()` to auto-scroll to off-screen cards.

---

## Yocto Integration

### Recipe structure (in the meta layer, not this repo)

```
recipes-graphics/
  lvgl/
    lvgl_9.1.0.bbappend         # FS_POSIX, libpng, system malloc, fonts, cache
  phytec-launcher/
    phytec-launcher_1.0.bb       # Main recipe; sets LAUNCHER_COVERS; requires covers.inc
    covers.inc                   # do_install:append for cover art PNGs
    files/
      launcher.conf              # Deployed config with icon= paths
      phytec-launcher.service    # systemd unit
      phytec-launcher-start.sh   # Startup wrapper
      covers/
        supertuxkart.png         # Cover images (layer-specific)
        neverball.png
```

### Adding a new cover

1. Drop a PNG into `files/covers/`
2. Add `file://covers/<name>.png \` to `LAUNCHER_COVERS` in the `.bb`
3. Add `icon=/usr/share/phytec-launcher/covers/<name>.png` to `files/launcher.conf`

---

## Known Limitations

- LVGL v9 required (v8.3 is incompatible — different Wayland driver)
- Weston must be running before launcher starts (`Requires=weston.service`)
- No automated tests — manual integration testing against a real Weston session
- Keyboard navigation (arrow keys + Enter) is described in README but not fully integrated
- The `lv_conf.h` pragma warning ("Possible failure to include lv_conf.h") appears if `-I${STAGING_INCDIR}/lvgl` is missing from the compile flags

---

## Coding Conventions

- `#pragma once` for header guards
- `extern` globals for SDL/LVGL state shared across modules (pragmatic for graphics frameworks)
- Static functions for module-internal helpers (e.g. `trim`, `parse_args`, `card_click_cb`)
- Dynamic allocation via `new`/`delete` for LVGL display buffers
- Process management: `fork()` + `waitpid(WNOHANG)` polling loop; `SIGTERM` then `SIGKILL` for kills
- HW renderer → SW renderer fallback in `main.cpp`
- Log to `stdout`/`stderr` (not a logging framework) at key lifecycle points
- Card dimensions logged at startup: `Card size: WxH` — useful for sizing cover art
