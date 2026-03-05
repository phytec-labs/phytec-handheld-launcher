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
  main.cpp      Entry point; SDL2 + LVGL init; --input-debug CLI; main event loop
  config.cpp    INI config parser (/etc/phytec-launcher/launcher.conf)
  config.h      Game struct (MAX_GAMES=12, MAX_ARGS=8, MAX_STR=256); input_debug flag
  ui.cpp        LVGL card grid (3 cols × 2 visible rows); cover art; scroll; results overlay
  ui.h          Layout constants (COLS, ROWS, HEADER_H, PAD, GAP) and color palette
  launcher.cpp  fork()/execv() launch; home-button wait loop; output capture
  launcher.h    Declares launch_game(); OUTPUT_TMP path
  input.cpp     Gamepad init; D-pad navigation; analog stick deadzone/repeat
  input.h       AXIS_DEADZONE=16000, AXIS_REPEAT_MS=250, TOUCH_DEBOUNCE_MS=600; input_debug_log()
  settings.cpp  Settings menu + controller configuration screen (two-level overlay)
  settings.h    State flags (settings_active, controller_cfg_active); settings/controller API
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

INI format with a `[launcher]` global section and `[game]` sections:
```ini
[launcher]
home_button=8        # raw SDL joystick button index that kills any running
                     # child and returns to launcher; -1 = disabled
                     # use --input-debug to discover the correct index

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

Environment variables (set via `phytec-launcher-start.sh`, inherited by child processes):
- `WAYLAND_DISPLAY=/run/wayland-0`
- `XDG_RUNTIME_DIR=/run/user/0`
- `SDL_GAMECONTROLLERCONFIG=...` — MSPM0 gamepad mapping (see [SDL_GAMECONTROLLERCONFIG section](#sdl_gamecontrollerconfig-mspm0-mapping))

### Cover Art

- **Config**: `icon=` field per game — absolute path to a PNG file
- **Deployment**: Covers are layer-specific, not in the launcher source repo. A `covers.inc` file handles installation from the recipe's `files/covers/` directory
- **LVGL path**: Code prepends `"A:"` to the absolute path for the POSIX FS driver
- **Scaling**: Images are scaled at runtime to fill the card using `lv_image_set_scale()`. This is performant because decoded pixel data is held in the LVGL image cache (no per-frame PNG decode)
- **Fallback**: Cards without a valid `icon=` show a centered text label instead

---

## Input

### SDL2 dual API model

SDL2 exposes two joystick APIs. Understanding which path a device takes is critical:

- **GameController** (high-level): Devices in SDL2's internal database (Xbox, PS4, etc.) are opened via `SDL_GameControllerOpen()` and fire `SDL_CONTROLLERBUTTONDOWN` events with **named enum values** (`SDL_CONTROLLER_BUTTON_A`, `SDL_CONTROLLER_BUTTON_DPAD_UP`, etc.). These drive grid navigation in the main loop.
- **Joystick** (low-level): Devices **not** in the database are opened via `SDL_JoystickOpen()` and fire `SDL_JOYBUTTONDOWN` events with **0-based sequential button indices**. The home button kill mechanism uses this API.

The button index in `SDL_JOYBUTTONDOWN` is **not** the same as the kernel evtest code — SDL assigns sequential indices based on the driver's registered capabilities.

### Input methods

| Method | How |
|--------|-----|
| Touchscreen | Via Weston/Wayland — no direct evdev needed; scrollable grid |
| Gamepad D-pad | SDL2 `SDL_CONTROLLERBUTTONDOWN` events; navigates grid with auto-scroll |
| Analog stick | SDL2 axis events; deadzone 16000/32767 (~50%); 250ms repeat |
| Home button | Global `home_button=N` in `[launcher]` config; matches `SDL_JOYBUTTONDOWN` index; kills any running child |
| MSPM0 I2C gamepad | Kernel driver (`phyhandheld.c`) exposes as standard evdev gamepad; requires `SDL_GAMECONTROLLERCONFIG` mapping to be recognized as a GameController by SDL2 |

When the grid has more than 6 apps (3 cols × 2 visible rows), additional rows scroll. D-pad/analog navigation calls `lv_obj_scroll_to_view()` to auto-scroll to off-screen cards.

### Home button (child kill)

The `[launcher]` section's `home_button=N` sets a global raw joystick button index. While a child app is running, the wait loop in `launcher.cpp` polls `SDL_JOYBUTTONDOWN` events and kills the child (SIGTERM → SIGKILL) when the button matches. The value `N` is the SDL joystick button index, discoverable via `--input-debug` mode.

For devices recognized as GameControllers (e.g. Xbox), SDL also emits underlying `SDL_JOYBUTTONDOWN` events, so the home button works for both mapped and unmapped controllers.

### Input debug mode

Run with `--input-debug` to log **all** SDL input events to both stdout and `/tmp/phytec_launcher_input.log`:

```bash
phytec-launcher --input-debug
```

This mode:
1. Enumerates all SDL joystick devices at startup — prints name, GameController yes/no, **SDL2 GUID** (32-char hex), button/axis/hat counts, and existing mapping string (if GameController)
2. Logs every button press, axis movement, hat change, and touch event in the main loop with the SDL event type and index
3. Logs all events in the wait loop (while a child is running), marking home button matches with `*** MATCH — KILL ***`

Use this to:
- Discover the correct `home_button=` index for any controller
- Discover the SDL2 GUID needed for `SDL_GAMECONTROLLERCONFIG` mapping strings
- Verify button/axis mapping for new or custom controllers

The log file persists at `/tmp/phytec_launcher_input.log` for review after testing.

Example startup output:
```
[input-debug]   [0] "PHYTEC Handheld One Gamepad"  GameController=NO  GUID=180000001234000032340000010000
```

### Dual-API event deduplication

GameController devices fire **both** `SDL_CONTROLLERBUTTONDOWN` (mapped enum indices) and `SDL_JOYBUTTONDOWN` (raw sequential indices) — with **different numbering**. To prevent duplicate events in the controller config screen, `main.cpp` checks `SDL_JoystickInstanceID()` against the GameController's underlying joystick and skips raw `SDL_JOY*` events for devices already handled as GameControllers.

### MSPM0 I2C gamepad (kernel driver)

The PHYTEC Handheld-One has an onboard TI MSPM0L1306 microcontroller that acts as an I2C slave, sending a raw GPIO button bitmap (16-bit) and 4 ADC axis values (16-bit each) to the AM62x SoC. The Linux kernel driver (`drivers/input/joystick/phyhandheld.c`) translates these I2C bytes into standard Linux input events on `/dev/input/eventX`.

**Input event chain**: MSPM0 hardware → I2C bytes → kernel driver (`phyhandheld.c`) → `/dev/input/eventX` → SDL2 → launcher/RetroArch

The driver is **not** in this repo — it lives in the kernel source tree. The launcher source repo only contains the userspace SDL2 code.

#### Kernel driver design decisions

| Decision | Rationale |
|----------|-----------|
| D-pad as `ABS_HAT0X`/`ABS_HAT0Y` axes | Industry standard — used by Xbox (xpad), PS4/PS5 (hid-playstation), Nintendo Pro (hid-nintendo), and generic HID gamepads. PS3 (hid-sony with `BTN_DPAD_*`) is the only major outlier. Hat axes are what SDL2 expects for D-pad-to-GameController button translation |
| `BTN_MODE` for home button | Standard gamepad guide/home button (code `0x13c`). `KEY_HOME` (code 102) is a keyboard keycode in the 1–255 range — invisible to SDL2's joystick subsystem which only scans `BTN_*` codes (`0x100`+) |
| VID `0x3432` / PID `0x0001` / version `0x0100` | Non-zero identity allows SDL2 to build a stable GUID for `SDL_GAMECONTROLLERCONFIG` mapping. Without VID/PID, SDL2 falls back to the device name for GUID construction which is fragile |
| `BUS_I2C` bus type | Reflects the actual hardware bus; appears in the SDL2 GUID |

#### Kernel driver button mapping

```
Scan code          → Linux keycode     → SDL2 role
PHYHANDHELD_R_BUT  → BTN_TR            → Right trigger
PHYHANDHELD_R_BUT1 → BTN_TL            → Left trigger
PHYHANDHELD_BUT    → BTN_NORTH         → Y button
PHYHANDHELD_BUT1   → BTN_EAST          → B button
PHYHANDHELD_BUT2   → BTN_WEST          → X button
PHYHANDHELD_BUT3   → BTN_SOUTH         → A button
PHYHANDHELD_BUT4   → ABS_HAT0Y = +1    → D-pad Down
PHYHANDHELD_BUT5   → ABS_HAT0X = +1    → D-pad Right
PHYHANDHELD_BUT6   → ABS_HAT0Y = -1    → D-pad Up
PHYHANDHELD_BUT7   → ABS_HAT0X = -1    → D-pad Left
PHYHANDHELD_BUT8   → BTN_MODE          → Home/Guide button
PHYHANDHELD_BUT9   → BTN_SELECT        → Select/Back
PHYHANDHELD_BUT10  → BTN_START         → Start
```

#### Kernel driver axes

| Register | Axis | Notes |
|----------|------|-------|
| `0x04` | `ABS_X` (left stick X) | 12-bit ADC, 0–4095 |
| `0x05` | `ABS_Y` (left stick Y) | 12-bit ADC, 0–4095 |
| `0x06` | `ABS_RX` (right stick X) | 12-bit ADC, 0–4095 |
| `0x07` | `ABS_RY` (right stick Y) | 12-bit ADC, 0–4095 |
| D-pad | `ABS_HAT0X`, `ABS_HAT0Y` | -1, 0, +1 (digital, from button bitmap) |

#### Kernel driver source location

```
/path/to/kernel-source/drivers/input/joystick/phyhandheld.c
```

In the Yocto build tree:
```
am62x-meta-handheld-one/build/tmp-ampliphy/work-shared/phyboard-lyra-handheld-am62xx-3/kernel-source/drivers/input/joystick/phyhandheld.c
```

#### MSPM0 firmware

Firmware source: https://github.com/phytec-labs/phytec-handheld-mspm0-driver

The MSPM0 firmware is a separate concern from the kernel driver. Modifying the firmware changes what raw bytes are sent over I2C; modifying the kernel driver changes how those bytes are translated into Linux input events. For gamepad compatibility, **kernel driver changes are preferred** because they affect all applications at the `/dev/input/eventX` level.

---

## Settings Screen

Accessible via the gear icon (⚙) in the header bar or the SELECT button on a gamepad.

### Architecture

Two-level full-screen overlay system following the same pattern as `results_overlay` in `ui.cpp`:

1. **Level 1 — Settings Menu**: Title, "Controller Configuration" card item, "Back to Launcher" button. Navigation: touch/tap, A to select, B or SELECT to close.

2. **Level 2 — Controller Configuration**: Real-time visual feedback for all controller inputs. Navigation: touch/tap, SELECT to return to settings menu (B is deliberately not used as exit so it can be tested as a button input).

State flags (`settings_active`, `controller_cfg_active`) gate input routing in `main.cpp` — when either overlay is active, normal grid navigation is blocked.

### Controller Configuration screen

Displays real-time controller state for debugging and future button remapping:

- **Device info**: Name, type (GameController vs Raw Joystick), button/axis/hat counts
- **Button indicators**: Numbered circles in a 6-column grid. Default: dark card color. Pressed: blue accent with white border and glow shadow. Labels show either symbolic names (A, B, X, Y, LB, ↑, etc.) or raw numeric indices
- **Index/Symbols toggle**: Touchable button next to the "Buttons" section title (GameController only). Switches all button labels between named symbols and raw SDL indices. Persists across open/close cycles. Essential for discovering the correct `home_button=` index
- **Axis bars**: Horizontal tracks with sliding indicators. Color changes at the deadzone threshold (dim → accent). Numeric value displayed to the right
- **Event log**: Ring buffer of the last 8 events, rebuilt on each new event. Shows button presses/releases, axis values, and hat directions
- **Hat events**: Logged with directional names (UP, DOWN, LEFT, RIGHT, and diagonals)

### GameController D-pad handling

SDL2's GameController API maps the D-pad hat (kernel `ABS_HAT0X`/`ABS_HAT0Y`) to virtual buttons 11–14 (`SDL_CONTROLLER_BUTTON_DPAD_UP` through `DPAD_RIGHT`). These indices exceed the raw `SDL_JoystickNumButtons()` count, so `open_controller_config()` extends `num_btns` to at least 15 for GameController devices.

### Button label mapping (GameController)

| Index | Symbol | SDL enum |
|-------|--------|----------|
| 0 | A | `SDL_CONTROLLER_BUTTON_A` |
| 1 | B | `SDL_CONTROLLER_BUTTON_B` |
| 2 | X | `SDL_CONTROLLER_BUTTON_X` |
| 3 | Y | `SDL_CONTROLLER_BUTTON_Y` |
| 4 | BK | `SDL_CONTROLLER_BUTTON_BACK` |
| 5 | G | `SDL_CONTROLLER_BUTTON_GUIDE` |
| 6 | ST | `SDL_CONTROLLER_BUTTON_START` |
| 7 | LS | `SDL_CONTROLLER_BUTTON_LEFTSTICK` |
| 8 | RS | `SDL_CONTROLLER_BUTTON_RIGHTSTICK` |
| 9 | LB | `SDL_CONTROLLER_BUTTON_LEFTSHOULDER` |
| 10 | RB | `SDL_CONTROLLER_BUTTON_RIGHTSHOULDER` |
| 11 | ↑ | `SDL_CONTROLLER_BUTTON_DPAD_UP` |
| 12 | ↓ | `SDL_CONTROLLER_BUTTON_DPAD_DOWN` |
| 13 | ← | `SDL_CONTROLLER_BUTTON_DPAD_LEFT` |
| 14 | → | `SDL_CONTROLLER_BUTTON_DPAD_RIGHT` |

Raw joystick devices always display numeric indices (no symbolic mapping).

---

## SDL_GAMECONTROLLERCONFIG (MSPM0 mapping)

For SDL2 to recognize the MSPM0 gamepad as a GameController (enabling named-button events like `SDL_CONTROLLER_BUTTON_A`, `SDL_CONTROLLER_BUTTON_DPAD_UP`, etc.), a mapping string must be provided via the `SDL_GAMECONTROLLERCONFIG` environment variable.

### Why it's needed

SDL2 maintains an internal database of known controllers (Xbox, PS4, PS5, Nintendo, etc.) keyed by GUID. The MSPM0 gamepad is a custom device not in this database. Without a mapping string, SDL2 opens it as a raw joystick only — the launcher's grid navigation and RetroArch's input system both ignore raw joystick events.

### Mapping string format

```
<GUID>,<device name>,<button mappings>,platform:Linux,
```

Example (button indices must be verified on-device):
```
180000001234000032340000010000,PHYTEC Handheld One Gamepad,a:b5,b:b3,x:b4,y:b2,back:b7,guide:b6,start:b8,leftshoulder:b1,rightshoulder:b0,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a3,platform:Linux,
```

| Token | Meaning |
|-------|---------|
| `<GUID>` | 32-char hex string built by SDL2 from bus type, device name CRC, VID, PID, version. Discover via `--input-debug` |
| `a:b5` | SDL GameController button A → raw joystick button index 5 |
| `dpup:h0.1` | D-pad up → hat 0, bit 1. Maps to `ABS_HAT0Y = -1` from the kernel driver |
| `h0.1`/`.2`/`.4`/`.8` | Hat bitmask: 1=up, 2=right, 4=down, 8=left |
| `leftx:a0` | Left stick X → raw axis 0 |

### Where it's set

The env var is set in `phytec-launcher-start.sh` (the systemd startup wrapper). Since the launcher `fork()/execv()`s child processes (RetroArch, games), all children inherit the variable.

```bash
export SDL_GAMECONTROLLERCONFIG="<GUID>,PHYTEC Handheld One Gamepad,..."
```

### Setup procedure

1. Flash the updated kernel with the `phyhandheld.c` driver changes (ABS_HAT0X/Y, BTN_MODE, VID/PID)
2. Run `phytec-handheld-launcher --input-debug` on the device
3. Copy the GUID from the startup enumeration log
4. Press each button and note the raw joystick index (`SDL_JOYBUTTONDOWN button=N`)
5. Build the mapping string and uncomment/update the line in `phytec-launcher-start.sh`

### Scope

| Application | Input driver | Needs `SDL_GAMECONTROLLERCONFIG`? |
|-------------|-------------|----------------------------------|
| Launcher | SDL2 GameController | Yes — required for D-pad navigation and named button events |
| RetroArch (sdl2 input) | SDL2 GameController | Yes — inherits env var from launcher |
| RetroArch (udev input) | evdev direct | No — reads `/dev/input/eventX` directly; kernel driver changes are sufficient |
| Any SDL2 game | SDL2 GameController | Yes — inherits env var from launcher |
| Non-SDL2 apps | evdev / other | No — kernel driver provides standard evdev capabilities |

---

## Yocto Integration

### Recipe structure (in the meta layer, not this repo)

Meta layer root: `am62x-meta-handheld-one/sources/meta-handheld-one/`

```
recipes-graphics/
  lvgl/
    lvgl_9.1.0.bbappend         # FS_POSIX, libpng, system malloc, fonts, cache
  wayland/
    weston-init.bbappend         # Weston config: DSI-1, 90° rotation, XWayland
  phytec-launcher/
    phytec-launcher_1.0.bb       # Main recipe; RDEPENDS retroarch, mpv; requires covers.inc
    covers.inc                   # do_install:append for cover art PNGs
    files/
      launcher.conf              # Deployed config with icon= paths (6 games)
      phytec-launcher.service    # systemd unit (After=weston.service)
      phytec-launcher-start.sh   # Startup wrapper (env vars + SDL_GAMECONTROLLERCONFIG)
      retroarch.cfg              # RetroArch config → /etc/retroarch/retroarch.cfg
      PHYTEC-Handheld-One-Gamepad.cfg       # RetroArch SDL2 autoconfig → /usr/share/retroarch/autoconfig/sdl2/
      PHYTEC-Handheld-One-Gamepad-udev.cfg  # RetroArch udev autoconfig → /usr/share/retroarch/autoconfig/udev/
      covers/                    # Cover images (layer-specific)
        supertuxkart.png
        neverball.png
        neverputt.png
        3d-som-viewer.png
        glmark2.png
        retroarch.png
        freedoom1.png
        freedoom2.png
        how-we-built-this.png
