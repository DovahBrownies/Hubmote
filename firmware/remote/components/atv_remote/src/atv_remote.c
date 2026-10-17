/*
 * Android TV Remote Service protocol v2 client.
 *
 * One internal task ("atv_remote") owns the TLS socket and the state
 * machine. Public calls validate what they can synchronously, post a
 * command to the queue and poke the task through a loopback UDP "wake"
 * socket, so the task can block in select() on the TLS socket and the wake
 * socket at the same time (no polling while a session is idle).
 *
 * Ported from tronikos/androidtvremote2:
 *   base.py      varint length-prefixed framing
 *   pairing.py   pairing session on port 6467 (polo.proto)
 *   remote.py    control session on port 6466 (remotemessage.proto)
 *   androidtv_remote.py  connect / reconnect / "needs pairing" logic
 *
 * Event error codes (atv_event_t.error):
 *   STATE NOT_PAIRED    ESP_ERR_NOT_ALLOWED  TLS on 6466 failed at the TLS layer
 *                                            (alert / protocol error): the TV
 *                                            does not accept our certificate
 *                       ESP_ERR_INVALID_STATE no usable identity in NVS
 *   STATE DISCONNECTED  ESP_ERR_TIMEOUT      TCP connect / TLS handshake timed
 *                                            out, or 16 s without traffic
 *                       ESP_ERR_NOT_FOUND    host name did not resolve
 *                       ESP_FAIL             refused, reset, closed by the TV
 *   STATE IDLE          ESP_OK after atv_disconnect()/atv_pair_cancel(),
 *                       the PAIR_RESULT error after a failed pairing,
 *                       ESP_ERR_NOT_FOUND when no host is stored
 *   PAIR_RESULT         ESP_OK                paired (control session follows)
 *                       ESP_ERR_INVALID_ARG   code does not match (nothing was
 *                                             sent; still PAIRING_CODE, retry)
 *                       ESP_ERR_TIMEOUT       TV did not answer within 10 s
 *                       ESP_ERR_INVALID_RESPONSE TV answered status != 200
 *                                             (e.g. 402 bad secret) or sent
 *                                             something unexpected
 *                       ESP_ERR_NOT_ALLOWED   TLS refused on 6467
 *                       ESP_ERR_NOT_SUPPORTED a certificate is not RSA
 *                       ESP_FAIL / others     connection failed or closed
 *                                             (e.g. cancelled on the TV)
 */

#include "atv_remote.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atv_identity.h"
#include "atv_msg.h"
#include "atv_pb.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/bignum.h"
#include "mbedtls/cipher.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/md.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#include "nvs.h"
#include "sdkconfig.h"
#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
#include "psa/crypto.h"
#endif

#if !defined(MBEDTLS_SSL_KEEP_PEER_CERTIFICATE)
#error "atv_remote needs CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=y (the TV's public key enters the pairing secret)"
#endif

static const char *TAG = "atv";

#define ATV_PORT_CONTROL         6466
#define ATV_PORT_PAIRING         6467
#define TCP_CONNECT_TIMEOUT_MS   5000
#define TLS_HANDSHAKE_TIMEOUT_MS 10000              /* per handshake read */
#define WRITE_TIMEOUT_MS         5000
#define PAIRING_TIMEOUT_US       (10 * 1000000LL)   /* pairing.py PAIRING_TIMEOUT */
#define IDLE_TIMEOUT_US          (16 * 1000000LL)   /* remote.py _async_idle_disconnect */
#define BACKOFF_MAX_S            30
#define POST_PAIR_DELAY_US       (300 * 1000LL)     /* let the TV store the new cert */
#define POLL_FALLBACK_MS         50                 /* only without the wake socket */
#define MAX_SELECT_WAIT_US       (60 * 1000000LL)

#define RX_BUF_LEN    4096 /* larger messages are skipped */
#define TX_BUF_LEN    2048
#define MAX_TEXT_LEN  1024
#define MAX_LINK_LEN  1024
#define HOST_MAX      64
#define CMD_QUEUE_LEN 16
#define NVS_KEY_HOST  "host"
#define NVS_KEY_TV_KEY "tv_key" /* SHA-256 of the TV's public key */
#define PUBKEY_DER_MAX 600      /* an RSA-4096 SubjectPublicKeyInfo fits */

#define ERROR_SUGGESTION_MSG                                                                                     \
    "Try clearing the storage of the Android TV Remote Service system app. On the Android TV device, go to "    \
    "Settings > Apps > See all apps > Show system apps. Then, select Android TV Remote Service > Storage > Clear " \
    "data/storage."

/* ------------------------------------------------------------------ types */

typedef enum {
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_PAIR_START,
    CMD_PAIR_FINISH,
    CMD_PAIR_CANCEL,
    CMD_KEY,
    CMD_APP_LINK,
    CMD_TEXT,
    CMD_FORGET_KEY,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    int32_t key;
    int32_t dir;
    char code[8];
    char *str; /* heap copy (app link / text), freed by the task */
} cmd_t;

typedef enum {
    SESS_NONE = 0,
    SESS_CONTROL,
    SESS_PAIRING,
} sess_kind_t;

typedef enum {
    PAIR_WAIT_ACK,        /* sent pairing_request */
    PAIR_WAIT_OPTIONS,    /* sent options */
    PAIR_WAIT_CONFIG_ACK, /* sent configuration */
    PAIR_WAIT_CODE,       /* TV shows the code; waiting for atv_pair_finish */
    PAIR_WAIT_SECRET_ACK, /* sent secret */
} pair_step_t;

/* Everything the task owns. Allocated once, preferably in PSRAM. */
typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt own_cert;
    mbedtls_pk_context own_key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    bool drbg_ready;

    sess_kind_t kind;
    uint32_t gen; /* bumped on every session open/close */

    uint8_t rx[RX_BUF_LEN];
    size_t rx_len;
    size_t rx_skip; /* bytes of an oversized message still to discard */
    uint8_t tx[TX_BUF_LEN];
    atv_remote_msg_t rmsg;
    atv_polo_msg_t pmsg;

    /* control session (per connection, like a RemoteProtocol instance) */
    int32_t features;
    bool started; /* remote_start seen: CONNECTED */
    int32_t ime_counter;
    int32_t field_counter;
    int64_t last_activity_us;

    /* pairing session */
    pair_step_t pair_step;
    int64_t pair_deadline_us; /* 0 = none */

    /* (re)connect */
    bool want_connected;
    bool connect_pending; /* connect at retry_at_us without a DISCONNECTED state */
    uint32_t backoff_s;
    int64_t retry_at_us;

    char host[HOST_MAX];
} ctx_t;

static struct {
    bool inited;
    char client_name[64];
    char nvs_ns[16];
    bool enable_ime;
    atv_event_cb_t on_event;
    void *user_ctx;

    QueueHandle_t queue;
    TaskHandle_t task;
    volatile atv_state_t state;

    SemaphoreHandle_t lock; /* host copy + wake socket */
    char host[HOST_MAX];
    bool has_host;

    int wake_rx;
    int wake_tx;
    struct sockaddr_in wake_addr;

    ctx_t *ctx;
} s = {.wake_rx = -1, .wake_tx = -1};

/* ---------------------------------------------------------------- helpers */

static const char *state_name(atv_state_t st)
{
    static const char *const names[] = {"IDLE",    "CONNECTING",   "CONNECTED",   "NOT_PAIRED",
                                        "PAIRING", "PAIRING_CODE", "DISCONNECTED"};
    return (unsigned)st < sizeof(names) / sizeof(names[0]) ? names[st] : "?";
}

