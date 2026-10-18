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
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
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
typedef struct {
    ui_app_info_t tv[UI_MAX_APPS];
    char pkg[UI_MAX_APPS][UI_APP_PKG_MAX];
    char name[UI_MAX_APPS][UI_APP_NAME_MAX];
    lv_image_dsc_t icon[UI_MAX_APPS];
    lv_image_dsc_t icon_small[UI_MAX_APPS];
    /* Home's layout: package names, each between newlines
     * ("\npkg.a\npkg.b\n"). The controller saves the same strings in NVS. */
    char order[UI_APP_LIST_MAX];
    char hidden[UI_APP_LIST_MAX];
} apps_mem_t;
static apps_mem_t *s_mem;
static size_t s_tv_count;

/* The lists above take about 16 KB: in PSRAM, as internal RAM is scarce,
 * from the first call on (under the LVGL lock, as every call here). */
static apps_mem_t *mem(void)
{
    if (!s_mem) {
        s_mem = lv_malloc_zeroed(sizeof(*s_mem)); /* PSRAM: ui_mem.c */
        if (!s_mem) {
            ESP_LOGE("ui", "no memory for the app list");
            abort(); /* at start-up: nothing else would fit either */
        }
        strcpy(s_mem->order, "\n");
        strcpy(s_mem->hidden, "\n");
    }
    return s_mem;
}

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
    apps_mem_t *m = mem();
    s_tv_count = count < UI_MAX_APPS ? count : UI_MAX_APPS;
    for (size_t i = 0; i < s_tv_count; i++) {
        const ui_app_info_t *known = find_built_in(apps[i].pkg);
        strlcpy(m->pkg[i], apps[i].pkg, sizeof(m->pkg[i]));
        strlcpy(m->name[i], apps[i].label, sizeof(m->name[i]));
        m->tv[i] = (ui_app_info_t){
            .package = m->pkg[i],
            .name = m->name[i],
            .icon = describe(&m->icon[i], apps[i].icon, UI_APP_ICON),
            .icon_small = describe(&m->icon_small[i], apps[i].icon_small, UI_APP_ICON_SMALL),
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
        return mem()->tv;
    }
    *count = BUILT_IN_COUNT;
    return BUILT_IN;
}

const ui_app_info_t *ui_apps_find(const char *package)
{
    for (size_t i = 0; package && i < s_tv_count; i++) {
        if (strcmp(mem()->tv[i].package, package) == 0) {
            return &mem()->tv[i];
        }
    }
    return find_built_in(package);
}

/* Lists of package names, each between newlines ("\npkg.a\npkg.b\n"). */
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

/* Adds a package at the end; false when the list (cap bytes) is full. */
static bool list_append_cap(char *list, size_t cap, const char *package)
{
    size_t len = strlen(list);
    size_t n = strlen(package);
    if (!n || len + n + 2 > cap) {
        return false;
    }
    memcpy(list + len, package, n);
    list[len + n] = '\n';
    list[len + n + 1] = '\0';
    return true;
}

static bool list_append(char *list, const char *package)
{
    return list_append_cap(list, UI_APP_LIST_MAX, package);
}

static void list_load_cap(char *list, size_t cap, const char *saved)
{
    size_t len = saved ? strlen(saved) : 0;
    bool ok = len >= 1 && len < cap && saved[0] == '\n' && saved[len - 1] == '\n';
    strlcpy(list, ok ? saved : "\n", cap);
}

static void list_load(char *list, const char *saved)
{
    list_load_cap(list, UI_APP_LIST_MAX, saved);
}

/* Takes package out of list, if it's there. */
static void list_remove(char *list, const char *package)
{
    size_t n = 0;
    char *at = (char *)list_find(list, package, &n);
    if (at) {
        memmove(at + 1, at + n, strlen(at + n) + 1); /* drop "pkg\n", keep the '\n' before it */
    }
}

bool ui_apps_is_hidden(const char *package)
{
    size_t n;
    return list_find(mem()->hidden, package, &n) != NULL;
}

void ui_apps_set_hidden(const char *package, bool hidden)
{
    size_t n = 0;
    char *at = (char *)list_find(mem()->hidden, package, &n);
    if (hidden && !at) {
        list_append(mem()->hidden, package);
    } else if (!hidden && at) {
        memmove(at + 1, at + n, strlen(at + n) + 1); /* drop "pkg\n", keep the '\n' before it */
    }
}

int ui_apps_rank(const char *package)
{
    size_t n;
    const char *at = list_find(mem()->order, package, &n);
    return at ? (int)(at - mem()->order) : -1;
}

void ui_apps_set_order(const char *const *packages, size_t count)
{
    char *order = mem()->order;
    strlcpy(order, "\n", UI_APP_LIST_MAX);
    for (size_t i = 0; i < count && list_append(order, packages[i]); i++) {
    }
}

