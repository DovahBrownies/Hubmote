#pragma once

/*
 * Updates from the published releases (main/update.c).
 * The remote reads update.json next to the web installer
 * (CONFIG_HM_UPDATE_URL), and on request installs the newer image over
 * HTTPS into the spare app slot. Rollback stays on (ota_mark_good()).
 */

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    UPDATE_UNKNOWN = 0, /* not checked yet */
    UPDATE_CHECKING,
    UPDATE_CURRENT,    /* the newest release, or newer */
    UPDATE_AVAILABLE,  /* .version is newer */
    UPDATE_INSTALLING, /* .percent done; restarts at 100 */
    UPDATE_FAILED,     /* the last check or install didn't work (log says why) */
} update_state_t;

/* The companion app in the release, if it has one (update.json
 * "companion"): the remote offers it in Settings > Remotes when the TV has
 * an older one. */
typedef struct {
    char version[16]; /* "0.5"; "" if the release has no companion */
    int code;         /* its Android version code */
    char apk[48];     /* file name next to update.json */
    int size;
    char sha256[65];
} update_companion_t;

typedef struct {
    update_state_t state;
    char version[32]; /* the release's version, once known */
    int percent;
    update_companion_t companion;
} update_status_t;

/* Runs on the update task: copy and queue, don't block. */
typedef void (*update_cb_t)(const update_status_t *status);

void update_start(update_cb_t cb);
/* Wi-Fi is up: the first automatic check follows a minute later. */
void update_net_up(void);
void update_check(void);
/* Installs the release found by the last check (checks first if needed). */
void update_install(void);
/* The address of a file next to update.json (e.g. the companion's APK). */
void update_file_url(char *out, size_t size, const char *name);