static const char *dir_name(int32_t d)
{
    switch (d) {
    case ATV_DIR_START_LONG: return "START_LONG";
    case ATV_DIR_END_LONG: return "END_LONG";
    case ATV_DIR_SHORT: return "SHORT";
    default: return "UNKNOWN";
    }
}

static const char *tls_err_str(int ret, char *buf, size_t len)
{
    /* ESP-IDF's socket layer is not MBEDTLS_NET_C, so strerror has no text
     * for these. */
    const char *net = ret == MBEDTLS_ERR_NET_CONN_RESET    ? "connection reset by peer"
                      : ret == MBEDTLS_ERR_NET_RECV_FAILED ? "receive failed"
                      : ret == MBEDTLS_ERR_NET_SEND_FAILED ? "send failed"
                                                           : NULL;
    if (net) {
        snprintf(buf, len, "-0x%04x %s", (unsigned)-ret, net);
        return buf;
    }
#if defined(MBEDTLS_ERROR_C)
    char tmp[80];
    mbedtls_strerror(ret, tmp, sizeof(tmp));
    snprintf(buf, len, "-0x%04x %s", (unsigned)-ret, tmp);
#else
    snprintf(buf, len, "-0x%04x", (unsigned)-ret);
#endif
    return buf;
}

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void *calloc_prefer_psram(size_t n, size_t size)
{
    return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
}

static bool host_is_ipv4(const char *host)
{
    struct in_addr a;
    return inet_pton(AF_INET, host, &a) == 1;
}

/* --------------------------------------------------------------- events */

static void emit(atv_event_t *e)
{
    e->state = s.state;
    if (s.on_event) {
        s.on_event(e, s.user_ctx);
    }
}

static void set_state(atv_state_t st, esp_err_t err)
{
    atv_state_t old = s.state;
    if (st == old) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "state %s -> %s (%s)", state_name(old), state_name(st), esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "state %s -> %s", state_name(old), state_name(st));
    }
    s.state = st;
    atv_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = ATV_EVT_STATE;
    e.error = err;
    emit(&e);
}

static void emit_pair_result(esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "pairing succeeded");
    } else {
        ESP_LOGW(TAG, "pairing result: %s", esp_err_to_name(err));
    }
    atv_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = ATV_EVT_PAIR_RESULT;
    e.error = err;
    emit(&e);
}

static void fill_ime(atv_event_t *e, const atv_text_field_t *tf)
{
    strlcpy(e->ime.value, tf->value, sizeof(e->ime.value));
    strlcpy(e->ime.label, tf->label, sizeof(e->ime.label));
    e->ime.start = tf->start;
    e->ime.end = tf->end;
}

/* ------------------------------------------------------------ wake socket */

static void wake_init(void)
{
    int rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    socklen_t al = sizeof(a);
    if (rx < 0 || tx < 0 || bind(rx, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        getsockname(rx, (struct sockaddr *)&a, &al) != 0) {
        ESP_LOGW(TAG, "no loopback wake socket (errno %d): polling every %d ms while a session is open", errno,
                 POLL_FALLBACK_MS);
        if (rx >= 0) {
            close(rx);
        }
        if (tx >= 0) {
            close(tx);
        }
        return;
    }
    fcntl(rx, F_SETFL, fcntl(rx, F_GETFL, 0) | O_NONBLOCK);
    fcntl(tx, F_SETFL, fcntl(tx, F_GETFL, 0) | O_NONBLOCK);
    s.wake_addr = a;
    s.wake_rx = rx;
    s.wake_tx = tx;
}

static void wake_task(void)
{
    if (s.wake_tx < 0) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    /* Failure (e.g. loopback queue full) is fine: the task is already awake. */
    sendto(s.wake_tx, "w", 1, 0, (struct sockaddr *)&s.wake_addr, sizeof(s.wake_addr));
    xSemaphoreGive(s.lock);
}

static void wake_drain(void)
{
    if (s.wake_rx < 0) {
        return;
    }
    char b[8];
    while (recv(s.wake_rx, b, sizeof(b), MSG_DONTWAIT) > 0) {
    }
}

/* ---------------------------------------------------------------- TLS/TCP */

static esp_err_t ensure_drbg(ctx_t *c)
{
    if (c->drbg_ready) {
        return ESP_OK;
    }
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->drbg);
    static const unsigned char pers[] = "atv_remote session";
    int ret = mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->entropy, pers, sizeof(pers) - 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "ctr_drbg seed failed (-0x%04x)", (unsigned)-ret);
        mbedtls_ctr_drbg_free(&c->drbg);
        mbedtls_entropy_free(&c->entropy);
        return ESP_FAIL;
    }
#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
    psa_crypto_init();
#endif
    c->drbg_ready = true;
    return ESP_OK;
}

static void tls_init(ctx_t *c)
{
    mbedtls_net_init(&c->net);
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_x509_crt_init(&c->own_cert);
    mbedtls_pk_init(&c->own_key);
}

static void tls_free(ctx_t *c)
{
    mbedtls_net_free(&c->net);
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_x509_crt_free(&c->own_cert);
    mbedtls_pk_free(&c->own_key);
}

/* Internal only: a control-port TCP connect was cut short because a command
 * arrived (never reported in an event). */
#define ATV_ERR_INTERRUPTED ESP_ERR_NOT_FINISHED

/* TCP connect with a timeout (lwIP's own SYN retries take over a minute).
 * When interruptible, a poke on the wake socket (a new command) aborts the
 * wait so atv_disconnect()/atv_pair_start() are not stuck behind it. */
static esp_err_t tcp_connect(const char *host, uint16_t port, int *out_fd, bool interruptible)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        ESP_LOGW(TAG, "cannot resolve %s", host);
        return ESP_ERR_NOT_FOUND;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return ESP_FAIL;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && errno != EINPROGRESS) {
        ESP_LOGD(TAG, "connect: errno %d", errno);
        close(fd);
        return ESP_FAIL;
    }
    if (r != 0) {
        fd_set rfds, wfds, efds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(fd, &wfds);
        FD_SET(fd, &efds);
        int maxfd = fd;
        bool watch_wake = interruptible && s.wake_rx >= 0;
        if (watch_wake) {
            FD_SET(s.wake_rx, &rfds);
            if (s.wake_rx > maxfd) {
                maxfd = s.wake_rx;
            }
        }
        struct timeval tv = {.tv_sec = TCP_CONNECT_TIMEOUT_MS / 1000, .tv_usec = (TCP_CONNECT_TIMEOUT_MS % 1000) * 1000};
        r = select(maxfd + 1, &rfds, &wfds, &efds, &tv);
        if (r <= 0) {
            close(fd);
            return r == 0 ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        if (!FD_ISSET(fd, &wfds) && !FD_ISSET(fd, &efds)) {
            close(fd); /* only the wake socket fired */
            return ATV_ERR_INTERRUPTED;
        }
        int so_err = 0;
        socklen_t len = sizeof(so_err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) != 0 || so_err != 0) {
            ESP_LOGD(TAG, "connect: SO_ERROR %d", so_err);
            close(fd);
            return so_err == ETIMEDOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
    }
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); /* asyncio does the same */
    *out_fd = fd;
    return ESP_OK;
}

