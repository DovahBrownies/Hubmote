#pragma once

/*
 * Wi-Fi station. The network's name and password live in NVS (namespace
 * "wifi"), set on the remote (setup or Settings > Wi-Fi). A build with
 * WIFI_SSID in secrets.env uses those only while NVS has none, and saves
 * them once they connect. New credentials are saved only after they
 * connect, so a wrong password never replaces a working network.
 *
 * Network names never go to the log (it can leave the device, netlog.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* reason: 0 when up, otherwise the wifi_err_reason_t of the disconnect. */
typedef void (*net_cb_t)(bool up, int reason, void *ctx);

typedef struct {
    char ssid[33];
    int rssi;    /* dBm */
    bool secure; /* needs a password */
} net_ap_t;
#define NET_SCAN_MAX 24 /* networks a scan reports at most */

/* Called on the event task with the networks found, strongest first, one
 * entry per name (count 0 if the scan failed). */
typedef void (*net_scan_cb_t)(const net_ap_t *aps, size_t count, void *ctx);

/* The remote's mDNS name: hubmote.local. */
#define NET_HOSTNAME "hubmote"

/* netif + default event loop. Call once, before anything that uses them. */
void net_init(void);
/* True when there is a network to join (saved, or from secrets.env). */
bool net_configured(void);
const char *net_ssid(void);
/* Starts the radio and joins the network, if there is one, retrying for
 * ever. cb runs on the event task when the link comes up and after every
 * failed or dropped attempt. */
void net_start(net_cb_t cb, void *ctx);
/* Joins another network now; it is saved once it connects. */
esp_err_t net_join(const char *ssid, const char *password);
/* Looks for networks (2 to 3 s). One scan at a time. */
esp_err_t net_scan(net_scan_cb_t cb, void *ctx);

/* Answers to NET_HOSTNAME.local (mDNS); call once the network is up (safe
 * to call again). The TV is found over mDNS too (atv_discover()). */
void net_mdns_start(void);
/* The mDNS name in use, without ".local": NET_HOSTNAME, or the name mDNS
 * picked when another device already answers to it ("hubmote-2"). "" before
 * net_mdns_start(). */
void net_hostname(char *out, size_t size);
