// Three-Finger Drag - companion to libinput's three-finger drag (idea: github.com/rawneo/3-finger-drag, MIT)
// For fast 3-finger movements (>= 5 mm in 80 ms) libinput reports a SWIPE instead of a drag.
// GNOME would turn that into a workspace switch. We filter out 3-finger swipes in the
// capture phase of the stage; 4-finger swipes still reach GNOME. While drag3=0 in
// ~/.config/mbp115/input.conf (preferences window, mbp115-touchpad) nothing is filtered.
import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const CONF = GLib.build_filenamev([GLib.get_user_config_dir(), 'mbp115', 'input.conf']);

function dragOn() {
    try {
        const [, bytes] = GLib.file_get_contents(CONF);
        return !/^drag3=0$/m.test(new TextDecoder().decode(bytes));
    } catch {
        return true;
    }
}

export default class ThreeFingerDrag extends Extension {
    enable() {
        this._on = dragOn();
        this._monitor = Gio.File.new_for_path(CONF).monitor_file(Gio.FileMonitorFlags.WATCH_MOVES, null);
        this._monitor.connect('changed', () => (this._on = dragOn()));
        this._id = global.stage.connect('captured-event', (_actor, event) => {
            if (this._on && event.type() === Clutter.EventType.TOUCHPAD_SWIPE &&
                event.get_touchpad_gesture_finger_count() === 3)
                return Clutter.EVENT_STOP;
            return Clutter.EVENT_PROPAGATE;
        });
    }

    disable() {
        if (this._id) {
            global.stage.disconnect(this._id);
            this._id = 0;
        }
        this._monitor?.cancel();
        this._monitor = null;
    }
}