/* Which TLS failures mean "the TV does not accept us". The reference maps
 * any ssl.SSLError to InvalidAuth ("Need to pair") and OSErrors (refused,
 * reset, timeout, EOF during the handshake = asyncio ConnectionResetError)
 * to CannotConnect (retry). Transport, timeout, EOF and out-of-memory
 * errors are therefore retryable; every other TLS-layer failure (fatal
 * alert such as certificate_unknown, protocol errors) is a rejection. */
static bool is_tls_rejection(int ret)
{
    if (ret >= -0x0052 && ret <= -0x0042) { /* MBEDTLS_ERR_NET_* */
        return false;
    }
    switch (ret) {
    case 0:
    case MBEDTLS_ERR_SSL_WANT_READ:
    case MBEDTLS_ERR_SSL_WANT_WRITE:
    case MBEDTLS_ERR_SSL_TIMEOUT:
    case MBEDTLS_ERR_SSL_CONN_EOF:
    case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
    case MBEDTLS_ERR_SSL_ALLOC_FAILED:
    case MBEDTLS_ERR_MPI_ALLOC_FAILED:
    case MBEDTLS_ERR_PK_ALLOC_FAILED:
    case MBEDTLS_ERR_X509_ALLOC_FAILED:
    case MBEDTLS_ERR_ECP_ALLOC_FAILED:
    case MBEDTLS_ERR_CIPHER_ALLOC_FAILED:
    case MBEDTLS_ERR_MD_ALLOC_FAILED:
        return false;
    default:
        return true;
    }
}

/* Opens TCP + TLS to c->host:port with our certificate. On success the
 * socket is switched to non-blocking and c->kind is set. */
static esp_err_t session_open(ctx_t *c, uint16_t port, sess_kind_t kind)
{
    char eb[112];
    esp_err_t err = ensure_drbg(c);
    if (err != ESP_OK) {
        return err;
    }
    tls_init(c);
    err = atv_id_load(s.nvs_ns, &c->own_cert, &c->own_key, mbedtls_ctr_drbg_random, &c->drbg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no usable identity in NVS (%s); call atv_generate_identity()", esp_err_to_name(err));
        tls_free(c);
        return ESP_ERR_INVALID_STATE;
    }

    int64_t t0 = now_us();
    err = tcp_connect(c->host, port, &c->net.fd, kind == SESS_CONTROL);
    if (err == ATV_ERR_INTERRUPTED) {
        ESP_LOGD(TAG, "TCP connect to %s:%u interrupted by a command", c->host, port);
        tls_free(c);
        return err;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TCP connect to %s:%u failed: %s", c->host, port, esp_err_to_name(err));
        tls_free(c);
        return err;
    }

    int ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret == 0) {
        /* The TV's certificate is self-signed: no verification (like
         * ssl.CERT_NONE in the reference). Its key is still kept
         * (MBEDTLS_SSL_KEEP_PEER_CERTIFICATE) for the pairing secret. */
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);
        mbedtls_ssl_conf_read_timeout(&c->conf, TLS_HANDSHAKE_TIMEOUT_MS);
        ret = mbedtls_ssl_conf_own_cert(&c->conf, &c->own_cert, &c->own_key);
    }
    if (ret == 0) {
        ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
    }
    if (ret == 0 && !host_is_ipv4(c->host)) {
        ret = mbedtls_ssl_set_hostname(&c->ssl, c->host); /* SNI, as Python sends for names */
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "TLS setup failed: %s", tls_err_str(ret, eb, sizeof(eb)));
        tls_free(c);
        return ret == MBEDTLS_ERR_SSL_ALLOC_FAILED ? ESP_ERR_NO_MEM : ESP_FAIL;
    }

    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
    do {
        ret = mbedtls_ssl_handshake(&c->ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret != 0) {
        /* The TV answers an unknown client certificate with a
         * certificate_unknown alert and resets the connection right after.
         * lwIP reports a pending reset before data still queued, so the
         * alert is lost and the handshake ends in NET_CONN_RESET instead. A
         * reset once our certificate is out (TLS 1.2: waiting for the
         * server's ChangeCipherSpec/Finished) is that rejection. */
        int hs_state = c->ssl.MBEDTLS_PRIVATE(state);
        bool rejected = is_tls_rejection(ret) ||
                        (ret == MBEDTLS_ERR_NET_CONN_RESET && hs_state > MBEDTLS_SSL_CLIENT_CERTIFICATE &&
                         hs_state <= MBEDTLS_SSL_HANDSHAKE_WRAPUP);
        ESP_LOGW(TAG, "TLS handshake with %s:%u failed in state %d: %s%s", c->host, port, hs_state,
                 tls_err_str(ret, eb, sizeof(eb)), rejected ? " -> certificate not accepted" : "");
        tls_free(c);
        if (rejected) {
            return ESP_ERR_NOT_ALLOWED;
        }
        if (ret == MBEDTLS_ERR_SSL_ALLOC_FAILED) {
            return ESP_ERR_NO_MEM;
        }
        return ret == MBEDTLS_ERR_SSL_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }

    mbedtls_net_set_nonblock(&c->net);
    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, mbedtls_net_recv, NULL);
    c->kind = kind;
    c->gen++;
    c->rx_len = 0;
    c->rx_skip = 0;
    ESP_LOGI(TAG, "TLS up with %s:%u (%s, %s) in %d ms", c->host, port, mbedtls_ssl_get_version(&c->ssl),
             mbedtls_ssl_get_ciphersuite(&c->ssl), (int)((now_us() - t0) / 1000));
    ESP_LOGD(TAG, "task stack: %u bytes never used", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return ESP_OK;
}

static void session_close(ctx_t *c)
{
    if (c->kind == SESS_NONE) {
        return;
    }
    mbedtls_ssl_close_notify(&c->ssl); /* best effort, non-blocking */
    tls_free(c);
    c->kind = SESS_NONE;
    c->gen++;
    c->rx_len = 0;
    c->rx_skip = 0;
    c->started = false;
    c->pair_deadline_us = 0;
}

static void wait_fd(int fd, bool for_write, int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    select(fd + 1, for_write ? NULL : &fds, for_write ? &fds : NULL, NULL, &tv);
}

