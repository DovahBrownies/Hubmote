/*
 * Waveshare boards whose IO expander is a CH32V003 running Waveshare's
 * firmware (the 7B, the 4.3C): register writes at I2C 0x24, with backlight
 * PWM and a battery ADC. The board's header says which expander pin does
 * what (BOARD_EXIO_*); the rest is waveshare_s3_rgb.c.
 *
 * Sources: Waveshare's schematics and ESP-IDF demos (the expander protocol),
 * ESPHome waveshare_io_ch32v003 (register map, PWM limits), and bench tests
 * on the 7B. Same expander code as the power-test firmware.
 */
#include "board.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

/* CH32V003 expander running Waveshare firmware, I2C 0x24. */
#define EXP_ADDR     0x24
#define EXP_REG_MODE 0x02 /* bit = 1: output */
#define EXP_REG_OUT  0x03
#define EXP_REG_PWM  0x05 /* backlight PWM, inverted: higher duty = dimmer */
#define EXP_REG_ADC  0x06 /* 10-bit battery ADC, little-endian, 3:1 divider */

/* ESPHome caps the inverted PWM at 247 ("safe level"); near 0 (full
 * brightness) users report flicker, so the bright end stops at 10. */
#define PWM_DIMMEST   247
#define PWM_BRIGHTEST 10

static i2c_master_dev_handle_t s_exp;
static uint8_t s_exp_out;
static uint8_t s_backlight_pct;
static int64_t s_backlight_changed_us;

/* The expander is a small MCU running Waveshare firmware, and it misses the
 * odd write that lands right after another transaction (seen on the bench:
 * a single backlight write after a touch sometimes never took effect). Keep
 * transactions EXP_GAP_US apart; board_backlight_refresh() covers the rest. */
#define EXP_GAP_US 5000
static int64_t s_exp_last_us;

static void exp_wait_gap(void)
{
    int64_t since = esp_timer_get_time() - s_exp_last_us;
    if (since < EXP_GAP_US) {
        vTaskDelay(pdMS_TO_TICKS((EXP_GAP_US - since) / 1000 + 1));
    }
}

static esp_err_t exp_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    exp_wait_gap();
    esp_err_t err = i2c_master_transmit(s_exp, buf, sizeof(buf), 100);
    s_exp_last_us = esp_timer_get_time();
    return err;
}

static uint8_t pwm_for(uint8_t pct)
{
    return PWM_DIMMEST - (uint8_t)((PWM_DIMMEST - PWM_BRIGHTEST) * pct / 100);
}

void ws_exio_set(uint8_t mask, bool high)
{
    uint8_t next = high ? (s_exp_out | mask) : (s_exp_out & ~mask);
    if (next == s_exp_out) {
        return;
    }
    s_exp_out = next;
    if (exp_write(EXP_REG_OUT, s_exp_out) != ESP_OK) {
        ESP_LOGW(TAG, "expander output write failed");
    }
}

esp_err_t board_init(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(ws_i2c_bus(&bus), TAG, "i2c");
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EXP_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_exp), TAG, "expander dev");

    /* Every pin an output, at the board's start levels (its header); the
     * backlight waits for the first frame. */
    s_exp_out = BOARD_EXIO_START & ~BIT(BOARD_EXIO_DISP);
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_MODE, 0xFF), TAG, "expander mode");
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_OUT, s_exp_out), TAG, "expander out");
    /* PWM is left at the expander's own default until the first
     * board_backlight_set(), like Waveshare's demos do. */
    s_backlight_pct = 0;

    ESP_LOGI(TAG, "%s: expander ready, battery %u mV", BOARD_NAME, board_battery_mv());
    return ESP_OK;
}

void board_backlight_set(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    if (pct == s_backlight_pct) {
        return;
    }
    if (pct == 0) {
        ws_exio_set(BIT(BOARD_EXIO_DISP), false);
        ESP_LOGI(TAG, "backlight off");
    } else {
        esp_err_t err = exp_write(EXP_REG_PWM, pwm_for(pct));
        ws_exio_set(BIT(BOARD_EXIO_DISP), true);
        ESP_LOGI(TAG, "backlight %u%% -> pwm %u (%s)", pct, pwm_for(pct), esp_err_to_name(err));
    }
    s_backlight_pct = pct;
    s_backlight_changed_us = esp_timer_get_time();
}

int64_t board_backlight_changed_us(void)
{
    return s_backlight_changed_us;
}

void board_backlight_refresh(void)
{
    /* Reading the PWM register back is unreliable on this expander, so
     * re-send the wanted state instead. */
    if (s_backlight_pct > 0) {
        exp_write(EXP_REG_PWM, pwm_for(s_backlight_pct));
    }
    exp_write(EXP_REG_OUT, s_exp_out);
}

uint8_t board_backlight_get(void)
{
    return s_backlight_pct;
}

/* Panel bias rails (AVDD/VGH/VGL/VCOM boost) via LCD_VDD_EN, where the
 * board has it on the expander. */
void ws_panel_power(bool on)
{
#ifdef BOARD_EXIO_LCD_VDD_EN
    ws_exio_set(BIT(BOARD_EXIO_LCD_VDD_EN), on);
#endif
}

uint16_t board_battery_mv(void)
{
    uint8_t reg = EXP_REG_ADC;
    uint8_t d[2] = {0};
    exp_wait_gap();
    esp_err_t err = i2c_master_transmit_receive(s_exp, &reg, 1, d, sizeof(d), 100);
    s_exp_last_us = esp_timer_get_time();
    if (err != ESP_OK) {
        return 0;
    }
    uint32_t raw = ((uint32_t)d[1] << 8) | d[0];
    return (uint16_t)(raw * 3 * 3300 / 1023);
}
