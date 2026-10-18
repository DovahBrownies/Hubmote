# The platform side of modules: registry, events, web requests, settings,
# files and time (docs/modules.md).
idf_component_register(SRCS "hub_module.c" "hub_http.c" "hub_store.c" "hub_time.c" "hub_tz.c" "hub_actions.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_client mbedtls esp_netif kv log)