static esp_err_t tls_write_all(ctx_t *c, const uint8_t *p, size_t len)
{
    int64_t deadline = now_us() + WRITE_TIMEOUT_MS * 1000LL;
    while (len > 0) {
        int ret = mbedtls_ssl_write(&c->ssl, p, len);
        if (ret > 0) {
            p += ret;
            len -= (size_t)ret;
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
            if (now_us() >= deadline) {
                ESP_LOGW(TAG, "TLS write timed out");
                return ESP_ERR_TIMEOUT;
            }
            wait_fd(c->net.fd, ret == MBEDTLS_ERR_SSL_WANT_WRITE, 100);
            continue;
        }
        char eb[112];
        ESP_LOGW(TAG, "TLS write failed: %s", tls_err_str(ret, eb, sizeof(eb)));
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ------------------------------------------------------- state transitions */

static void schedule_retry(ctx_t *c, esp_err_t err)
{
    uint32_t d = c->backoff_s ? c->backoff_s : 1;
    c->retry_at_us = now_us() + (int64_t)d * 1000000LL;
    c->backoff_s = (d * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : d * 2;
    ESP_LOGI(TAG, "reconnecting in %u s", (unsigned)d);
    set_state(ATV_STATE_DISCONNECTED, err);
}

static void control_lost(ctx_t *c, esp_err_t err, bool rejected)
{
    bool was_started = c->started;
    session_close(c);
    if (rejected && !was_started) {
        /* TLS failure before remote_start: like the reference's
         * "Need to pair again" when on_con_lost carries an SSLError. */
        ESP_LOGW(TAG, "TV rejected the connection at the TLS layer: pairing needed");
        c->want_connected = false;
        set_state(ATV_STATE_NOT_PAIRED, ESP_ERR_NOT_ALLOWED);
        return;
    }
    if (c->want_connected) {
        schedule_retry(c, err);
    } else {
        set_state(ATV_STATE_IDLE, err);
    }
}

static void pairing_fail(ctx_t *c, esp_err_t err)
{
    session_close(c);
    emit_pair_result(err);
    set_state(ATV_STATE_IDLE, err);
}

/* Session-level failure from a read/write path. tls_ret is the mbedTLS code
 * (0 when not a TLS-layer error). */
static void session_broken(ctx_t *c, esp_err_t err, int tls_ret)
{
    if (c->kind == SESS_PAIRING) {
        pairing_fail(c, err);
    } else if (c->kind == SESS_CONTROL) {
        control_lost(c, err, tls_ret != 0 && is_tls_rejection(tls_ret));
    }
}

static void refresh_host(ctx_t *c)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.has_host) {
        strlcpy(c->host, s.host, sizeof(c->host));
    } else {
        c->host[0] = '\0';
    }
    xSemaphoreGive(s.lock);
}

/* SHA-256 of the peer's public key (its DER SubjectPublicKeyInfo). */
static bool peer_key_hash(ctx_t *c, uint8_t out[32])
{
    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&c->ssl);
    if (!peer) {
        return false;
    }
    uint8_t der[PUBKEY_DER_MAX]; /* on the stack: no allocation that could fail */
    /* Writes at the end of the buffer. */
    int n = mbedtls_pk_write_pubkey_der((mbedtls_pk_context *)&peer->pk, der, sizeof(der));
    return n > 0 && mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), der + sizeof(der) - n, (size_t)n,
                               out) == 0;
}

static esp_err_t save_tv_key(const uint8_t hash[32])
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(s.nvs_ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY_TV_KEY, hash, 32);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

/* The TV's certificate is self-signed, so its key is pinned instead: pairing
 * stores it (the Streamer shows the same key on both ports), and every
 * control session must show the same key. Something else answering at the
 * TV's address (or under its mDNS name after a move) never gets a key
 * press, text or the companion. With no key stored (paired by an older
 * firmware, or a TV the user picked), the first control session stores it. */
static esp_err_t check_tv_key(ctx_t *c)
{
    uint8_t got[32];
    if (!peer_key_hash(c, got)) {
        ESP_LOGE(TAG, "the TV's key can't be read");
        return ATV_ERR_WRONG_TV;
    }
    nvs_handle_t h;
    if (nvs_open(s.nvs_ns, NVS_READONLY, &h) != ESP_OK) {
        return ATV_ERR_WRONG_TV;
    }
    uint8_t pin[32];
    size_t len = sizeof(pin);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_TV_KEY, pin, &len);
    nvs_close(h);
    if (err == ESP_OK) {
        return len == sizeof(pin) && memcmp(pin, got, sizeof(pin)) == 0 ? ESP_OK : ATV_ERR_WRONG_TV;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "the pinned key can't be read: %s", esp_err_to_name(err));
        return ATV_ERR_WRONG_TV; /* fail closed: Find TVs (the user picking a TV) starts over */
    }
    err = save_tv_key(got);
    ESP_LOGI(TAG, "the TV's key is pinned%s", err == ESP_OK ? "" : " for now (not saved)");
    return ESP_OK;
}

