#pragma once

/* Shared between weather.c (data and refreshing) and weather_ui.c. */

#include "hub.h"
#include "lvgl.h"
#include "weather_data.h"

#define WEATHER_ID "weather"

typedef struct {
    wx_data_t data;   /* the last forecast; data.valid is false if none */
    int64_t fetched;  /* when it was fetched (unix seconds), 0 if unknown */
    bool has_place;
    wx_place_t place;
    bool imperial;    /* show Fahrenheit and mph (data is always metric) */
    bool fetching;
    bool failed;      /* the last refresh failed */
} weather_state_t;

/* weather.c */
void weather_snapshot(weather_state_t *out); /* a consistent copy; any task */
void weather_retry(void);                    /* refresh now, after a failure */
void weather_search(const char *text);       /* results arrive in weather_ui_search_results() */
void weather_set_place(const wx_place_t *place);
void weather_set_imperial(bool imperial);

/* weather_ui.c (LVGL task) */
lv_obj_t *weather_ui_create_view(lv_obj_t *parent);
lv_obj_t *weather_ui_create_card(lv_obj_t *parent, hub_card_size_t size);
bool weather_ui_glance(hub_glance_t *out);
void weather_ui_update(void *arg);
void weather_ui_search_results(const wx_place_t *places, int count, bool failed);
