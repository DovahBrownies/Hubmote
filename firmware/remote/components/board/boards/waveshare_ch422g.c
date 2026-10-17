/*
 * Waveshare boards whose IO expander is a CH422G (the 4.3, 4.3B, 5, 5B and
 * 7): eight outputs, a backlight that is only on or off, no battery
 * reading. The board's header says which expander pin does what
 * (BOARD_EXIO_*); the rest is waveshare_s3_rgb.c.
 *
 * The CH422G has no register address: each command is an I2C address of its
 * own, followed by one data byte (Waveshare's CH422G.h, Espressif's
 * esp_io_expander_ch422g):
 *   0x24  system: 0x01 = IO0..IO7 are outputs (all or none)
 *   0x38  IO0..IO7 output levels
 * It answers on 0x20-0x27 and 0x30-0x3F, so nothing else can sit there.
 *
 * Sources: Waveshare's schematics and ESP-IDF demos for these boards
 * (waveshare_rgb_lcd_port.c: the same writes, 0x01 to 0x24, then the
 * outputs to 0x38). Untested: no Hubmote has run on a CH422G board yet.
 */
#include "board.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "board";

#define CH422G_SYSTEM  0x24
#define CH422G_OUT     0x38
#define CH422G_IO_OE   0x01

static i2c_master_dev_handle_t s_system;
static i2c_master_dev_handle_t s_out;
static uint8_t s_exp_out;
static uint8_t s_backlight_pct;
static int64_t s_backlight_changed_us;

static esp_err_t ch422g_write(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, 100);
}

static esp_err_t add_device(i2c_master_bus_handle_t bus, uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &cfg, out);
}

void ws_exio_set(uint8_t mask, bool high)
{
    uint8_t next = high ? (s_exp_out | mask) : (s_exp_out & ~mask);
    if (next == s_exp_out) {
        return;
    }
    s_exp_out = next;
    if (ch422g_write(s_out, s_exp_out) != ESP_OK) {
        ESP_LOGW(TAG, "expander output write failed");
    }
}

esp_err_t board_init(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(ws_i2c_bus(&bus), TAG, "i2c");
    ESP_RETURN_ON_ERROR(add_device(bus, CH422G_SYSTEM, &s_system), TAG, "expander dev");
    ESP_RETURN_ON_ERROR(add_device(bus, CH422G_OUT, &s_out), TAG, "expander dev");

    /* The board's start levels (its header), backlight off until the first
     * frame. The levels go out before and after the pins become outputs,
     * so USB_SEL never rises: a high would move the native USB port's
     * lines to the CAN transceiver. */
    s_exp_out = BOARD_EXIO_START & ~BIT(BOARD_EXIO_DISP);
    ESP_RETURN_ON_ERROR(ch422g_write(s_out, s_exp_out), TAG, "expander out");
    ESP_RETURN_ON_ERROR(ch422g_write(s_system, CH422G_IO_OE), TAG, "expander mode");
    ESP_RETURN_ON_ERROR(ch422g_write(s_out, s_exp_out), TAG, "expander out");
    s_backlight_pct = 0;

    ESP_LOGI(TAG, "%s: expander ready", BOARD_NAME);
    return ESP_OK;
}

/* The backlight driver's enable is the only control (BOARD_BACKLIGHT_DIMMABLE
 * is 0): any level above 0 is full brightness. */
void board_backlight_set(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    if ((pct > 0) != (s_backlight_pct > 0)) {
        ws_exio_set(BIT(BOARD_EXIO_DISP), pct > 0);
        ESP_LOGI(TAG, "backlight %s", pct ? "on" : "off");
        s_backlight_changed_us = esp_timer_get_time();
    }
    s_backlight_pct = pct;
}

int64_t board_backlight_changed_us(void)
{
    return s_backlight_changed_us;
}

/* The CH422G is a plain I2C chip and keeps what it is told. */
void board_backlight_refresh(void)
{
}

uint8_t board_backlight_get(void)
{
    return s_backlight_pct;
}

void ws_panel_power(bool on)
{
#ifdef BOARD_EXIO_LCD_VDD_EN
    ws_exio_set(BIT(BOARD_EXIO_LCD_VDD_EN), on);
#endif
}

/* The battery's voltage doesn't reach the ESP32 or the expander. */
uint16_t board_battery_mv(void)
{
    return 0;
}