static void forget_tv_key(void)
{
    nvs_handle_t h;
    if (nvs_open(s.nvs_ns, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_erase_key(h, NVS_KEY_TV_KEY) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

static void control_connect(ctx_t *c)
{
    refresh_host(c);
    if (!c->host[0]) {
        c->want_connected = false;
        set_state(ATV_STATE_IDLE, ESP_ERR_NOT_FOUND);
        return;
    }
    set_state(ATV_STATE_CONNECTING, ESP_OK);
    esp_err_t err = session_open(c, ATV_PORT_CONTROL, SESS_CONTROL);
    if (err == ESP_OK && check_tv_key(c) != ESP_OK) {
        ESP_LOGW(TAG, "%s has a different key from the paired TV: not using it", c->host);
        session_close(c);
        err = ATV_ERR_WRONG_TV;
    }
    if (err == ESP_OK) {
        c->started = false;
        c->features = ATV_FEATURE_PING | ATV_FEATURE_KEY | ATV_FEATURE_POWER | ATV_FEATURE_VOLUME |
                      ATV_FEATURE_APP_LINK | (s.enable_ime ? ATV_FEATURE_IME : 0);
        c->ime_counter = 0;
        c->field_counter = 0;
        c->last_activity_us = now_us();
        return; /* CONNECTED once remote_start arrives */
    }
    if (err == ATV_ERR_INTERRUPTED) {
        /* Handle the new command(s) first; then (if still wanted) try again
         * right away. State stays CONNECTING. */
        c->connect_pending = true;
        c->retry_at_us = now_us();
        return;
    }
    if (err == ESP_ERR_NOT_ALLOWED || err == ESP_ERR_INVALID_STATE) {
        c->want_connected = false;
        set_state(ATV_STATE_NOT_PAIRED, err);
        return;
    }
    schedule_retry(c, err);
}

/* ------------------------------------------------------------- sending */

#define TX_BEGIN(c, w) pb_writer_init(&(w), (c)->tx + PB_VARINT_MAX, TX_BUF_LEN - PB_VARINT_MAX)

static esp_err_t tx_flush(ctx_t *c, pb_writer_t *w, bool built)
{
    if (!built) {
        ESP_LOGE(TAG, "message does not fit the %d-byte transmit buffer", TX_BUF_LEN);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t flen;
    uint8_t *f = atv_frame_finish(c->tx, w->len, &flen);
    esp_err_t err = tls_write_all(c, f, flen);
    if (err != ESP_OK) {
        session_broken(c, err, 0);
    }
    return err;
}

/* ------------------------------------------------------- control session */

static void log_rx_remote(const atv_remote_msg_t *m, size_t len)
{
    uint32_t p = m->present;
    if (p & ATV_RM_CONFIGURE) {
        ESP_LOGD(TAG, "rx remote_configure code1=0x%x vendor='%s' model='%s' app_version='%s'",
                 (unsigned)m->configure_code1, m->vendor, m->model, m->app_version);
    }
    if (p & ATV_RM_SET_ACTIVE) {
        ESP_LOGD(TAG, "rx remote_set_active active=0x%x", (unsigned)m->set_active);
    }
    if (p & ATV_RM_ERROR) {
        ESP_LOGD(TAG, "rx remote_error value=%d", m->error_value);
    }
    if (p & ATV_RM_PING_REQUEST) {
        ESP_LOGD(TAG, "rx remote_ping_request val1=%d val2=%d", (int)m->ping_val1, (int)m->ping_val2);
    }
    if (p & ATV_RM_IME_KEY_INJECT) {
        ESP_LOGD(TAG, "rx remote_ime_key_inject app_package='%s'%s%s%s", m->app_package,
                 m->has_ime_text_field ? " text='" : "", m->has_ime_text_field ? m->ime_text_field.value : "",
                 m->has_ime_text_field ? "'" : "");
    }
    if (p & ATV_RM_IME_BATCH_EDIT) {
        ESP_LOGD(TAG, "rx remote_ime_batch_edit ime_counter=%d field_counter=%d", (int)m->ime_counter,
                 (int)m->field_counter);
    }
    if (p & ATV_RM_IME_SHOW_REQUEST) {
        ESP_LOGD(TAG, "rx remote_ime_show_request value='%s' label='%s'", m->show_text_field.value,
                 m->show_text_field.label);
    }
    if (p & ATV_RM_VOICE_BEGIN) {
        ESP_LOGD(TAG, "rx remote_voice_begin session_id=%d", (int)m->voice_session_id);
    }
    if (p & ATV_RM_START) {
        ESP_LOGD(TAG, "rx remote_start started=%d", m->started);
    }
    if (p & ATV_RM_SET_VOLUME_LEVEL) {
        ESP_LOGD(TAG, "rx remote_set_volume_level level=%u max=%u muted=%d", (unsigned)m->volume_level,
                 (unsigned)m->volume_max, m->volume_muted);
    }
    const uint32_t logged = ATV_RM_CONFIGURE | ATV_RM_SET_ACTIVE | ATV_RM_ERROR | ATV_RM_PING_REQUEST |
                            ATV_RM_IME_KEY_INJECT | ATV_RM_IME_BATCH_EDIT | ATV_RM_IME_SHOW_REQUEST |
                            ATV_RM_VOICE_BEGIN | ATV_RM_START | ATV_RM_SET_VOLUME_LEVEL;
    if (!(p & logged)) {
        ESP_LOGD(TAG, "rx RemoteMessage (%u bytes, fields 0x%x)", (unsigned)len, (unsigned)p);
    }
}

static void send_ping_response(ctx_t *c, int32_t val1)
{
    pb_writer_t w;
    TX_BEGIN(c, w);
    bool ok = atv_msg_ping_response(&w, val1);
    ESP_LOGD(TAG, "tx remote_ping_response val1=%d", (int)val1);
    tx_flush(c, &w, ok);
}

static void control_on_message(ctx_t *c, const uint8_t *buf, size_t len)
{
    c->last_activity_us = now_us();
    atv_remote_msg_t *m = &c->rmsg;
    if (!atv_parse_remote(buf, len, m)) {
        ESP_LOGD(TAG, "rx: couldn't parse RemoteMessage (%u bytes), ignored", (unsigned)len);
        return;
    }
    log_rx_remote(m, len);

    pb_writer_t w;
    atv_event_t e;
    memset(&e, 0, sizeof(e));
    uint32_t p = m->present;

    /* Same if/elif order as remote.py _handle_message. */
    if (p & ATV_RM_CONFIGURE) {
        int32_t tv = m->configure_code1;
        if (!(tv & ATV_FEATURE_KEY)) {
            ESP_LOGE(TAG, "Device doesn't support sending keys. %s", ERROR_SUGGESTION_MSG);
        }
        if (!(tv & ATV_FEATURE_APP_LINK)) {
            ESP_LOGE(TAG, "Device doesn't support sending app links. %s", ERROR_SUGGESTION_MSG);
        }
        c->features &= tv;
        TX_BEGIN(c, w);
        bool ok = atv_msg_configure(&w, c->features);
        ESP_LOGD(TAG, "tx remote_configure code1=0x%x device_info{unknown1=%d unknown2='%s' package_name='%s' "
                      "app_version='%s'}",
                 (unsigned)c->features, ATV_DEVINFO_UNKNOWN1, ATV_DEVINFO_UNKNOWN2, ATV_DEVINFO_PACKAGE_NAME,
                 ATV_DEVINFO_APP_VERSION);
        if (tx_flush(c, &w, ok) != ESP_OK) {
            return;
        }
        if (m->has_device_info) {
            e.type = ATV_EVT_DEVICE_INFO;
            strlcpy(e.device.vendor, m->vendor, sizeof(e.device.vendor));
            strlcpy(e.device.model, m->model, sizeof(e.device.model));
            strlcpy(e.device.app_version, m->app_version, sizeof(e.device.app_version));
            ESP_LOGI(TAG, "TV: %s %s, remote service %s, features 0x%x (active 0x%x)", m->vendor, m->model,
                     m->app_version, (unsigned)tv, (unsigned)c->features);
            emit(&e);
        }
    } else if (p & ATV_RM_SET_ACTIVE) {
        TX_BEGIN(c, w);
        bool ok = atv_msg_set_active(&w, c->features);
        ESP_LOGD(TAG, "tx remote_set_active active=0x%x", (unsigned)c->features);
        tx_flush(c, &w, ok);
    } else if (p & ATV_RM_IME_KEY_INJECT) {
        if (m->has_app_info) {
            e.type = ATV_EVT_APP;
            strlcpy(e.app_package, m->app_package, sizeof(e.app_package));
            emit(&e);
        }
        if (m->has_ime_text_field) {
            memset(&e, 0, sizeof(e));
            e.type = ATV_EVT_IME_TEXT;
            fill_ime(&e, &m->ime_text_field);
            emit(&e);
        }
    } else if (p & ATV_RM_IME_BATCH_EDIT) {
        c->ime_counter = m->ime_counter;
        c->field_counter = m->field_counter;
    } else if (p & ATV_RM_SET_VOLUME_LEVEL) {
        e.type = ATV_EVT_VOLUME;
        e.volume.level = (uint16_t)(m->volume_level > 0xFFFF ? 0xFFFF : m->volume_level);
        e.volume.max = (uint16_t)(m->volume_max > 0xFFFF ? 0xFFFF : m->volume_max);
        e.volume.muted = m->volume_muted;
        emit(&e);
    } else if (p & ATV_RM_START) {
        if (!c->started) {
            c->started = true;
            c->backoff_s = 1;
            set_state(ATV_STATE_CONNECTED, ESP_OK);
        }
        e.type = ATV_EVT_POWER;
        e.is_on = m->started;
        emit(&e);
    } else if (p & ATV_RM_PING_REQUEST) {
        send_ping_response(c, m->ping_val1);
    } else if (p & ATV_RM_ERROR) {
        ESP_LOGE(TAG, "Received an error from the device: value=%s%s", m->error_value ? "true" : "false",
                 m->error_has_message ? " (with the offending message)" : "");
    } else if (p & ATV_RM_VOICE_BEGIN) {
        ESP_LOGD(TAG, "Ignoring remote_voice_begin: no client request available");
    } else if (p & ATV_RM_IME_SHOW_REQUEST) {
        e.type = ATV_EVT_IME_SHOW;
        if (m->has_show_text_field) {
            fill_ime(&e, &m->show_text_field);
        }
        emit(&e);
    } else {
        ESP_LOGD(TAG, "Unhandled RemoteMessage (fields 0x%x)", (unsigned)p);
    }
}

/* ------------------------------------------------------- pairing session */

static void log_rx_polo(const atv_polo_msg_t *m)
{
    ESP_LOGD(TAG, "rx OuterMessage protocol_version=%u status=%u%s%s%s%s%s%s", (unsigned)m->protocol_version,
             (unsigned)m->status, m->has_pairing_request_ack ? " pairing_request_ack" : "",
             m->has_options ? " options" : "", m->has_configuration_ack ? " configuration_ack" : "",
             m->has_secret_ack ? " secret_ack" : "", m->has_pairing_request ? " pairing_request" : "",
             m->has_configuration ? " configuration" : "");
}

static void pairing_start(ctx_t *c)
{
    refresh_host(c);
    set_state(ATV_STATE_PAIRING, ESP_OK);
    if (!c->host[0]) {
        emit_pair_result(ESP_ERR_NOT_FOUND);
        set_state(ATV_STATE_IDLE, ESP_ERR_NOT_FOUND);
        return;
    }
    esp_err_t err = session_open(c, ATV_PORT_PAIRING, SESS_PAIRING);
    if (err != ESP_OK) {
        emit_pair_result(err);
        set_state(ATV_STATE_IDLE, err);
        return;
    }
    c->pair_step = PAIR_WAIT_ACK;
    c->pair_deadline_us = now_us() + PAIRING_TIMEOUT_US;
    pb_writer_t w;
    TX_BEGIN(c, w);
    bool ok = atv_polo_pairing_request(&w, s.client_name);
    ESP_LOGD(TAG, "tx pairing_request service_name='%s' client_name='%s'", POLO_SERVICE_NAME, s.client_name);
    tx_flush(c, &w, ok);
}

static void pairing_on_message(ctx_t *c, const uint8_t *buf, size_t len)
{
    atv_polo_msg_t *m = &c->pmsg;
    if (!atv_parse_polo(buf, len, m)) {
        ESP_LOGW(TAG, "pairing: couldn't parse OuterMessage (%u bytes)", (unsigned)len);
        pairing_fail(c, ESP_ERR_INVALID_RESPONSE);
        return;
    }
    log_rx_polo(m);
    if (!m->has_status || m->status != POLO_STATUS_OK) {
        ESP_LOGW(TAG, "pairing: TV answered status %u", (unsigned)m->status);
        pairing_fail(c, ESP_ERR_INVALID_RESPONSE);
        return;
    }

    pb_writer_t w;
    TX_BEGIN(c, w);
    /* Same if/elif order as pairing.py _handle_message. */
    if (m->has_pairing_request_ack) {
        if (m->server_name[0]) {
            ESP_LOGI(TAG, "pairing with \"%s\"", m->server_name);
        }
        bool ok = atv_polo_options(&w);
        ESP_LOGD(TAG, "tx options input_encodings=[HEXADECIMAL/6] preferred_role=INPUT");
        if (tx_flush(c, &w, ok) == ESP_OK && c->pair_step == PAIR_WAIT_ACK) {
            c->pair_step = PAIR_WAIT_OPTIONS;
        }
    } else if (m->has_options) {
        bool ok = atv_polo_configuration(&w);
        ESP_LOGD(TAG, "tx configuration encoding=HEXADECIMAL/6 client_role=INPUT");
        if (tx_flush(c, &w, ok) == ESP_OK && c->pair_step <= PAIR_WAIT_OPTIONS) {
            c->pair_step = PAIR_WAIT_CONFIG_ACK;
        }
    } else if (m->has_configuration_ack) {
        if (c->pair_step == PAIR_WAIT_CONFIG_ACK) {
            c->pair_step = PAIR_WAIT_CODE;
            c->pair_deadline_us = 0; /* the user may take their time */
            set_state(ATV_STATE_PAIRING_CODE, ESP_OK);
        }
    } else if (m->has_secret_ack) {
        if (c->pair_step == PAIR_WAIT_SECRET_ACK) {
            uint8_t key[32];
            if (peer_key_hash(c, key) && save_tv_key(key) == ESP_OK) {
                ESP_LOGI(TAG, "the TV's key is pinned");
            } else {
                forget_tv_key(); /* the first control session pins it */
            }
            session_close(c);
            emit_pair_result(ESP_OK);
            /* atv_pair_finish: "on success connects the control session". */
            c->want_connected = true;
            c->backoff_s = 1;
            c->connect_pending = true;
            c->retry_at_us = now_us() + POST_PAIR_DELAY_US;
        }
    } else {
        ESP_LOGW(TAG, "pairing: unhandled message");
        pairing_fail(c, ESP_ERR_INVALID_RESPONSE);
    }
}

static void pairing_finish(ctx_t *c, const char *code)
{
    if (c->kind != SESS_PAIRING || c->pair_step != PAIR_WAIT_CODE) {
        ESP_LOGW(TAG, "atv_pair_finish ignored: no pairing code is being shown");
        return;
    }
    const mbedtls_x509_crt *server = mbedtls_ssl_get_peer_cert(&c->ssl);
    if (!server) {
        ESP_LOGE(TAG, "pairing: the TV's certificate is not available");
        pairing_fail(c, ESP_FAIL);
        return;
    }
    uint8_t secret[ATV_SECRET_LEN];
    esp_err_t err = atv_pair_secret_from_certs(&c->own_cert, server, code, secret);
    if (err == ESP_ERR_INVALID_ARG) {
        /* Not the code: a typo, or something between the remote and the TV
         * (whose real code must not reach the log). */
        ESP_LOGW(TAG, "pairing code does not match (nothing sent; enter it again)");
        emit_pair_result(ESP_ERR_INVALID_ARG);
        return;
    }
    if (err != ESP_OK) {
        pairing_fail(c, err);
        return;
    }
    c->pair_step = PAIR_WAIT_SECRET_ACK;
    c->pair_deadline_us = now_us() + PAIRING_TIMEOUT_US;
    pb_writer_t w;
    TX_BEGIN(c, w);
    bool ok = atv_polo_secret(&w, secret, sizeof(secret));
    ESP_LOGD(TAG, "tx secret (%d bytes)", ATV_SECRET_LEN);
    tx_flush(c, &w, ok);
}

/* --------------------------------------------------------- receive path */

static void dispatch(ctx_t *c, const uint8_t *msg, size_t len)
{
    if (c->kind == SESS_CONTROL) {
        control_on_message(c, msg, len);
    } else if (c->kind == SESS_PAIRING) {
        pairing_on_message(c, msg, len);
    }
}

/* Splits the receive buffer into varint-length-prefixed messages (base.py
 * data_received): partial prefixes and bodies wait for more data. */
static void process_rx(ctx_t *c)
{
    uint32_t gen = c->gen;
    size_t off = 0;
    while (off < c->rx_len) {
        if (c->rx_skip) {
            size_t n = c->rx_len - off;
            if (n > c->rx_skip) {
                n = c->rx_skip;
            }
            off += n;
            c->rx_skip -= n;
            continue;
        }
        size_t hdr, mlen;
        pb_frame_result_t r = pb_frame_peek(c->rx + off, c->rx_len - off, &hdr, &mlen);
        if (r == PB_FRAME_CORRUPT) {
            /* base.py: the stream cannot be resynchronized: close. */
            ESP_LOGW(TAG, "corrupt message length, closing");
            session_broken(c, ESP_ERR_INVALID_RESPONSE, 0);
            return;
        }
        if (r == PB_FRAME_NEED_MORE) {
            if (hdr && hdr + mlen > RX_BUF_LEN) {
                ESP_LOGW(TAG, "skipping a %u-byte message (receive buffer is %d)", (unsigned)mlen, RX_BUF_LEN);
                c->rx_skip = hdr + mlen - (c->rx_len - off);
                off = c->rx_len;
            }
            break;
        }
        const uint8_t *msg = c->rx + off + hdr;
        off += hdr + mlen;
        dispatch(c, msg, mlen);
        if (c->kind == SESS_NONE || c->gen != gen) {
            return; /* session closed (buffer already reset) */
        }
    }
    if (off) {
        memmove(c->rx, c->rx + off, c->rx_len - off);
        c->rx_len -= off;
    }
}

static void session_read(ctx_t *c)
{
    uint32_t gen = c->gen;
    for (;;) {
        if (c->rx_len >= RX_BUF_LEN) {
            /* Cannot happen: process_rx skips messages that do not fit. */
            ESP_LOGE(TAG, "receive buffer full");
            session_broken(c, ESP_ERR_INVALID_RESPONSE, 0);
            return;
        }
        int ret = mbedtls_ssl_read(&c->ssl, c->rx + c->rx_len, RX_BUF_LEN - c->rx_len);
        if (ret > 0) {
            c->rx_len += (size_t)ret;
            process_rx(c);
            if (c->kind == SESS_NONE || c->gen != gen) {
                return;
            }
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return;
        }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            continue;
        }
#endif
        bool eof = (ret == 0 || ret == MBEDTLS_ERR_SSL_CONN_EOF || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY);
        if (eof) {
            ESP_LOGW(TAG, "%s session closed by the TV", c->kind == SESS_PAIRING ? "pairing" : "control");
        } else {
            char eb[112];
            ESP_LOGW(TAG, "%s session read failed: %s", c->kind == SESS_PAIRING ? "pairing" : "control",
                     tls_err_str(ret, eb, sizeof(eb)));
        }
        session_broken(c, ESP_FAIL, eof ? 0 : ret);
        return;
    }
}

/* ------------------------------------------------------------- commands */

static bool control_ready(const ctx_t *c)
{
    return c->kind == SESS_CONTROL && c->started;
}

static void handle_cmd(ctx_t *c, cmd_t *cmd)
{
    pb_writer_t w;
    switch (cmd->type) {
    case CMD_CONNECT:
        if (c->kind == SESS_CONTROL) {
            break; /* already connecting / connected */
        }
        session_close(c); /* abandons a pairing session, if any */
        c->want_connected = true;
        c->backoff_s = 1;
        c->connect_pending = true;
        c->retry_at_us = now_us();
        break;

    case CMD_DISCONNECT:
        c->want_connected = false;
        c->connect_pending = false;
        session_close(c);
        set_state(ATV_STATE_IDLE, ESP_OK);
        break;

    case CMD_FORGET_KEY:
        /* Here, in order with connects: a connection still finishing to
         * the previous TV can't pin its key after this. */
        forget_tv_key();
        break;

    case CMD_PAIR_START:
        /* Pairing and control sessions never run at the same time. */
        c->want_connected = false;
        c->connect_pending = false;
        session_close(c);
        pairing_start(c);
        break;

    case CMD_PAIR_FINISH:
        pairing_finish(c, cmd->code);
        break;

    case CMD_PAIR_CANCEL:
        if (c->kind == SESS_PAIRING) {
            ESP_LOGI(TAG, "pairing cancelled");
            session_close(c);
            set_state(ATV_STATE_IDLE, ESP_OK);
        }
        break;

    case CMD_KEY:
        if (!control_ready(c)) {
            ESP_LOGW(TAG, "key %d dropped: not connected", (int)cmd->key);
            break;
        }
        c->last_activity_us = now_us();
        TX_BEGIN(c, w);
        {
            bool ok = atv_msg_key_inject(&w, cmd->key, cmd->dir);
            ESP_LOGD(TAG, "tx remote_key_inject key_code=%d direction=%s", (int)cmd->key, dir_name(cmd->dir));
            tx_flush(c, &w, ok);
        }
        break;

    case CMD_APP_LINK:
        if (!control_ready(c)) {
            ESP_LOGW(TAG, "app link dropped: not connected");
            break;
        }
        c->last_activity_us = now_us();
        TX_BEGIN(c, w);
        {
            bool ok = atv_msg_app_link(&w, cmd->str);
            ESP_LOGD(TAG, "tx remote_app_link_launch_request app_link='%s'", cmd->str);
            tx_flush(c, &w, ok);
        }
        break;

    case CMD_TEXT:
        if (!control_ready(c)) {
            ESP_LOGW(TAG, "text dropped: not connected");
            break;
        }
        c->last_activity_us = now_us();
        TX_BEGIN(c, w);
        {
            bool ok = atv_msg_ime_batch_edit(&w, c->ime_counter, c->field_counter, cmd->str);
            ESP_LOGD(TAG, "tx remote_ime_batch_edit ime_counter=%d field_counter=%d insert=1 value='%s'",
                     (int)c->ime_counter, (int)c->field_counter, cmd->str);
            tx_flush(c, &w, ok);
        }
        break;
    }
    free(cmd->str);
    cmd->str = NULL;
}

static void process_queue(ctx_t *c)
{
    cmd_t cmd;
    while (xQueueReceive(s.queue, &cmd, 0) == pdTRUE) {
        handle_cmd(c, &cmd);
    }
}

/* ------------------------------------------------------------------ task */

static void on_timeout(ctx_t *c)
{
    if (c->kind == SESS_CONTROL) {
        ESP_LOGW(TAG, "no traffic for %d s: closing the idle connection", (int)(IDLE_TIMEOUT_US / 1000000));
        control_lost(c, ESP_ERR_TIMEOUT, false);
    } else if (c->kind == SESS_PAIRING) {
        ESP_LOGW(TAG, "no response from the TV within %d s while pairing", (int)(PAIRING_TIMEOUT_US / 1000000));
        pairing_fail(c, ESP_ERR_TIMEOUT);
    }
}

static void session_poll(ctx_t *c)
{
    int64_t now = now_us();
    int64_t deadline = INT64_MAX;
    if (c->kind == SESS_CONTROL) {
        deadline = c->last_activity_us + IDLE_TIMEOUT_US;
    } else if (c->pair_deadline_us) {
        deadline = c->pair_deadline_us;
    }
    if (now >= deadline) {
        on_timeout(c);
        return;
    }

    bool readable = mbedtls_ssl_get_bytes_avail(&c->ssl) > 0 || mbedtls_ssl_check_pending(&c->ssl);
    if (!readable) {
        int64_t wait_us = deadline - now;
        if (wait_us > MAX_SELECT_WAIT_US) {
            wait_us = MAX_SELECT_WAIT_US;
        }
        if (s.wake_rx < 0 && wait_us > POLL_FALLBACK_MS * 1000LL) {
            wait_us = POLL_FALLBACK_MS * 1000LL;
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(c->net.fd, &rfds);
        int maxfd = c->net.fd;
        if (s.wake_rx >= 0) {
            FD_SET(s.wake_rx, &rfds);
            if (s.wake_rx > maxfd) {
                maxfd = s.wake_rx;
            }
        }
        struct timeval tv = {.tv_sec = (time_t)(wait_us / 1000000), .tv_usec = (suseconds_t)(wait_us % 1000000)};
        int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            ESP_LOGD(TAG, "select: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(10));
        } else if (r > 0 && FD_ISSET(c->net.fd, &rfds)) {
            readable = true;
        }
    }

    uint32_t gen = c->gen;
    wake_drain();
    process_queue(c);
    if (c->kind == SESS_NONE || c->gen != gen) {
        return;
    }
    if (readable) {
        session_read(c);
    }
}

static void atv_task(void *arg)
{
    ctx_t *c = (ctx_t *)arg;
    for (;;) {
        if (c->kind != SESS_NONE) {
            session_poll(c);
            continue;
        }
        /* Handle whatever is queued before (re)connecting: a stale wake
         * datagram would otherwise interrupt the connect attempt. */
        wake_drain();
        process_queue(c);
        if (c->kind != SESS_NONE) {
            continue;
        }
        TickType_t wait = portMAX_DELAY;
        if (c->want_connected && (c->connect_pending || s.state == ATV_STATE_DISCONNECTED)) {
            int64_t left = c->retry_at_us - now_us();
            if (left <= 0) {
                c->connect_pending = false;
                control_connect(c);
                continue;
            }
            wait = pdMS_TO_TICKS((uint32_t)((left + 999) / 1000));
            if (wait == 0) {
                wait = 1;
            }
        }
        cmd_t cmd;
        if (xQueueReceive(s.queue, &cmd, wait) == pdTRUE) {
            wake_drain();
            handle_cmd(c, &cmd);
            /* Act on everything queued (e.g. connect immediately followed by
             * disconnect) before starting a blocking connect. */
            process_queue(c);
        }
    }
}

/* ------------------------------------------------------------ public API */

static esp_err_t post(cmd_t *cmd)
{
    TickType_t wait = (xTaskGetCurrentTaskHandle() == s.task) ? 0 : pdMS_TO_TICKS(50);
    if (xQueueSend(s.queue, cmd, wait) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full");
        free(cmd->str);
        return ESP_ERR_TIMEOUT;
    }
    wake_task();
    return ESP_OK;
}

esp_err_t atv_init(const atv_config_t *cfg)
{
    if (!cfg || !cfg->nvs_namespace || !cfg->nvs_namespace[0] || strlen(cfg->nvs_namespace) > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    strlcpy(s.nvs_ns, cfg->nvs_namespace, sizeof(s.nvs_ns));
    strlcpy(s.client_name, (cfg->client_name && cfg->client_name[0]) ? cfg->client_name : "ESP32 Remote",
            sizeof(s.client_name));
    s.enable_ime = cfg->enable_ime;
    s.on_event = cfg->on_event;
    s.user_ctx = cfg->user_ctx;
    s.state = ATV_STATE_IDLE;

    esp_err_t err = atv_id_init();
    if (err != ESP_OK) {
        return err;
    }
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
    }
    if (!s.queue) {
        s.queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(cmd_t));
    }
    if (!s.ctx) {
        s.ctx = calloc_prefer_psram(1, sizeof(ctx_t));
    }
    if (!s.lock || !s.queue || !s.ctx) {
        return ESP_ERR_NO_MEM;
    }
    s.ctx->backoff_s = 1;

    nvs_handle_t h;
    if (nvs_open(s.nvs_ns, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s.host);
        s.has_host = nvs_get_str(h, NVS_KEY_HOST, s.host, &len) == ESP_OK && s.host[0];
        nvs_close(h);
    }

    wake_init();

    BaseType_t ok = xTaskCreatePinnedToCore(atv_task, "atv_remote", CONFIG_ATV_REMOTE_TASK_STACK_SIZE, s.ctx,
                                            CONFIG_ATV_REMOTE_TASK_PRIORITY, &s.task,
                                            CONFIG_ATV_REMOTE_TASK_CORE < 0 ? tskNO_AFFINITY
                                                                            : CONFIG_ATV_REMOTE_TASK_CORE);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s.inited = true;
    ESP_LOGI(TAG, "started (client \"%s\", NVS \"%s\", host %s, identity %s, IME %s)", s.client_name, s.nvs_ns,
             s.has_host ? s.host : "(none)", atv_id_exists(s.nvs_ns) ? "present" : "missing",
             s.enable_ime ? "on" : "off");
    return ESP_OK;
}

bool atv_has_identity(void)
{
    return s.inited && atv_id_exists(s.nvs_ns);
}

esp_err_t atv_generate_identity(void)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    return atv_id_generate(s.nvs_ns, s.client_name);
}

esp_err_t atv_set_host(const char *host)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!host || !host[0] || strlen(host) >= HOST_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(s.nvs_ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_HOST, host);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    strlcpy(s.host, host, sizeof(s.host));
    s.has_host = true;
    xSemaphoreGive(s.lock);
    ESP_LOGI(TAG, "TV host set to %s", host);
    return ESP_OK;
}

void atv_forget_tv_key(void)
{
    if (s.inited) {
        cmd_t cmd = {.type = CMD_FORGET_KEY};
        post(&cmd);
    }
}

bool atv_tv_key_pinned(void)
{
    nvs_handle_t h;
    size_t len = 0;
    bool pinned = false;
    if (s.inited && nvs_open(s.nvs_ns, NVS_READONLY, &h) == ESP_OK) {
        pinned = nvs_get_blob(h, NVS_KEY_TV_KEY, NULL, &len) == ESP_OK && len > 0;
        nvs_close(h);
    }
    return pinned;
}

bool atv_get_host(char *buf, size_t len)
{
    if (!s.inited || !buf || len == 0) {
        return false;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool ok = s.has_host;
    if (ok) {
        strlcpy(buf, s.host, len);
    }
    xSemaphoreGive(s.lock);
    return ok;
}

static esp_err_t check_can_connect(void)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atv_id_exists(s.nvs_ns)) {
        ESP_LOGW(TAG, "no identity: call atv_generate_identity() first");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool has_host = s.has_host;
    xSemaphoreGive(s.lock);
    if (!has_host) {
        ESP_LOGW(TAG, "no TV host: call atv_set_host() first");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t atv_connect(void)
{
    esp_err_t err = check_can_connect();
    if (err != ESP_OK) {
        return err;
    }
    cmd_t cmd = {.type = CMD_CONNECT};
    return post(&cmd);
}

esp_err_t atv_disconnect(void)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    cmd_t cmd = {.type = CMD_DISCONNECT};
    return post(&cmd);
}

esp_err_t atv_pair_start(void)
{
    esp_err_t err = check_can_connect();
    if (err != ESP_OK) {
        return err;
    }
    cmd_t cmd = {.type = CMD_PAIR_START};
    return post(&cmd);
}

esp_err_t atv_pair_finish(const char *code)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atv_code_valid(code)) {
        return ESP_ERR_INVALID_ARG; /* pairing.py: exactly 6 hex characters */
    }
    if (s.state != ATV_STATE_PAIRING_CODE) {
        return ESP_ERR_INVALID_STATE;
    }
    cmd_t cmd = {.type = CMD_PAIR_FINISH};
    strlcpy(cmd.code, code, sizeof(cmd.code));
    return post(&cmd);
}

