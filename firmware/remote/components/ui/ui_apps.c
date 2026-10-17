/*
 * Apps the remote knows by package name.
 *
 * The companion app on the Streamer lists the installed apps with their
 * icons (ui_set_apps); those are shown and launched by package. Without it,
 * the built-in apps below are shown and launched by deep link. Deep links
 * follow Home Assistant's Android TV Remote docs; launching by package ID
 * over the remote protocol broke with Play Store 52.6 (Aug 2026). The
 * built-in apps have no icons of their own (the apps' logos aren't ours to
 * ship): they show as letter tiles until the companion sends real icons.
 */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

/* hides_title as measured on a Streamer (2026-10-16): Netflix's session
 * has no metadata, Plex publishes no session, Prime Video's title is
 * "PrimeVideo". */
static const ui_app_info_t BUILT_IN[] = {
    {"com.netflix.ninja", "Netflix", NULL, NULL, "https://www.netflix.com/title", true, false, true},
    {"com.google.android.youtube.tv", "YouTube", NULL, NULL, "https://www.youtube.com", false, false, false},
    {"com.plexapp.android", "Plex", NULL, NULL, "plex://", true, false, false},
    {"com.amazon.amazonvideo.livingroom", "Prime Video", NULL, NULL, "https://app.primevideo.com", true, false, false},
    {"com.spotify.tv.android", "Spotify", NULL, NULL, "spotify://", false, false, false},
};
#define BUILT_IN_COUNT (sizeof(BUILT_IN) / sizeof(BUILT_IN[0]))

/* System screens that cover the app for a while; the TV reports them as the
 * foreground app, but the app underneath is still the one in use. */
static const char *const SYSTEM_SCREENS[] = {
    "com.google.android.apps.tv.dreamx",        /* Google TV's screensaver (Ambient mode) */
    "com.google.android.backdrop",              /* the older screensaver */
    "com.android.dreams.basic",                 /* Android's own screensavers */
    "com.android.dreams.phototable",
    "com.google.android.katniss",               /* the Assistant */
    "com.android.systemui",                     /* system panels and dialogs */
    "com.google.android.systemui.gtvoverlayx",  /* Google TV's overlays (quick settings) */
    "com.google.android.tv.remote.service",
    "com.google.android.inputmethod.latin",     /* keyboards */
    "com.google.android.leanback.ime",
    "com.google.android.tungsten.setupwraith",  /* setup */
    "android",
};

bool ui_app_is_system_screen(const char *package)
{
    if (!package || !package[0]) {
        return false;
    }
    for (size_t i = 0; i < sizeof(SYSTEM_SCREENS) / sizeof(SYSTEM_SCREENS[0]); i++) {
        if (strcmp(SYSTEM_SCREENS[i], package) == 0) {
            return true;
        }
    }
    return strstr(package, ".dream") != NULL; /* other screensavers */
}

/* Apps with nothing to play: their panel leaves out the playback keys,
 * Captions and what's playing. Every other app keeps them: most apps on a
 * TV play something, and many never say so (Plex has no media session). */
static const char *const NOT_MEDIA[] = {
    "com.android.tv.settings",             /* Settings */
    "com.android.vending",                 /* Play Store */
    "com.google.android.gms",              /* Google account screens */
    "com.android.packageinstaller",        /* install dialogs */
    "com.google.android.packageinstaller",
    "com.android.documentsui",             /* files */
};

bool ui_app_plays_media(const char *package)
{
    for (size_t i = 0; package && i < sizeof(NOT_MEDIA) / sizeof(NOT_MEDIA[0]); i++) {
        if (strcmp(NOT_MEDIA[i], package) == 0) {
            return false;
        }
    }
    return true;
}

static const char *const LAUNCHERS[] = {
    "com.google.android.apps.tv.launcherx",
    "com.google.android.tvlauncher",
    "com.google.android.leanbacklauncher",
};

/* The TV's apps. Descriptors point at pixels the caller owns and are
 * reused for each new list. LVGL draws them straight from the descriptor;
 * with its image cache on, stale pixels could be drawn. */
#if LV_CACHE_DEF_SIZE > 0
#error "ui_apps_set_tv() reuses image descriptors: drop them from LVGL's image cache first"
#endif
static ui_app_info_t s_tv[UI_MAX_APPS];
static char s_tv_pkg[UI_MAX_APPS][UI_APP_PKG_MAX];
static char s_tv_name[UI_MAX_APPS][UI_APP_NAME_MAX];
static lv_image_dsc_t s_tv_icon[UI_MAX_APPS];
static lv_image_dsc_t s_tv_icon_small[UI_MAX_APPS];
static size_t s_tv_count;

static const ui_app_info_t *find_built_in(const char *package)
{
    for (size_t i = 0; package && i < BUILT_IN_COUNT; i++) {
        if (strcmp(BUILT_IN[i].package, package) == 0) {
            return &BUILT_IN[i];
        }
    }
    return NULL;
}

