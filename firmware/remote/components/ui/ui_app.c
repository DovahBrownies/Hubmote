/*
 * App context: the panel shown while an app is in the foreground. One panel
 * serves every app; it changes its header, the note about what is playing,
 * whether the previous/next keys show, and, for apps with nothing to play
 * (Settings...), leaves out the playback keys and Captions.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

#define ACTION_W ((UI_INNER_W - UI_GAP) / 2)

static lv_obj_t *s_icon;
static lv_obj_t *s_mono_box;
static lv_obj_t *s_mono;
static lv_obj_t *s_name;
static lv_obj_t *s_package;
static lv_obj_t *s_note;
static lv_obj_t *s_note_head;
static lv_obj_t *s_note_title;
static lv_obj_t *s_note_sub;
/* The app in front, and what the companion says is playing. */
static char s_pkg[96];
static char s_app_name[UI_APP_NAME_MAX];
static bool s_hides_title;
static struct {
    char pkg[96];
    char title[96];
    char subtitle[96];
    bool playing;
} s_media;
static lv_obj_t *s_prev;
static lv_obj_t *s_next;
static lv_obj_t *s_transport;
static lv_obj_t *s_captions;
static lv_obj_t *s_exit;
static bool s_media_app; /* the app in front may play something */

/* The +-10 s buttons send D-pad left/right, which seek in nearly every TV
 * app (Netflix treats the media rewind/fast-forward keys as "scrub until
 * pressed again"). A tap seeks once. Holding seeks again and again, every
 * SEEK_START_MS at first and 20% sooner each time, down to SEEK_FAST_MS. */
#define SEEK_START_MS 300
#define SEEK_FAST_MS  60

static lv_timer_t *s_seek_timer;
static uint32_t s_seek_period;

static void seek_tick(lv_timer_t *t)
{
    ui_emit_key((int)(intptr_t)lv_timer_get_user_data(t), UI_DIR_SHORT);
    s_seek_period = s_seek_period * 4 / 5 > SEEK_FAST_MS ? s_seek_period * 4 / 5 : SEEK_FAST_MS;
    lv_timer_set_period(t, s_seek_period);
}

static void on_seek(lv_event_t *e)
{
    int key = (int)(intptr_t)lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_SHORT_CLICKED:
        ui_emit_key(key, UI_DIR_SHORT);
        break;
    case LV_EVENT_LONG_PRESSED:
        ui_emit_key(key, UI_DIR_SHORT);
        if (!s_seek_timer) {
            s_seek_period = SEEK_START_MS;
            s_seek_timer = lv_timer_create(seek_tick, s_seek_period, (void *)(intptr_t)key);
        }
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (s_seek_timer) {
            lv_timer_delete(s_seek_timer);
            s_seek_timer = NULL;
        }
        break;
    default:
        break;
    }
}

static void on_wrong_app(lv_event_t *e)
{
    /* Detection lags for apps that never open the keyboard; let the user
     * fall back to the launcher layout by hand. */
    ui_show_view(VIEW_TV_HOME);
}

static lv_obj_t *round_key(lv_obj_t *parent, int32_t size, lv_color_t bg, const lv_font_t *font,
                           const char *icon, lv_color_t fg, ui_key_t key, ui_keymode_t mode)
{
    lv_obj_t *b = ui_btn(parent, size, size, LV_RADIUS_CIRCLE, bg);
    lv_obj_t *l = ui_label(b, font, fg, icon);
    lv_obj_center(l);
    ui_btn_bind_key(b, key, mode);
    return b;
}

