#!/bin/bash
# Install the trackpad refinements - run as a REGULAR user (asks for sudo itself).
#   bash install.sh            install, then log out and back in
#   bash install.sh remove     remove
#   bash install.sh status     exit 0 if installed and up to date
# Installs:
#   /usr/local/lib/mbp115/mbp115-input.so                     3-finger drag + scroll factor (gnome-shell only)
#   /etc/systemd/user/org.gnome.Shell@.service.d/mbp115-input.conf   loads it via LD_PRELOAD
#   ~/.local/share/gnome-shell/extensions/mbp115-3fg@alexg-78   "Three-Finger Drag": keeps 3-finger swipes from GNOME
#   /usr/local/bin/mbp115-touchpad                            settings (pointer curve, scroll, drag)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" -ne 0 ] || { echo "Run without sudo (user settings are applied as well)"; exit 1; }
DROPIN=/etc/systemd/user/org.gnome.Shell@.service.d/mbp115-input.conf
EXTDIR=$HOME/.local/share/gnome-shell/extensions/mbp115-3fg@alexg-78
OLDDIR=/usr/share/gnome-shell/extensions/mbp115-3fg@alexg-78     # before 2026-11: system-wide
case ${1:-install} in
  install)
    sudo -v
    # first install sets the pointer curve and three-finger drag; an update keeps what the user chose since
    first=yes; [ -e /usr/local/lib/mbp115/mbp115-input.so ] && first=no
    v=$(dpkg-query -W -f='${Version}' libinput10 2>/dev/null)
    dpkg --compare-versions "${v:-0}" ge 1.28 || { echo "libinput $v too old (3-finger drag requires 1.28)"; exit 1; }
    echo "== Tools (libinput $v, $(gnome-shell --version 2>/dev/null))"
    sudo apt-get install -y -qq build-essential libinput-dev >/dev/null
    echo "== Building library"
    tmp=$(mktemp -d)
    gcc -std=gnu11 -O2 -Wall -Wextra -fPIC -shared -o "$tmp/mbp115-input.so" "$here/mbp115-input.c"
    sudo install -Dm644 "$tmp/mbp115-input.so" /usr/local/lib/mbp115/mbp115-input.so; rm -rf "$tmp"
    sha256sum "$here/mbp115-input.c" | cut -d' ' -f1 | sudo tee /usr/local/lib/mbp115/mbp115-input.c.sha256 >/dev/null   # setup.sh: rebuild when the source changes
    sudo install -Dm644 "$here/mbp115-input.conf.systemd" "$DROPIN"
    sudo rm -rf "$OLDDIR"
    for f in metadata.json extension.js prefs.js; do install -Dm644 "$here/extension/mbp115-3fg@alexg-78/$f" "$EXTDIR/$f"; done
    sudo install -Dm755 "$here/mbp115-touchpad" /usr/local/bin/mbp115-touchpad
    systemctl --user daemon-reload
    systemctl --user list-units --all 'org.gnome.Shell@*' --no-legend 2>/dev/null | grep -q . \
        || echo "WARNING: gnome-shell is not running as an org.gnome.Shell@* service - the library will not be loaded"
    echo "== User settings"
    [ -e "$HOME/.config/mbp115/input.conf" ] || { mkdir -p "$HOME/.config/mbp115"; printf 'drag3=1\nscroll=0.30\nscroll_accel=1\n' > "$HOME/.config/mbp115/input.conf"; }
    if [ "$(gsettings get org.gnome.shell disable-user-extensions)" = true ]; then
        gsettings set org.gnome.shell disable-user-extensions false
        echo "GNOME's switch that turns off all extensions was on: switched it off, so three-finger drag works."
    fi
    if [ $first = yes ]; then
        mbp115-touchpad drag on
        mbp115-touchpad pointer mac || true
    fi
    cat <<TXT

Done. Now LOG OUT once and LOG BACK IN (loads the library and the extension).
Then: mbp115-touchpad status
If login hangs: Ctrl+Alt+F3, log in, "touch ~/.config/mbp115/input.disable", Ctrl+Alt+F1.
TXT
    ;;
  remove)
    mbp115-touchpad drag off 2>/dev/null || true
    mbp115-touchpad pointer default 2>/dev/null || true
    rm -rf "$EXTDIR"
    sudo rm -rf "$DROPIN" "$OLDDIR" /usr/local/lib/mbp115/mbp115-input.so /usr/local/lib/mbp115/mbp115-input.c.sha256 /usr/local/bin/mbp115-touchpad
    sudo rmdir --ignore-fail-on-non-empty /usr/local/lib/mbp115 2>/dev/null || true
    systemctl --user daemon-reload
    echo "removed - log out and back in"
    ;;
  status)   # the library itself is loaded at the next login
    cmp -s "$here/mbp115-input.conf.systemd" "$DROPIN" && [ -e /usr/local/lib/mbp115/mbp115-input.so ] && cmp -s "$here/mbp115-touchpad" /usr/local/bin/mbp115-touchpad \
        && [ "$(cat /usr/local/lib/mbp115/mbp115-input.c.sha256 2>/dev/null)" = "$(sha256sum "$here/mbp115-input.c" | cut -d' ' -f1)" ] || exit 1
    for f in metadata.json extension.js prefs.js; do cmp -s "$here/extension/mbp115-3fg@alexg-78/$f" "$EXTDIR/$f" || exit 1; done
    ;;
  *) echo "Usage: $0 [install|remove|status]"; exit 2 ;;
esac