static const lv_image_dsc_t *describe(lv_image_dsc_t *dsc, const uint8_t *pixels, uint32_t size)
{
    if (!pixels) {
        return NULL;
    }
    *dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w = size, .h = size,
                   .stride = size * 2},
        .data_size = size * size * 3,
        .data = pixels,
    };
    return dsc;
}

void ui_apps_set_tv(const ui_app_t *apps, size_t count)
{
    s_tv_count = count < UI_MAX_APPS ? count : UI_MAX_APPS;
    for (size_t i = 0; i < s_tv_count; i++) {
        const ui_app_info_t *known = find_built_in(apps[i].pkg);
        strlcpy(s_tv_pkg[i], apps[i].pkg, sizeof(s_tv_pkg[i]));
        strlcpy(s_tv_name[i], apps[i].label, sizeof(s_tv_name[i]));
        s_tv[i] = (ui_app_info_t){
            .package = s_tv_pkg[i],
            .name = s_tv_name[i],
            .icon = describe(&s_tv_icon[i], apps[i].icon, UI_APP_ICON),
            .icon_small = describe(&s_tv_icon_small[i], apps[i].icon_small, UI_APP_ICON_SMALL),
            .link = known ? known->link : NULL,
            .hides_title = known && known->hides_title,
            .on_tv = true,
            .no_skip = known && known->no_skip,
        };
    }
}

const ui_app_info_t *ui_apps_home(size_t *count)
{
    if (s_tv_count) {
        *count = s_tv_count;
        return s_tv;
    }
    *count = BUILT_IN_COUNT;
    return BUILT_IN;
}

const ui_app_info_t *ui_apps_find(const char *package)
{
    for (size_t i = 0; package && i < s_tv_count; i++) {
        if (strcmp(s_tv[i].package, package) == 0) {
            return &s_tv[i];
        }
    }
    return find_built_in(package);
}

/* Home's layout: package names, each between newlines ("\npkg.a\npkg.b\n").
 * The controller saves the same strings in NVS. */
static char s_order[UI_APP_LIST_MAX] = "\n";
static char s_hidden[UI_APP_LIST_MAX] = "\n";

static const char *list_find(const char *list, const char *package, size_t *key_len)
{
    char key[104];
    int n = snprintf(key, sizeof(key), "\n%s\n", package ? package : "");
    if (n <= 2 || (size_t)n >= sizeof(key)) {
        return NULL;
    }
    *key_len = (size_t)n;
    return strstr(list, key);
}

/* Adds a package at the end; false when the list is full. */
static bool list_append(char *list, const char *package)
{
    size_t len = strlen(list);
    size_t n = strlen(package);
    if (!n || len + n + 2 > UI_APP_LIST_MAX) {
        return false;
    }
    memcpy(list + len, package, n);
    list[len + n] = '\n';
    list[len + n + 1] = '\0';
    return true;
}

static void list_load(char *list, const char *saved)
{
    size_t len = saved ? strlen(saved) : 0;
    bool ok = len >= 1 && len < UI_APP_LIST_MAX && saved[0] == '\n' && saved[len - 1] == '\n';
    strlcpy(list, ok ? saved : "\n", UI_APP_LIST_MAX);
}

bool ui_apps_is_hidden(const char *package)
{
    size_t n;
    return list_find(s_hidden, package, &n) != NULL;
}

void ui_apps_set_hidden(const char *package, bool hidden)
{
    size_t n = 0;
    char *at = (char *)list_find(s_hidden, package, &n);
    if (hidden && !at) {
        list_append(s_hidden, package);
    } else if (!hidden && at) {
        memmove(at + 1, at + n, strlen(at + n) + 1); /* drop "pkg\n", keep the '\n' before it */
    }
}

int ui_apps_rank(const char *package)
{
    size_t n;
    const char *at = list_find(s_order, package, &n);
    return at ? (int)(at - s_order) : -1;
}

void ui_apps_set_order(const char *const *packages, size_t count)
{
    strlcpy(s_order, "\n", sizeof(s_order));
    for (size_t i = 0; i < count && list_append(s_order, packages[i]); i++) {
    }
}

void ui_apps_load_layout(const char *order, const char *hidden)
{
    list_load(s_order, order);
    list_load(s_hidden, hidden);
}

void ui_apps_copy_layout(char *order, char *hidden)
{
    strlcpy(order, s_order, UI_APP_LIST_MAX);
    strlcpy(hidden, s_hidden, UI_APP_LIST_MAX);
}

bool ui_apps_is_launcher(const char *package)
{
    if (!package || !package[0]) {
        return true;
    }
    for (size_t i = 0; i < sizeof(LAUNCHERS) / sizeof(LAUNCHERS[0]); i++) {
        if (strcmp(LAUNCHERS[i], package) == 0) {
            return true;
        }
    }
    return false;
}
