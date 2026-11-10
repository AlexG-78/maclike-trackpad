# Mac-like trackpad

macOS-like trackpad behavior for GNOME Shell on Wayland: three-finger drag, macOS scroll acceleration, one global
scroll speed and a macOS pointer curve. No patches to GNOME or libinput.

![License: MIT](https://img.shields.io/badge/license-MIT-blue)
![GNOME Shell 50 | 51](https://img.shields.io/badge/GNOME%20Shell-50%20%7C%2051-4a86cf)

## Features

- **Three-finger drag:** move windows and select text with three fingers. Workspace and Overview gestures move to
  four fingers
- **macOS scroll acceleration:** slow two-finger movements scroll precisely, fast ones go farther
- **One scroll speed for every app:** a global scroll factor; the mouse wheel is unchanged
- **macOS pointer curve:** precise when slow, covers distance when fast (`mac`, `mac-slow`, `mac-fast`)
- **Correct scroll speed with a custom pointer curve:** without the fix, GNOME 51 scrolls far too fast with one
- **Settings window** in Extension Manager and a command-line tool, changes apply at once

## How it works

| Part | Content |
|---|---|
| `mbp115-input.c` | small library loaded only into gnome-shell (LD_PRELOAD through a systemd drop-in). It turns on libinput's three-finger drag, which GNOME never enables, and adjusts touchpad scroll values |
| `extension/mbp115-3fg@alexg-78` | GNOME Shell extension "Three-Finger Drag": keeps three-finger swipes from GNOME so the drag works; settings window |
| `mbp115-touchpad` | command-line settings: pointer curve, scroll factor, scroll acceleration, three-finger drag |

The library removes itself from `LD_PRELOAD`, so apps started from the shell do not load it. It stays inactive in the
login screen.

## Requirements

| | |
|---|---|
| GNOME Shell | 50 or 51 on Wayland, started as a systemd user service (`org.gnome.Shell@wayland`, the default on Ubuntu) |
| libinput | 1.28 or newer |
| Pointer curve | GNOME 51 or newer |
| Installer | Ubuntu or Debian (`apt`, installs `build-essential` and `libinput-dev`) |
| Tested on | MacBook Pro 11,5 (bcm5974 trackpad), Ubuntu 26.10, GNOME 51. Other trackpads untested |

## Installation

### With the installer

```bash
git clone https://github.com/AlexG-78/maclike-trackpad.git
cd maclike-trackpad
bash install.sh
```

Run it as your user; it asks for your password itself. It:

- builds the library and installs it to `/usr/local/lib/mbp115/mbp115-input.so`
- adds the drop-in `/etc/systemd/user/org.gnome.Shell@.service.d/mbp115-input.conf` that loads it into gnome-shell
- installs the extension to `~/.local/share/gnome-shell/extensions/mbp115-3fg@alexg-78` and `mbp115-touchpad` to `/usr/local/bin`
- on the first install: turns on three-finger drag, sets the `mac` pointer curve and scroll factor 0.30

Log out and back in once, then check with `mbp115-touchpad status`.

`bash install.sh status` exits with 0 when everything is installed and up to date.

### Manual

```bash
gcc -std=gnu11 -O2 -Wall -Wextra -fPIC -shared -o mbp115-input.so mbp115-input.c   # needs libinput headers
sudo install -Dm644 mbp115-input.so /usr/local/lib/mbp115/mbp115-input.so
sudo install -Dm644 mbp115-input.conf.systemd /etc/systemd/user/org.gnome.Shell@.service.d/mbp115-input.conf
sudo install -Dm755 mbp115-touchpad /usr/local/bin/mbp115-touchpad
cp -r extension/mbp115-3fg@alexg-78 ~/.local/share/gnome-shell/extensions/
systemctl --user daemon-reload
```

Log out and back in, then run `mbp115-touchpad drag on` and `mbp115-touchpad pointer mac`.

### Update

```bash
git pull && bash install.sh
```

An update keeps your pointer curve, scroll settings and three-finger drag choice. Log out and back in afterwards.

### Uninstall

```bash
bash install.sh remove
```

Turns three-finger drag off, resets the pointer to GNOME's default and removes all installed files. Log out and back
in. Your settings file `~/.config/mbp115/input.conf` stays.

## Configuration

Open the settings in Extension Manager (⚙ on "Three-Finger Drag") or use the command line:

| Command | Effect |
|---|---|
| `mbp115-touchpad` | show the current settings and whether the library is loaded |
| `mbp115-touchpad drag on\|off` | three-finger drag; off gives three-finger gestures back to GNOME |
| `mbp115-touchpad scroll 0.3` | scroll factor, 0.05 to 5 (settings window: 0.1 to 1.5) |
| `mbp115-touchpad scroll-accel on\|off` | macOS scroll acceleration |
| `mbp115-touchpad pointer mac\|mac-slow\|mac-fast\|default [factor]` | pointer curve, optionally scaled, e.g. `mac 1.1` |

Everything is stored in `~/.config/mbp115/input.conf` and read again on the next gesture, no new login needed:

| Key | Default | |
|---|---|---|
| `drag3` | `1` | three-finger drag |
| `scroll` | `0.30` | scroll factor |
| `scroll_accel` | `1` | scroll acceleration |
| `upm` | `97.8` | trackpad units per mm (bcm5974); other trackpads may need their own value |

## Known limitations

- Apps continue scrolling after you lift your fingers (kinetic scrolling) by themselves. Chromium and Electron apps
  scroll about 1.3 times as far as Firefox and twice as far as GTK apps; one global setting cannot even that out
- Scroll factor and acceleration apply to touchpad scrolling only, never to a mouse wheel

## Troubleshooting

| Problem | Fix |
|---|---|
| Login hangs after installing | Ctrl+Alt+F3, log in, `touch ~/.config/mbp115/input.disable`, Ctrl+Alt+F1. The file turns the library off |
| `mbp115-touchpad` says the library is not loaded | log out and back in; check that gnome-shell runs as `org.gnome.Shell@*` (`systemctl --user list-units 'org.gnome.Shell@*'`) |
| Three-finger drag does nothing | check that the extension is on (`gnome-extensions info mbp115-3fg@alexg-78`) and libinput is 1.28 or newer |
| Log messages | `journalctl --user -b -o cat _COMM=gnome-shell \| grep mbp115-input` |

## Development

Bug reports and pull requests are welcome. Changes are listed in [CHANGELOG.md](CHANGELOG.md).

The trackpad parts also ship with [mbp115-linux](https://github.com/AlexG-78/mbp115-linux), a kit for MacBook Pro
11,5 under Ubuntu, as the `touchpad` setup step.

## Credits and license

MIT ([LICENSE](LICENSE)). Idea for the three-finger drag: [rawneo/3-finger-drag](https://github.com/rawneo/3-finger-drag) (MIT).

Developed with [Claude Code](https://claude.com/claude-code).

**Trademarks:** Apple, Mac, MacBook and macOS are trademarks of Apple Inc., used here only to name the hardware and the
behavior imitated. Not affiliated with or endorsed by Apple.
