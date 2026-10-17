/*
 * Developer tools on the update server, signed with the same token
 * (ota_authorized()):
 *   GET  /screenshot                  the shown frame as a 24-bit BMP
 *   POST /tap?x=&y=[&ms=]             touch at (x, y) for ms (default 80)
 *   POST /swipe?x1=&y1=&x2=&y2=[&ms=] drag from one point to the other
 *
 * Touches go through an extra LVGL pointer device, in screen coordinates
 * (the picture's, not the panel's), so they work the same whether or not
 * the display is turned. Off in release builds (CONFIG_HM_DEV_TOOLS).
 */
#include "devtools.h"

#include <stdlib.h>
#include "board.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "ota.h"

#if CONFIG_HM_DEV_TOOLS

static const char *TAG = "devtools";

#define W           BOARD_LCD_H_RES
#define H           BOARD_LCD_V_RES
#define ROWS_PER_CHUNK 8
#define STEP_MS     20
#define MAX_MS      10000 /* the server has one task: no gesture holds it longer */

static volatile int16_t s_x, s_y;
static volatile bool s_pressed;
static SemaphoreHandle_t s_touch_lock; /* one simulated gesture at a time */

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point.x = s_x;
    data->point.y = s_y;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void devtools_init(void)
{
    s_touch_lock = xSemaphoreCreateMutex();
    if (lvgl_port_lock(0)) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, read_cb);
        lvgl_port_unlock();
    }
}

static int query_int(httpd_req_t *req, const char *key, int fallback)
{
    char query[96], value[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK) {
        return atoi(value);
    }
    return fallback;
}

static void put_le(uint8_t *p, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static esp_err_t screenshot_get(httpd_req_t *req)
{
    if (!ota_authorized(req, NULL)) {
        return ESP_OK;
    }
    uint16_t *frame = heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    uint8_t *rows = heap_caps_malloc(W * 3 * ROWS_PER_CHUNK, MALLOC_CAP_SPIRAM);
    if (!frame || !rows || !display_capture(frame)) {
        free(frame);
        free(rows);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "no frame\n");
    }

    /* 24-bit BMP, rows bottom-up; 1024 * 3 bytes per row needs no padding. */
    uint8_t hdr[54] = {'B', 'M'};
    put_le(hdr + 2, sizeof(hdr) + W * H * 3, 4);
    put_le(hdr + 10, sizeof(hdr), 4);
    put_le(hdr + 14, 40, 4);
    put_le(hdr + 18, W, 4);
    put_le(hdr + 22, H, 4);
    put_le(hdr + 26, 1, 2);
    put_le(hdr + 28, 24, 2);
    put_le(hdr + 34, W * H * 3, 4);
    httpd_resp_set_type(req, "image/bmp");
    esp_err_t err = httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr));
    for (int y = H - 1; err == ESP_OK && y >= 0;) {
        int n = 0;
        for (; n < ROWS_PER_CHUNK && y >= 0; n++, y--) {
            const uint16_t *src = frame + y * W;
            uint8_t *dst = rows + n * W * 3;
            for (int x = 0; x < W; x++) {
                uint16_t c = src[x];
                dst[3 * x + 0] = (uint8_t)((c & 0x1F) << 3 | (c & 0x1F) >> 2);           /* blue */
                dst[3 * x + 1] = (uint8_t)((c >> 5 & 0x3F) << 2 | (c >> 5 & 0x3F) >> 4); /* green */
                dst[3 * x + 2] = (uint8_t)((c >> 11) << 3 | (c >> 11) >> 2);             /* red */
            }
        }
        err = httpd_resp_send_chunk(req, (const char *)rows, n * W * 3);
    }
    free(frame);
    free(rows);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    return err;
}

static void hold(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms < STEP_MS ? STEP_MS : ms));
}

static esp_err_t tap_post(httpd_req_t *req)
{
    if (!ota_authorized(req, NULL)) {
        return ESP_OK;
    }
    int x = query_int(req, "x", -1), y = query_int(req, "y", -1), ms = query_int(req, "ms", 80);
    if (x < 0 || x >= W || y < 0 || y >= H || ms < 0 || ms > MAX_MS) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "x and y must be on the screen, ms at most 10000\n");
    }
    xSemaphoreTake(s_touch_lock, portMAX_DELAY);
    s_x = (int16_t)x;
    s_y = (int16_t)y;
    s_pressed = true;
    hold(ms);
    s_pressed = false;
    hold(100); /* let LVGL read the release */
    xSemaphoreGive(s_touch_lock);
    ESP_LOGI(TAG, "tap %d,%d (%d ms)", x, y, ms);
    return httpd_resp_sendstr(req, "ok\n");
}

static esp_err_t swipe_post(httpd_req_t *req)
{
    if (!ota_authorized(req, NULL)) {
        return ESP_OK;
    }
    int x1 = query_int(req, "x1", -1), y1 = query_int(req, "y1", -1);
    int x2 = query_int(req, "x2", -1), y2 = query_int(req, "y2", -1);
    int ms = query_int(req, "ms", 300);
    if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0 || x1 >= W || x2 >= W || y1 >= H || y2 >= H || ms < 0 ||
        ms > MAX_MS) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "points must be on the screen, ms at most 10000\n");
    }
    int steps = ms / STEP_MS < 2 ? 2 : ms / STEP_MS;
    xSemaphoreTake(s_touch_lock, portMAX_DELAY);
    for (int i = 0; i <= steps; i++) {
        s_x = (int16_t)(x1 + (x2 - x1) * i / steps);
        s_y = (int16_t)(y1 + (y2 - y1) * i / steps);
        s_pressed = true;
        hold(STEP_MS);
    }
    s_pressed = false;
    hold(100);
    xSemaphoreGive(s_touch_lock);
    ESP_LOGI(TAG, "swipe %d,%d -> %d,%d (%d ms)", x1, y1, x2, y2, ms);
    return httpd_resp_sendstr(req, "ok\n");
}

void devtools_register(httpd_handle_t server)
{
    static const httpd_uri_t shot = {.uri = "/screenshot", .method = HTTP_GET, .handler = screenshot_get};
    static const httpd_uri_t tap = {.uri = "/tap", .method = HTTP_POST, .handler = tap_post};
    static const httpd_uri_t swipe = {.uri = "/swipe", .method = HTTP_POST, .handler = swipe_post};
    httpd_register_uri_handler(server, &shot);
    httpd_register_uri_handler(server, &tap);
    httpd_register_uri_handler(server, &swipe);
    ESP_LOGI(TAG, "screenshot and touch tools on");
}

#else

void devtools_init(void)
{
}

void devtools_register(httpd_handle_t server)
{
}

#endif
