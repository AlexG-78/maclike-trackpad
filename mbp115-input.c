/* mbp115-input.so - touchpad refinements for GNOME/Mutter, without patching GNOME or libinput.
 *
 * Loaded ONLY into gnome-shell via a systemd drop-in (LD_PRELOAD); it then removes itself
 * from LD_PRELOAD so that programs launched from the shell do not inherit it.
 *
 *  - 3-finger drag like macOS: libinput >= 1.28 supports it, but Mutter never enables it.
 *    We hook libinput_device_config_tap_set_enabled() (Mutter calls it for every touchpad)
 *    and re-check periodically in libinput_dispatch().
 *  - Touchpad scrolling like macOS (Mutter has no setting; the mouse wheel is unchanged), in
 *    libinput_event_pointer_get_scroll_value() for LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
 *    one global scroll factor, plus scroll acceleration - slow two-finger movements scroll precisely,
 *    fast ones go farther (gain 0.85 .. 2.4 over finger speed). Applies to every app the same way.
 *  - Fix for the custom pointer curve (GNOME 51, accel-profile 'custom'): Mutter only sets the
 *    curve for motion. For scrolling, libinput then uses "flat" in raw device units -
 *    ~9x too fast on the Apple trackpad. When the config is applied we add a constant scroll curve
 *    that exactly matches libinput's normal touchpad scrolling: 1000/dpi * 0.9 * 0.2968.
 *
 * Settings: ~/.config/mbp115/input.conf (re-read continuously, no re-login needed)
 *     drag3=1        3-finger drag on (0 = off)
 *     scroll=1.00    touchpad scroll factor (0.05 .. 5)
 *     scroll_accel=1 macOS-like scroll acceleration (0 = constant speed)
 *     upm=97.8       trackpad units per mm (bcm5974 Wellspring9: 10173 over 104 mm)
 * Kill switch: file ~/.config/mbp115/input.disable -> the library does nothing.
 * All libinput functions are resolved via dlsym(RTLD_NEXT): no load-time dependency.
 * License: MIT. Idea for the 3-finger part: github.com/rawneo/3-finger-drag (MIT).
 */
#define _GNU_SOURCE
#include <libinput.h>
#include <dlfcn.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG "[mbp115-input] "
#define MAXDEV 8

static int active;                 /* in gnome-shell only */
static int drag3 = 1;
static double scroll = 1.0;
static double upm = 97.8;          /* device units per mm */
static char conf[512];
static struct timespec conf_mtime;
static int scroll_accel = 1;
static inline double fabs_(double x) { return x < 0 ? -x : x; }
static locale_t c_num;
static struct libinput_device *devs[MAXDEV];
static int ndev;

static enum libinput_config_status (*r_tap_set)(struct libinput_device *, enum libinput_config_tap_state);
static int (*r_dispatch)(struct libinput *);
static double (*r_scroll)(struct libinput_event_pointer *, enum libinput_pointer_axis);
static int (*r_has_axis)(struct libinput_event_pointer *, enum libinput_pointer_axis);
static uint64_t (*r_time_usec)(struct libinput_event_pointer *);
static struct libinput_event *(*r_ptr_base)(struct libinput_event_pointer *);
static enum libinput_event_type (*r_ev_type)(struct libinput_event *);
static int (*r_3fg_count)(struct libinput_device *);
static enum libinput_config_status (*r_3fg_set)(struct libinput_device *, enum libinput_config_3fg_drag_state);
static enum libinput_config_3fg_drag_state (*r_3fg_get)(struct libinput_device *);
static struct libinput_device *(*r_dev_ref)(struct libinput_device *);
static struct libinput_device *(*r_dev_unref)(struct libinput_device *);
static struct libinput_event *(*r_get_event)(struct libinput *);
static struct libinput_device *(*r_ev_device)(struct libinput_event *);
static struct libinput *(*r_unref)(struct libinput *);
static struct libinput *cur_li;    /* context the remembered devices belong to */
static const char *(*r_dev_name)(struct libinput_device *);
static enum libinput_config_status (*r_accel_apply)(struct libinput_device *, struct libinput_config_accel *);
static enum libinput_config_status (*r_accel_points)(struct libinput_config_accel *, enum libinput_config_accel_type,
                                                     double, size_t, const double *);
