/*
 * What Waveshare's ESP32-S3 RGB boards share (waveshare_s3_rgb.h): the I2C
 * bus, the GT911 touch controller (its reset is on the IO expander), the RGB
 * panel and deep sleep. The expander, backlight and battery are in
 * waveshare_ch422g.c or waveshare_ch32v003.c, by board.
 *
 * Sources: Waveshare's schematics and ESP-IDF demos for each board, and
 * bench tests on the 7B (14 MHz PCLK, porch timings, 10-line bounce
 * buffer, the GT911 address strap). The panel runs without a framebuffer of
 * its own: the caller fills the bounce buffer (main/display.c turns the
 * picture that way).
 */
#include "board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_bus;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;

esp_err_t ws_i2c_bus(i2c_master_bus_handle_t *out)
{
    if (!s_bus) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = WS_PIN_I2C_SDA,
            .scl_io_num = WS_PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");
    }
    *out = s_bus;
    return ESP_OK;
}

esp_err_t board_touch_init(bool turned, esp_lcd_touch_handle_t *out)
{
    /* GT911 latches its I2C address from INT while RST rises: INT low
     * selects 0x5D. RST is on the expander, so this is done by hand. */
    gpio_config_t int_out = {
        .pin_bit_mask = BIT64(WS_PIN_TOUCH_INT),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&int_out);
    gpio_set_level(WS_PIN_TOUCH_INT, 0);
    ws_exio_set(BIT(BOARD_EXIO_TOUCH_RST), false);
    vTaskDelay(pdMS_TO_TICKS(20));
    ws_exio_set(BIT(BOARD_EXIO_TOUCH_RST), true);
    vTaskDelay(pdMS_TO_TICKS(60));
    gpio_config_t int_in = {
        .pin_bit_mask = BIT64(WS_PIN_TOUCH_INT),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&int_in);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Mirroring maps x to x_max - x, so the maxima are the last pixel. */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES - 1,
        .y_max = BOARD_LCD_V_RES - 1,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .flags = {.mirror_x = turned, .mirror_y = turned},
    };
    /* If the strap didn't take, the GT911 answers on its backup address. */
    static const uint8_t addrs[] = {ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP};
    esp_err_t err = ESP_FAIL;
    for (size_t i = 0; i < sizeof(addrs) && err != ESP_OK; i++) {
        esp_lcd_panel_io_handle_t io;
        esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        io_cfg.dev_addr = addrs[i];
        io_cfg.scl_speed_hz = 400000;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &io), TAG, "touch io");
        err = esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &s_touch);
        if (err != ESP_OK) {
            esp_lcd_panel_io_del(io);
        } else {
            ESP_LOGI(TAG, "gt911 at 0x%02x", addrs[i]);
        }
    }
    ESP_RETURN_ON_ERROR(err, TAG, "gt911 not found");
    *out = s_touch;
    return ESP_OK;
}

esp_err_t board_lcd_init(uint32_t pclk_hz, const esp_lcd_rgb_panel_event_callbacks_t *cbs, void *cb_ctx,
                         esp_lcd_panel_handle_t *out)
{
    static const int data_pins[16] = WS_LCD_DATA_PINS;
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = pclk_hz,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * 10,
        .dma_burst_size = 64,
        .hsync_gpio_num = WS_PIN_LCD_HSYNC,
        .vsync_gpio_num = WS_PIN_LCD_VSYNC,
        .de_gpio_num = WS_PIN_LCD_DE,
        .pclk_gpio_num = WS_PIN_LCD_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .flags.no_fb = 1,
    };
    for (int i = 0; i < 16; i++) {
        cfg.data_gpio_nums[i] = data_pins[i];
    }
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(s_panel, cbs, cb_ctx), TAG, "panel callbacks");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    *out = s_panel;
    return ESP_OK;
}

void board_deep_sleep(void)
{
    board_backlight_set(0);
    ws_panel_power(false);

    /* The GT911 keeps scanning and drives INT when touched; its idle level
     * depends on its configuration, so wake on the other one. Wait for
     * 300 ms of steady level first: the finger that pressed "off" may
     * still be lifting. */
    int idle = gpio_get_level(WS_PIN_TOUCH_INT);
    for (int steady = 0, waited = 0; steady < 300 && waited < 3000; waited++) {
        vTaskDelay(1); /* 1 ms at CONFIG_FREERTOS_HZ=1000 */
        int now = gpio_get_level(WS_PIN_TOUCH_INT);
        steady = now == idle ? steady + 1 : 0;
        idle = now;
    }
    esp_sleep_enable_ext1_wakeup_io(BIT64(WS_PIN_TOUCH_INT), idle ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_ext0_wakeup(WS_PIN_BOOT, 0);
    ESP_LOGI(TAG, "deep sleep (touch INT idles %s)", idle ? "high" : "low");
    vTaskDelay(pdMS_TO_TICKS(300)); /* let the Wi-Fi log send the line */
    esp_deep_sleep_start();
}
