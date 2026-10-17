#pragma once

/* Log over Wi-Fi (CONFIG_HM_NETLOG): a TCP stream on CONFIG_HM_NETLOG_PORT
 * that scripts/hm-netlog.ps1 reads. */

/* Starts copying log lines into the backlog. Call early in app_main. */
void netlog_init(void);
/* Starts the server; call once the network is up (safe to call again). */
void netlog_serve(void);