static int (*r_tap_count)(struct libinput_device *);

#define SYM(var, name) do { if (!(var)) *(void **)&(var) = dlsym(RTLD_NEXT, name); } while (0)

static void strip_preload(void)
{
    const char *lp = getenv("LD_PRELOAD");
    if (!lp)
        return;
    char *copy = strdup(lp), out[4096] = "", *save = NULL;
    if (!copy)
        return;
    for (char *t = strtok_r(copy, ": ", &save); t; t = strtok_r(NULL, ": ", &save)) {
        if (strstr(t, "mbp115-input"))
            continue;
        if (strlen(out) + strlen(t) + 2 >= sizeof(out))
            break;
        if (out[0])
            strcat(out, " ");
        strcat(out, t);
    }
    free(copy);
    if (out[0])
        setenv("LD_PRELOAD", out, 1);
    else
        unsetenv("LD_PRELOAD");
}

static void load_conf(void)
{
    struct stat st;
    if (!conf[0] || stat(conf, &st) != 0)
        return;
    if (st.st_mtim.tv_sec == conf_mtime.tv_sec && st.st_mtim.tv_nsec == conf_mtime.tv_nsec)
        return;
    conf_mtime = st.st_mtim;
    FILE *f = fopen(conf, "r");
    if (!f)
        return;
    char line[256];
    int d = drag3, a = scroll_accel;
    double s = scroll;
    while (fgets(line, sizeof(line), f)) {
        char *v = strchr(line, '=');
        if (line[0] == '#' || !v)
            continue;
        *v++ = '\0';
        if (!strcmp(line, "drag3"))
            d = atoi(v) ? 1 : 0;
        else if (!strcmp(line, "upm")) {
            double x = c_num ? strtod_l(v, NULL, c_num) : strtod(v, NULL);
            if (x >= 5.0 && x <= 500.0)
                upm = x;
        } else if (!strcmp(line, "scroll_accel")) {
            a = atoi(v) ? 1 : 0;
        } else if (!strcmp(line, "scroll")) {
            double x = c_num ? strtod_l(v, NULL, c_num) : strtod(v, NULL); /* dot as decimal separator */
            if (x >= 0.05 && x <= 5.0)
                s = x;
        }
    }
    fclose(f);
    if (d != drag3 || s != scroll || a != scroll_accel)
        fprintf(stderr, TAG "settings: drag3=%d scroll=%.2f scroll_accel=%d\n", d, s, a);
    drag3 = d;
    scroll = s;
    scroll_accel = a;
}

/* macOS-like scroll acceleration. Finger-scroll values are in 1000-dpi units (1 unit = 0.0254 mm);
 * finger speed is smoothed over the events of one gesture. Both axes of one event share a timestamp,
 * so the gain is computed once per event. Called in Mutter's input thread only. */