esp_err_t atv_pair_cancel(void)
{
    if (!s.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    cmd_t cmd = {.type = CMD_PAIR_CANCEL};
    return post(&cmd);
}

esp_err_t atv_send_key(atv_keycode_t key, atv_direction_t dir)
{
    if (!s.inited || s.state != ATV_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    cmd_t cmd = {.type = CMD_KEY, .key = (int32_t)key, .dir = (int32_t)dir};
    return post(&cmd);
}

esp_err_t atv_launch_app_link(const char *link)
{
    if (!link || !link[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s.inited || s.state != ATV_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    char *buf = malloc(MAX_LINK_LEN + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    if (atv_app_link_build(link, buf, MAX_LINK_LEN + 1) == 0) {
        free(buf);
        return ESP_ERR_INVALID_SIZE;
    }
    cmd_t cmd = {.type = CMD_APP_LINK, .str = buf};
    return post(&cmd);
}

esp_err_t atv_send_text(const char *text)
{
    if (!text || !text[0]) {
        return ESP_ERR_INVALID_ARG; /* remote.py: "Text cannot be empty" */
    }
    if (strlen(text) > MAX_TEXT_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s.inited || s.state != ATV_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    char *copy = strdup(text);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    cmd_t cmd = {.type = CMD_TEXT, .str = copy};
    return post(&cmd);
}
