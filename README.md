# phytec-handheld-launcher

A touchscreen and keyboard navigable LVGL game/app running under a Weston Wayland compositor.

The launcher displays a card grid of installed applications and launches them as Wayland clients via `fork()`/`execv()`. While a child app is running, the launcher minimizes its Weston window and keeps the Wayland event loop alive. When the app exits, the launcher window is automatically restored.

---

## Input

| Input | Backend | Notes |
|---|---|---|
| Touchscreen | Wayland protocol (via Weston) | Provided automatically by the LVGL Wayland driver |
| Keyboard | Wayland protocol (via Weston) | Arrow keys to navigate, Enter to launch, Tab to cycle focus |
| Gamepad (MSPM0) | I2C — *not yet integrated* | See `TODO (MSPM0 I2C joystick)` comments in `input.c` / `input.h` |

Input events are routed through the Weston compositor — no direct evdev access is needed for touch or keyboard.

## Configuration

The Wayland socket and runtime directory are set in the systemd unit. Override them there if your BSP uses non-default values:

| Variable | Default | Description |
|---|---|---|
| `WAYLAND_DISPLAY` | `wayland-0` | Weston socket name |
| `XDG_RUNTIME_DIR` | `/run/user/0` | Wayland runtime directory |

## Adding Apps

Edit `files/phytec-launcher/apps.h` and add an entry to `LAUNCHER_APPS`:

```c
{
    .name        = "My Game",
    .icon_symbol = LV_SYMBOL_PLAY,
    .binary_path = "/usr/bin/my-game",
    .argv        = { "/usr/bin/my-game", "--fullscreen", NULL },
},
```

Any `LV_SYMBOL_*` constant is valid for `icon_symbol`. See the [LVGL symbol reference](https://docs.lvgl.io/9.3/overview/font.html#built-in-symbols) for the full list. Child apps are launched as standard Wayland clients and inherit `WAYLAND_DISPLAY` from the launcher process.

## Known Limitations

- **LVGL v9 required.** The Wayland driver (`src/drivers/wayland/`) is part of the main LVGL v9 repo. If your `meta-oe` layer only carries LVGL v8.3, you will need to vendor an `lvgl_9.x.bb` recipe.
- **Weston must be running.** The launcher will fail to start if `WAYLAND_DISPLAY` is not set or Weston is not ready. The systemd unit has `Requires=weston.service` to handle ordering.
- **Apps compiled in.** The app list is compiled from `apps.h` and requires a rebuild to change.
- **MSPM0 joystick not yet integrated.** See `TODO` markers in `input.c` and `input.h` for all integration points.

## License

MIT
