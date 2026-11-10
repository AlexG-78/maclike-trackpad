// Three-Finger Drag - preferences window. Sets three-finger drag and scrolling through the trackpad library's
// config file (~/.config/mbp115/input.conf, re-read continuously, so changes apply at once).
import Adw from 'gi://Adw';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk';

import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

const CONF = GLib.build_filenamev([GLib.get_user_config_dir(), 'mbp115', 'input.conf']);

function readLines() {
    try {
        const [, bytes] = GLib.file_get_contents(CONF);
        return new TextDecoder().decode(bytes).split('\n').filter(l => l !== '');
    } catch {
        return [];
    }
}

function get(key, fallback) {
    const line = readLines().find(l => l.startsWith(`${key}=`));
    return line ? line.slice(key.length + 1) : fallback;
}

function set(key, value) {
    const lines = readLines().filter(l => !l.startsWith(`${key}=`));
    lines.push(`${key}=${value}`);
    GLib.mkdir_with_parents(GLib.path_get_dirname(CONF), 0o755);
    GLib.file_set_contents(CONF, `${lines.join('\n')}\n`);
}

export default class ThreeFingerDragPrefs extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const page = new Adw.PreferencesPage();
        const group = new Adw.PreferencesGroup({
            description: 'Workspace and overview gestures use four fingers while three-finger drag is on.',
        });
        const row = new Adw.SwitchRow({title: 'Three-finger drag', subtitle: 'Move windows and select text with three fingers'});
        row.active = get('drag3', '1') !== '0';
        row.connect('notify::active', () => set('drag3', row.active ? 1 : 0));
        group.add(row);
        page.add(group);

        // fallbacks are the library's own defaults (scroll 1.0, acceleration on)
        const scrolling = new Adw.PreferencesGroup({title: 'Scrolling', description: 'The same for every app.'});
        const factor = new Adw.SpinRow({
            title: 'Scroll factor',
            subtitle: 'Lower scrolls less per finger movement',
            digits: 2,
            adjustment: new Gtk.Adjustment({lower: 0.1, upper: 1.5, step_increment: 0.05, page_increment: 0.1}),
        });
        factor.value = Number(get('scroll', '1')) || 1;
        factor.connect('notify::value', () => set('scroll', factor.value.toFixed(2)));
        scrolling.add(factor);
        const accel = new Adw.SwitchRow({
            title: 'macOS scroll acceleration',
            subtitle: 'Slow movements scroll precisely, fast ones go farther',
        });
        accel.active = get('scroll_accel', '1') !== '0';
        accel.connect('notify::active', () => set('scroll_accel', accel.active ? 1 : 0));
        scrolling.add(accel);
        page.add(scrolling);

        window.add(page);
    }
}