lv_obj_t *ui_app_create(lv_obj_t *parent)
{
    lv_obj_t *p = ui_panel(parent, UI_CENTER_W, UI_BODY_H);
    ui_flex(p, LV_FLEX_FLOW_COLUMN, UI_S(18));

    /* Header: icon (monogram for unknown apps), name, package, override */
    lv_obj_t *head = ui_box(p);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(head, LV_FLEX_FLOW_ROW, UI_S(12));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_icon = lv_image_create(head);
    s_mono_box = ui_box(head);
    lv_obj_set_size(s_mono_box, UI_S(44), UI_S(44));
    lv_obj_set_style_radius(s_mono_box, ui_radius(12), 0);
    lv_obj_set_style_bg_color(s_mono_box, COL_SURFACE3, 0);
    lv_obj_set_style_bg_opa(s_mono_box, LV_OPA_COVER, 0);
    s_mono = ui_label(s_mono_box, &font_sora_18b, COL_ACCENT_TEXT, "");
    lv_obj_center(s_mono);
    lv_obj_t *names = ui_box(head);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(names, 1);
    ui_flex(names, LV_FLEX_FLOW_COLUMN, UI_S(3));
    s_name = ui_label(names, &font_sora_20sb, COL_TEXT, "");
    s_package = ui_label(names, &font_mono_11, COL_TEXT3, "");
    lv_label_set_long_mode(s_package, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_package, UI_S(220));
    lv_obj_t *wrong = ui_btn_text(head, LV_SIZE_CONTENT, UI_S(36), 10, COL_SURFACE1, &font_sora_12, COL_TEXT2, "Wrong app?");
    lv_obj_set_style_pad_hor(wrong, UI_S(12), 0);
    lv_obj_set_style_border_width(wrong, UI_S(1), 0);
    lv_obj_set_style_border_color(wrong, COL_LINE, 0);
    lv_obj_add_event_cb(wrong, on_wrong_app, LV_EVENT_CLICKED, NULL);

    /* What's playing (from the companion), or a note for apps that never
     * share it */
    s_note = ui_box(p);
    lv_obj_set_size(s_note, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(s_note, ui_radius(16), 0);
    lv_obj_set_style_bg_color(s_note, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(s_note, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_note, UI_S(16), 0);
    ui_flex(s_note, LV_FLEX_FLOW_COLUMN, UI_S(6));
    s_note_head = ui_label(s_note, &font_sora_11, COL_TEXT2, "NOW PLAYING");
    s_note_title = ui_label(s_note, &font_sora_15m, COL_TEXT, "");
    lv_obj_set_width(s_note_title, LV_PCT(100));
    lv_label_set_long_mode(s_note_title, LV_LABEL_LONG_MODE_DOTS);
    s_note_sub = ui_label(s_note, &font_sora_12, COL_TEXT2, "");
    lv_obj_set_width(s_note_sub, LV_PCT(100));
    lv_label_set_long_mode(s_note_sub, LV_LABEL_LONG_MODE_DOTS);

    /* Transport */
    lv_obj_t *transport = ui_box(p);
    s_transport = transport;
    lv_obj_set_size(transport, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(transport, UI_S(8), 0);
    ui_flex(transport, LV_FLEX_FLOW_ROW, UI_S(16));
    lv_obj_set_flex_align(transport, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_prev = round_key(transport, UI_S(52), COL_SURFACE1, &font_icons_24, ICON_SKIP_BACK, COL_TEXT2,
                       UI_KEY_MEDIA_PREVIOUS, UI_KEYMODE_TAP);
    lv_obj_t *back10 = ui_btn(transport, UI_S(68), UI_S(68), LV_RADIUS_CIRCLE, COL_SURFACE2);
    lv_obj_center(ui_label(back10, &font_icons_28, COL_TEXT, ICON_REWIND));
    lv_obj_add_event_cb(back10, on_seek, LV_EVENT_ALL, (void *)(intptr_t)UI_KEY_DPAD_LEFT);
    lv_obj_t *play = ui_btn(transport, UI_S(96), UI_S(96), LV_RADIUS_CIRCLE, COL_ACCENT);
    ui_flex(play, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(play, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(play, &font_icons_28, COL_ON_ACCENT, ICON_PLAY);
    ui_label(play, &font_icons_28, COL_ON_ACCENT, ICON_PAUSE);
    ui_btn_bind_key(play, UI_KEY_MEDIA_PLAY_PAUSE, UI_KEYMODE_TAP);
    lv_obj_t *fwd10 = ui_btn(transport, UI_S(68), UI_S(68), LV_RADIUS_CIRCLE, COL_SURFACE2);
    lv_obj_center(ui_label(fwd10, &font_icons_28, COL_TEXT, ICON_FORWARD));
    lv_obj_add_event_cb(fwd10, on_seek, LV_EVENT_ALL, (void *)(intptr_t)UI_KEY_DPAD_RIGHT);
    s_next = round_key(transport, UI_S(52), COL_SURFACE1, &font_icons_24, ICON_SKIP_FORWARD, COL_TEXT2,
                       UI_KEY_MEDIA_NEXT, UI_KEYMODE_TAP);

    /* Actions, pinned to the bottom */
    lv_obj_t *spacer = ui_box(p);
    lv_obj_set_width(spacer, UI_S(1));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *actions = ui_box(p);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex(actions, LV_FLEX_FLOW_ROW, UI_GAP);
    /* No Search: the Search key opens voice search, which a remote without a
     * microphone can't use, and apps' own search screens can't be opened
     * from outside. Typing reaches them once they are open. */
    s_captions = ui_btn_icon(actions, ACTION_W, UI_S(64), 14, COL_SURFACE2, &font_icons_20, ICON_CAPTIONS, COL_TEXT,
                             "Captions", true);
    ui_btn_bind_key(s_captions, UI_KEY_CAPTIONS, UI_KEYMODE_TAP);
    s_exit = ui_btn_icon(actions, ACTION_W, UI_S(64), 14, COL_SURFACE2, &font_icons_20, ICON_EXIT, COL_TEXT, "Exit app",
                         true);
    ui_btn_bind_key(s_exit, UI_KEY_HOME, UI_KEYMODE_TAP);
    return p;
}

/* True if text only names the app ("PrimeVideo" for Prime Video): its
 * letters and digits, case ignored. */
static bool names_app(const char *text, const char *name)
{
    if (!name[0]) {
        return false;
    }
    for (const char *a = text, *b = name;; a++, b++) {
        while (*a && !isalnum((unsigned char)*a)) {
            a++;
        }
        while (*b && !isalnum((unsigned char)*b)) {
            b++;
        }
        if (!*a || !*b) {
            return !*a && !*b;
        }
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
}

/* The title of what's playing in the app in front, if the companion knows
 * it. A note instead when the app plays without one (its session has no
 * title, or only the app's name), or never shares it (hides_title);
 * otherwise nothing. */
static void paint_note(void)
{
    bool session = s_media.pkg[0] && strcmp(s_media.pkg, s_pkg) == 0;
    bool media = session && s_media.title[0] && !names_app(s_media.title, s_app_name);
    lv_obj_set_hidden(s_note, !s_media_app || (!media && !session && !s_hides_title));
    lv_label_set_text(s_note_head, media && !s_media.playing ? "PAUSED" : "NOW PLAYING");
    lv_label_set_text(s_note_title, media ? s_media.title : "This app doesn't share the title");
    lv_label_set_text(s_note_sub, media ? s_media.subtitle : "");
    lv_obj_set_hidden(s_note_sub, !media || !s_media.subtitle[0]);
}

void ui_app_set_media(const char *pkg, const char *title, const char *subtitle, bool playing)
{
    strlcpy(s_media.pkg, pkg ? pkg : "", sizeof(s_media.pkg));
    strlcpy(s_media.title, title ? title : "", sizeof(s_media.title));
    strlcpy(s_media.subtitle, subtitle ? subtitle : "", sizeof(s_media.subtitle));
    s_media.playing = playing;
    if (s_note) {
        paint_note();
    }
}

/* The first letter of the app's name; for an unknown app, of the last
 * segment of its package. */
static void monogram_from(const ui_app_info_t *app, const char *package, char *out, size_t len)
{
    const char *seg = app ? app->name : strrchr(package, '.');
    seg = app ? seg : seg ? seg + 1 : package;
    char c = seg[0] >= 'a' && seg[0] <= 'z' ? (char)(seg[0] & ~0x20) : seg[0];
    snprintf(out, len, "%c", c ? c : '?');
}

void ui_app_set(const ui_app_info_t *app, const char *package)
{
    static char mono[4];
    bool icon = app && app->icon_small;
    lv_obj_set_hidden(s_icon, !icon);
    lv_obj_set_hidden(s_mono_box, icon);
    if (icon) {
        lv_image_set_src(s_icon, app->icon_small);
    } else {
        monogram_from(app, package, mono, sizeof(mono));
        lv_label_set_text(s_mono, mono);
    }
    lv_label_set_text(s_name, app ? app->name : "App");
    lv_label_set_text(s_package, package);
    strlcpy(s_pkg, package ? package : "", sizeof(s_pkg));
    strlcpy(s_app_name, app ? app->name : "", sizeof(s_app_name));
    s_hides_title = app && app->hides_title;
    s_media_app = ui_app_plays_media(package);
    paint_note();
    bool no_skip = app && app->no_skip;
    lv_obj_set_hidden(s_prev, no_skip);
    lv_obj_set_hidden(s_next, no_skip);
    lv_obj_set_hidden(s_transport, !s_media_app);
    lv_obj_set_hidden(s_captions, !s_media_app);
    lv_obj_set_width(s_exit, s_media_app ? ACTION_W : LV_PCT(100));
}
