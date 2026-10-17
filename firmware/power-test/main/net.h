#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NET_OFF = 0,
    NET_ON_NO_PS,  /* radio always listening */
    NET_ON_MIN_PS, /* modem sleep, wakes every DTIM */
    NET_ON_MAX_PS, /* modem sleep, wakes every 3 beacons (listen interval) */
    NET_MODE_COUNT
} net_mode_t;

void net_init(void);
/* False when no SSID was set in menuconfig: every Wi-Fi test is disabled. */
bool net_available(void);
void net_set_mode(net_mode_t mode);
net_mode_t net_get_mode(void);
bool net_wait_ip(uint32_t timeout_ms);

/* ICMP echo to the gateway at a fixed interval. Stands in for the Android
 * TV Remote v2 keepalive (the TV pings its clients about every 5 s). */
void net_keepalive_start(uint32_t interval_ms);
void net_keepalive_stop(void);

bool net_connected(void);
int net_rssi(void);
/* esp_timer time at which the current IP was obtained, 0 if none yet. */
int64_t net_ip_time_us(void);
/* Radio start to IP, -1 while not connected yet. */
int net_connect_ms(void);
bool net_used_fast_connect(void);