#define ACC_GMIN 0.85     /* slow movements: a bit finer than 1:1 */
#define ACC_GMAX 2.4      /* fast flicks */
#define ACC_HALF 200.0    /* mm/s at which half of the extra gain is reached */
#define ACC_TAU  60000.0  /* us: time constant of the speed smoothing */
#define ACC_SLEW 0.10     /* the gain changes by at most 10 % per event: no visible steps */
static double scroll_gain(struct libinput_event_pointer *event)
{
    static uint64_t last_t;
    static double speed, gain = ACC_GMIN;
    SYM(r_time_usec, "libinput_event_pointer_get_time_usec");
    SYM(r_has_axis, "libinput_event_pointer_has_axis");
    if (!r_time_usec || !r_has_axis || !r_scroll)
        return 1.0;
    uint64_t t = r_time_usec(event);
    if (t == last_t)
        return gain;                      /* second axis of the same event */
    uint64_t dt = t - last_t;
    last_t = t;
    double d = 0.0;
    if (r_has_axis(event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
        d += fabs_(r_scroll(event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL));
    if (r_has_axis(event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
        d += fabs_(r_scroll(event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL));
    if (d == 0.0)                         /* end of the gesture (kinetic stop) */
        return gain;
    if (dt == 0 || dt > 100000) {         /* new gesture: start precise */
        speed = 0.0;
        gain = ACC_GMIN;
        return gain;
    }
    /* time-weighted moving average of the finger speed: sensor noise and uneven event
     * timing do not reach the gain */
    double s = d * 0.0254 / ((double)dt / 1e6);           /* mm/s */
    double a = (double)dt / ((double)dt + ACC_TAU);
    speed = speed > 0.0 ? speed + a * (s - speed) : s;
    double s2 = speed * speed;
    double want = ACC_GMIN + (ACC_GMAX - ACC_GMIN) * s2 / (s2 + ACC_HALF * ACC_HALF);
    if (want > gain * (1.0 + ACC_SLEW))
        want = gain * (1.0 + ACC_SLEW);
    else if (want < gain * (1.0 - ACC_SLEW))
        want = gain * (1.0 - ACC_SLEW);
    gain = want;
    return gain;
}

/* Device lifecycle: we keep a reference to each touchpad so 3-finger drag can be re-applied when
 * input.conf changes. Drop it when libinput reports the device as removed (e.g. USB re-enumeration
 * after resume) and before the context is destroyed (logout), so no stale device is ever touched. */
static void forget_device(struct libinput_device *dev)
{
    SYM(r_dev_unref, "libinput_device_unref");
    for (int i = 0; i < ndev; i++) {
        if (devs[i] != dev)
            continue;
        if (r_dev_unref)
            r_dev_unref(dev);
        devs[i] = devs[--ndev];
        return;
    }
}

static void forget_all(void)
{
    SYM(r_dev_unref, "libinput_device_unref");
    while (ndev > 0) {
        ndev--;
        if (r_dev_unref)
            r_dev_unref(devs[ndev]);
    }
}

static void apply_3fg(struct libinput_device *dev)
{
    SYM(r_3fg_count, "libinput_device_config_3fg_drag_get_finger_count");
    SYM(r_3fg_set, "libinput_device_config_3fg_drag_set_enabled");
    SYM(r_3fg_get, "libinput_device_config_3fg_drag_get_enabled");
    SYM(r_dev_ref, "libinput_device_ref");
    SYM(r_dev_name, "libinput_device_get_name");
    if (!dev || !r_3fg_count || !r_3fg_set || !r_3fg_get)   /* libinput < 1.28 */
        return;
    if (r_3fg_count(dev) < 3)
        return;
    enum libinput_config_3fg_drag_state want =
        drag3 ? LIBINPUT_CONFIG_3FG_DRAG_ENABLED_3FG : LIBINPUT_CONFIG_3FG_DRAG_DISABLED;
    if (r_3fg_get(dev) != want) {
        enum libinput_config_status rc = r_3fg_set(dev, want);
        fprintf(stderr, TAG "3-finger drag %s (%s, rc=%d)\n", drag3 ? "on" : "off",
                r_dev_name ? r_dev_name(dev) : "?", rc);
    }
    for (int i = 0; i < ndev; i++)
        if (devs[i] == dev)
            return;
    if (ndev < MAXDEV && r_dev_ref)
        devs[ndev++] = r_dev_ref(dev);
}

__attribute__((constructor)) static void mbp115_init(void)
{
    char comm[64] = "";
    FILE *f = fopen("/proc/self/comm", "r");
    if (f) {
        if (fgets(comm, sizeof(comm), f))
            comm[strcspn(comm, "\n")] = '\0';
        fclose(f);
    }
    strip_preload();
    if (strcmp(comm, "gnome-shell") != 0)
        return;
    const char *home = getenv("HOME");
    const char *user = getenv("USER");
    if (!home)
        return;
    /* not in the login screen (GDM's own gnome-shell runs as gdm/gdm-greeter) */
    if (!strncmp(home, "/run/gdm", 8) || !strncmp(home, "/var/lib/gdm", 12) || (user && !strncmp(user, "gdm", 3)))
        return;
    char off[512];
    snprintf(off, sizeof(off), "%s/.config/mbp115/input.disable", home);
    if (access(off, F_OK) == 0) {
        fprintf(stderr, TAG "kill switch %s present: inactive\n", off);
        return;
    }
    snprintf(conf, sizeof(conf), "%s/.config/mbp115/input.conf", home);
    c_num = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    load_conf();
    active = 1;
    fprintf(stderr, TAG "active: drag3=%d scroll=%.2f (%s)\n", drag3, scroll, conf);
}

enum libinput_config_status
libinput_device_config_tap_set_enabled(struct libinput_device *device, enum libinput_config_tap_state enable)
{
    SYM(r_tap_set, "libinput_device_config_tap_set_enabled");
    enum libinput_config_status st = r_tap_set ? r_tap_set(device, enable) : LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
    if (active)
        apply_3fg(device);
    return st;
}

int libinput_dispatch(struct libinput *li)
{
    static unsigned long n;
    SYM(r_dispatch, "libinput_dispatch");
    int rc = r_dispatch ? r_dispatch(li) : -1;
    if (active && li != cur_li) {     /* a new context: devices of an old one must not be touched */
        if (cur_li)
            ndev = 0;
        cur_li = li;
    }
    if (active && (n++ % 500) == 0) {
        load_conf();
        for (int i = 0; i < ndev; i++)
            apply_3fg(devs[i]);
    }
    return rc;
}

struct libinput_event *libinput_get_event(struct libinput *li)
{
    SYM(r_get_event, "libinput_get_event");
    struct libinput_event *ev = r_get_event ? r_get_event(li) : NULL;
    if (active && ev && ndev > 0) {
        SYM(r_ev_type, "libinput_event_get_type");
        SYM(r_ev_device, "libinput_event_get_device");
        if (r_ev_type && r_ev_device && r_ev_type(ev) == LIBINPUT_EVENT_DEVICE_REMOVED)
            forget_device(r_ev_device(ev));
    }
    return ev;
}

struct libinput *libinput_unref(struct libinput *li)
{
    SYM(r_unref, "libinput_unref");
    if (active && li && li == cur_li) {   /* logout/shutdown: release our device references first */
        forget_all();
        cur_li = NULL;
    }
    return r_unref ? r_unref(li) : NULL;
}

double libinput_event_pointer_get_scroll_value(struct libinput_event_pointer *event, enum libinput_pointer_axis axis)
{
    SYM(r_scroll, "libinput_event_pointer_get_scroll_value");
    double v = r_scroll ? r_scroll(event, axis) : 0.0;
    if (!active || (scroll == 1.0 && !scroll_accel))
        return v;
    SYM(r_ptr_base, "libinput_event_pointer_get_base_event");
    SYM(r_ev_type, "libinput_event_get_type");
    if (r_ptr_base && r_ev_type && r_ev_type(r_ptr_base(event)) == LIBINPUT_EVENT_POINTER_SCROLL_FINGER)
        v *= scroll * (scroll_accel ? scroll_gain(event) : 1.0);
    return v;
}

/* Custom profile: set the scroll and fallback curves so touchpad scrolling behaves as in the default profile */
enum libinput_config_status
libinput_device_config_accel_apply(struct libinput_device *device, struct libinput_config_accel *config)
{
    SYM(r_accel_apply, "libinput_device_config_accel_apply");
    if (active) {
        SYM(r_accel_points, "libinput_config_accel_set_points");
        SYM(r_tap_count, "libinput_device_config_tap_get_finger_count");
        if (r_accel_points && r_tap_count && r_tap_count(device) > 0) {        /* touchpads only */
            double dpi = upm * 25.4;
            double pts[2] = { 0.0, 1000.0 / dpi * 0.9 * 0.2968 };            /* libinput: normalize_for_dpi * baseline * TP_MAGIC_SLOWDOWN */
            enum libinput_config_status a = r_accel_points(config, LIBINPUT_ACCEL_TYPE_SCROLL, 1.0, 2, pts);
            enum libinput_config_status b = r_accel_points(config, LIBINPUT_ACCEL_TYPE_FALLBACK, 1.0, 2, pts);
            if (a == LIBINPUT_CONFIG_STATUS_SUCCESS)
                fprintf(stderr, TAG "custom pointer curve: scroll speed corrected to normal (factor %.4f, rc=%d/%d)\n",
                        pts[1], a, b);
        }
    }
    return r_accel_apply ? r_accel_apply(device, config) : LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
}