```

### Startup wrapper (`phytec-launcher-start.sh`)

The startup script launched by the systemd unit:
1. Waits up to 30s for the Weston socket at `/run/wayland-0`
2. Exports `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR`
3. Exports `SDL_GAMECONTROLLERCONFIG` for the MSPM0 gamepad mapping (must be configured with the device's GUID after flashing — see [SDL_GAMECONTROLLERCONFIG section](#sdl_gamecontrollerconfig-mspm0-mapping))
4. Execs the launcher binary

Since the launcher `fork()/execv()`s child apps, all env vars are inherited by RetroArch, games, etc.

### RetroArch input integration

RetroArch supports two joypad drivers relevant to this platform:

| Driver | Config location | How it reads input | Needs `SDL_GAMECONTROLLERCONFIG`? |
|--------|----------------|-------------------|----------------------------------|
| `udev` | `/usr/share/retroarch/autoconfig/udev/` | Reads `/dev/input/eventX` directly via evdev | No |
| `sdl2` | `/usr/share/retroarch/autoconfig/sdl2/` | Uses SDL2 GameController API | Yes |

The meta-retro Yocto layer defaults to `input_joypad_driver = "udev"`. The autoconfig directory is `/usr/share/retroarch/autoconfig` (set via `retroarch-paths.bbclass`).

**Autoconfig profiles** are matched by device name (`input_device`), vendor ID (`input_vendor_id`), and product ID (`input_product_id`). Button/axis indices differ between drivers:

- **udev**: Sequential indices based on evdev capability enumeration. Buttons scan `BTN_JOYSTICK` (0x120) through `KEY_MAX`, then `BTN_MISC` (0x100) through `BTN_JOYSTICK`. Axes scan `ABS_*` sequentially, skipping `ABS_HAT*` (handled separately as hats). D-pad uses hat notation (`h0up`, `h0down`, `h0left`, `h0right`).
- **sdl2**: SDL2 GameController enum values (0=A, 1=B, 2=X, 3=Y, 4=Back, 5=Guide, 6=Start, 9=LeftShoulder, 10=RightShoulder, 11-14=DPad). Requires `SDL_GAMECONTROLLERCONFIG` with correct GUID.

**Caution**: RetroArch generates a user config at `/root/.config/retroarch/retroarch.cfg` on first run. This can override system defaults (including `input_joypad_driver`). Delete this file when troubleshooting input issues.

**Caution**: The `SDL_GAMECONTROLLERCONFIG` GUID changes whenever the kernel driver's VID/PID/version fields change. After any kernel driver update that modifies `input_dev->id.*`, rediscover the GUID with `--input-debug` and update `phytec-launcher-start.sh`.

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
- **MSPM0 SDL_GAMECONTROLLERCONFIG GUID**: Must be rediscovered on-device after any kernel driver change to VID/PID/version (run `--input-debug`). The current GUID in `phytec-launcher-start.sh` is the old name-based GUID from before the VID/PID patch — it needs updating
- **Hat patch not in kernel bbappend**: `0001-drivers-input-joystick-phyhandheld.c-use-HATS-for-d-.patch` exists in the kernel patch directory but is NOT listed in `SRC_URI` in `linux-phytec-ti_%.bbappend` — must be added for clean builds
- **`KEY_UNKNOWN` (240) in evdev capabilities**: The kernel driver registers `KEY_UNKNOWN` (likely from an unmapped scan code in the sparse keymap table). Harmless (SDL2/RetroArch ignore keyboard-range codes for joystick input) but should be cleaned up
- **MSPM0 left joystick Y-axis**: Intermittent issue with up/down not being detected — suspected MSPM0 firmware ADC channel or I2C transmit buffer issue, not a kernel driver problem. Verify with `evtest` on-device

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
