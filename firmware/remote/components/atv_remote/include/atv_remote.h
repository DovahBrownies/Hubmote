#pragma once

/*
 * Android TV Remote Service protocol v2 client: the protocol the Google TV
 * phone app uses. Pairing runs over TLS on port 6467, control over TLS on
 * port 6466. Every message is a protobuf prefixed with its varint length.
 * Reference implementation: tronikos/androidtvremote2 (remote.py,
 * pairing.py, remotemessage.proto, polo.proto).
 *
 * Threading: all network work runs on one internal task. Every function
 * below is safe to call from any task and returns immediately unless its
 * comment says "blocking". Events are delivered on the internal task through
 * the configured callback; the callback must not block for long (copy what
 * you need and hand it to your own task).
 *
 * Persistence (NVS, namespace from the config):
 *   - the client identity: RSA-2048 private key + self-signed certificate,
 *     generated once on the device;
 *   - the TV host (IP address or hostname) last used;
 *   - a hash of the TV's public key, pinned at pairing (or by the first
 *     control session without one): a host with another key is refused
 *     (ATV_ERR_WRONG_TV).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "atv_keycodes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ATV_STATE_IDLE = 0,       /* not connected, not trying */
    ATV_STATE_CONNECTING,     /* opening the control session on 6466 */
    ATV_STATE_CONNECTED,      /* control session up; keys can be sent */
    ATV_STATE_NOT_PAIRED,     /* the TV refused our certificate: pair first */
    ATV_STATE_PAIRING,        /* pairing session on 6467 being set up */
    ATV_STATE_PAIRING_CODE,   /* the TV is showing a 6-character code */
    ATV_STATE_DISCONNECTED,   /* lost the session; retrying with backoff */
} atv_state_t;

typedef enum {
    ATV_EVT_STATE = 0,   /* .state changed; .error says why for failures */
    ATV_EVT_POWER,       /* .is_on (remote_start.started) */
    ATV_EVT_APP,         /* .app_package (remote_ime_key_inject.app_info.app_package) */
    ATV_EVT_VOLUME,      /* .volume (remote_set_volume_level) */
    ATV_EVT_IME_SHOW,    /* remote_ime_show_request: a text field got focus; .ime */
    ATV_EVT_IME_TEXT,    /* remote_ime_key_inject.text_field_status changed; .ime */
    ATV_EVT_DEVICE_INFO, /* .device (remote_configure.device_info) */
    ATV_EVT_PAIR_RESULT, /* .error: ESP_OK = paired; ESP_ERR_INVALID_ARG = wrong code;
                            anything else = pairing failed */
} atv_event_type_t;

typedef struct {
    atv_event_type_t type;
    atv_state_t state; /* current state, filled for every event */
    esp_err_t error;   /* ESP_OK unless the event reports a failure */
    union {
        bool is_on;
        char app_package[128];
        struct {
            uint16_t level;
            uint16_t max;
            bool muted;
        } volume;
        struct {
            char value[128]; /* current field text, truncated */
            char label[64];  /* field hint/label if the TV sends one */
            int32_t start;
            int32_t end;
        } ime;
        struct {
            char vendor[32];
            char model[48];
            char app_version[24];
        } device;
    };
} atv_event_t;

typedef void (*atv_event_cb_t)(const atv_event_t *evt, void *user_ctx);

typedef struct {
    const char *client_name;   /* shown on the TV while pairing, e.g. "Hubmote" */
    const char *nvs_namespace; /* e.g. "atv" (max 15 chars) */
    bool enable_ime;           /* needed for current app and text entry */
    atv_event_cb_t on_event;
    void *user_ctx;
} atv_config_t;

typedef struct {
    char name[64];  /* mDNS instance name, e.g. "Living Room TV" */
    char host[40];  /* IPv4 address as text */
    uint16_t port;  /* control port advertised (normally 6466) */
} atv_device_t;

/* Starts the internal task. Call once, after esp_netif and NVS are up. */
esp_err_t atv_init(const atv_config_t *cfg);

/* True when a key + certificate are stored in NVS. */
bool atv_has_identity(void);

/* Blocking and slow (RSA-2048 key generation: seconds to about a minute).
 * Generates the identity and stores it in NVS. No-op if one exists. */
esp_err_t atv_generate_identity(void);

/* DISCONNECTED's .error when the host's key isn't the pinned one: it isn't
 * the paired TV. Retries go on with backoff. */
#define ATV_ERR_WRONG_TV 0x7A01

/* Remembers the TV address (persisted). Takes effect on the next connect. */
esp_err_t atv_set_host(const char *host);
/* Forgets the pinned key, for a TV the user picked (it pins on connect).
 * Queued like a command, so it applies before a connect asked for later. */
void atv_forget_tv_key(void);
/* True once a TV's key is pinned: this remote has been paired and connected. */
bool atv_tv_key_pinned(void);
/* Copies the stored host into buf; returns false when none is stored. */
bool atv_get_host(char *buf, size_t len);

/* Blocking: mDNS PTR query for "_androidtvremote2._tcp". */
esp_err_t atv_discover(atv_device_t *out, size_t max, size_t *found, uint32_t timeout_ms);

/* Opens the control session to the stored host. Reports CONNECTING then
 * CONNECTED, or NOT_PAIRED when the TV rejects the certificate. After a
 * connected session drops, retries with backoff until atv_disconnect(). */
esp_err_t atv_connect(void);
esp_err_t atv_disconnect(void);

/* Pairing: start makes the TV show a code (PAIRING then PAIRING_CODE).
 * finish takes the 6 hex characters the user read off the TV, reports
 * ATV_EVT_PAIR_RESULT, and on success connects the control session. */
esp_err_t atv_pair_start(void);
esp_err_t atv_pair_finish(const char *code);
esp_err_t atv_pair_cancel(void);

/* Control. Return ESP_ERR_INVALID_STATE when not CONNECTED. */
esp_err_t atv_send_key(atv_keycode_t key, atv_direction_t dir);
esp_err_t atv_launch_app_link(const char *link);
/* Replaces the focused field's text (RemoteImeBatchEdit, using the latest
 * ime/field counters the TV sent). */
esp_err_t atv_send_text(const char *text);

#ifdef __cplusplus
}
#endif