void ui_apps_load_layout(const char *order, const char *hidden)
{
    list_load(mem()->order, order);
    list_load(mem()->hidden, hidden);
}

void ui_apps_copy_layout(char *order, char *hidden)
{
    strlcpy(order, mem()->order, UI_APP_LIST_MAX);
    strlcpy(hidden, mem()->hidden, UI_APP_LIST_MAX);
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

/* --- Which apps get playback keys -------------------------------------------- */

/* Media apps the remote knows, so their keys are there before they play
 * anything; Plex never says it plays. Other apps get the keys once they
 * have played something (the companion reports it) or the user switches
 * them on. Add an app here with a pull request. */
static const char *const MEDIA_APPS[] = {
    "com.google.android.youtube.tvunplugged", /* YouTube TV */
    "com.google.android.videos",              /* Google TV (films and shows) */
    "com.apple.atve.androidtv.appletv",       /* Apple TV */
    "com.disney.disneyplus",
    "com.hulu.livingroomplus",
    "com.wbd.stream",                         /* Max */
    "com.cbs.ott",                            /* Paramount+ */
    "com.peacocktv.peacockandroid",
    "com.sling",
    "com.tubitv",
    "tv.pluto.android",
    "com.roku.web.trc",                       /* The Roku Channel */
    "com.xumo.xumo.tv",
    "com.crunchyroll.crunchyroid",
    "net.mbc.shahidTV",                       /* Shahid */
    "com.stremio.one",
    "org.xbmc.kodi",
    "org.videolan.vlc",
    "org.jellyfin.androidtv",
    "tv.emby.embyatv",
    "com.softmedia.receiver",                 /* AirReceiver */
};

/* Apps that have played something or the user switched on, and apps the
 * user switched off ("\npkg\n" lists, in PSRAM: internal RAM is scarce). */
static char *s_media_on;
static char *s_media_off;

static bool media_lists(void)
{
    if (!s_media_on) {
        s_media_on = lv_malloc(UI_MEDIA_LIST_MAX);
        s_media_off = lv_malloc(UI_MEDIA_LIST_MAX);
        if (!s_media_on || !s_media_off) {
            lv_free(s_media_on);
            lv_free(s_media_off);
            s_media_on = s_media_off = NULL;
            return false;
        }
        strlcpy(s_media_on, "\n", UI_MEDIA_LIST_MAX);
        strlcpy(s_media_off, "\n", UI_MEDIA_LIST_MAX);
    }
    return true;
}

static bool known_media(const char *package)
{
    if (find_built_in(package)) {
        return true;
    }
    for (size_t i = 0; i < sizeof(MEDIA_APPS) / sizeof(MEDIA_APPS[0]); i++) {
        if (strcmp(MEDIA_APPS[i], package) == 0) {
            return true;
        }
    }
    return false;
}

/* Adds package to list; when the list is full, the oldest entries go. */
static void media_list_add(char *list, const char *package)
{
    while (!list_append_cap(list, UI_MEDIA_LIST_MAX, package)) {
        char *second = strchr(list + 1, '\n');
        if (!second || !second[1]) {
            return; /* empty, or one package too long for the list */
        }
        memmove(list + 1, second + 1, strlen(second + 1) + 1);
    }
}

bool ui_app_plays_media(const char *package)
{
    size_t n;
    if (!package || !package[0]) {
        return false;
    }
    if (s_media_off && list_find(s_media_off, package, &n)) {
        return false;
    }
    return known_media(package) || (s_media_on && list_find(s_media_on, package, &n));
}

void ui_apps_load_media(const char *on, const char *off)
{
    if (media_lists()) {
        list_load_cap(s_media_on, UI_MEDIA_LIST_MAX, on);
        list_load_cap(s_media_off, UI_MEDIA_LIST_MAX, off);
    }
}

void ui_apps_copy_media(char *on, char *off)
{
    strlcpy(on, s_media_on ? s_media_on : "\n", UI_MEDIA_LIST_MAX);
    strlcpy(off, s_media_off ? s_media_off : "\n", UI_MEDIA_LIST_MAX);
}

bool ui_apps_learn_media(const char *package)
{
    size_t n;
    if (!package || !package[0] || !media_lists() || known_media(package) ||
        list_find(s_media_on, package, &n) || list_find(s_media_off, package, &n)) {
        return false;
    }
    media_list_add(s_media_on, package);
    return true;
}

void ui_apps_set_media_keys(const char *package, bool on)
{
    if (!package || !package[0] || !media_lists()) {
        return;
    }
    list_remove(s_media_on, package);
    list_remove(s_media_off, package);
    if (!on) {
        media_list_add(s_media_off, package);
    } else if (!known_media(package)) {
        media_list_add(s_media_on, package);
    }
}
