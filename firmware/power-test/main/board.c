/*
 * Waveshare ESP32-S3-Touch-LCD-7B board support, reduced to what the power
 * test needs.
 *
 * Sources: Waveshare schematic and waveshareteam/ESP32-S3-Touch-LCD-7B
 * ESP-IDF demos (pins, expander protocol), ESPHome waveshare_io_ch32v003
 * (register map, PWM limits), and bench tests on a second 7B (14 MHz
 * PCLK, porch timings, 10-line bounce buffer).
 */
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
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

#define PIN_I2C_SDA   GPIO_NUM_8
#define PIN_I2C_SCL   GPIO_NUM_9
#define PIN_TOUCH_INT GPIO_NUM_4

#define PIN_LCD_HSYNC 46
#define PIN_LCD_VSYNC 3
#define PIN_LCD_DE    5
#define PIN_LCD_PCLK  7
/* B3..B7, G2..G7, R3..R7 (RGB565; the panel's low bits are tied to GND). */
static const int LCD_DATA_PINS[16] = {
    14, 38, 18, 17, 10,
    39, 0, 45, 48, 47, 21,
    1, 2, 42, 41, 40,
};

/* CH32V003 expander running Waveshare firmware, I2C 0x24. */
#define EXP_ADDR     0x24
#define EXP_REG_MODE 0x02 /* bit = 1: output */
#define EXP_REG_OUT  0x03
#define EXP_REG_PWM  0x05 /* backlight PWM, inverted: higher duty = dimmer */
#define EXP_REG_ADC  0x06 /* 10-bit battery ADC, little-endian, 3:1 divider */

#define EXIO_BL_AUX     0 /* unrouted per the schematic; a faint glow was seen
                           * with only DISP low, so it is switched with DISP */
#define EXIO_TOUCH_RST  1
#define EXIO_DISP       2 /* backlight driver enable */
#define EXIO_LCD_RST    3
#define EXIO_SD_CS      4
#define EXIO_USB_SEL    5 /* 0 = native USB on GPIO19/20, 1 = CAN transceiver */
#define EXIO_LCD_VDD_EN 6 /* panel bias boost */

/* ESPHome caps the inverted PWM at 247 ("safe level"); near 0 (full
 * brightness) users report flicker, so the bright end stops at 10. */
#define PWM_DIMMEST   247
#define PWM_BRIGHTEST 10

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_exp;
static uint8_t s_exp_out;
static int s_backlight_pct = -1;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;

static esp_err_t exp_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_exp, buf, sizeof(buf), 100);
}

static void exio_set_mask(uint8_t mask, bool high)
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

static bool is_strapping_pin(int pin)
{
    return pin == 0 || pin == 3 || pin == 45 || pin == 46;
}

static void for_each_lcd_pin(void (*fn)(int pin))
{
    static const int ctrl[] = {PIN_LCD_HSYNC, PIN_LCD_VSYNC, PIN_LCD_DE, PIN_LCD_PCLK};
    for (size_t i = 0; i < sizeof(ctrl) / sizeof(ctrl[0]); i++) {
        fn(ctrl[i]);
    }
    for (size_t i = 0; i < 16; i++) {
        fn(LCD_DATA_PINS[i]);
    }
}

static void release_hold(int pin)
{
    gpio_hold_dis(pin);
}

void board_release_pin_holds(void)
{
    gpio_deep_sleep_hold_dis();
    for_each_lcd_pin(release_hold);
}

esp_err_t board_init_bus_and_expander(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EXP_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_exp), TAG, "expander dev");

    /* Everything high (Waveshare's default) except the backlight, which
     * waits for the first frame, and USB_SEL, low so the native USB-C port
     * works instead of routing GPIO19/20 to the CAN transceiver. */
    s_exp_out = 0xFF & ~(BIT(EXIO_DISP) | BIT(EXIO_BL_AUX) | BIT(EXIO_USB_SEL));
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_MODE, 0xFF), TAG, "expander mode");
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_OUT, s_exp_out), TAG, "expander out");
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_PWM, PWM_DIMMEST), TAG, "expander pwm");
    s_backlight_pct = 0;

    ESP_LOGI(TAG, "expander ready, battery %u mV", board_battery_mv());
    return ESP_OK;
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *out)
{
    /* GT911 latches its I2C address from INT while RST rises: INT low
     * selects 0x5D. RST is on the expander, so this is done by hand. */
    gpio_config_t int_out = {
        .pin_bit_mask = BIT64(PIN_TOUCH_INT),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&int_out);
    gpio_set_level(PIN_TOUCH_INT, 0);
    exio_set_mask(BIT(EXIO_TOUCH_RST), false);
    vTaskDelay(pdMS_TO_TICKS(20));
    exio_set_mask(BIT(EXIO_TOUCH_RST), true);
    vTaskDelay(pdMS_TO_TICKS(60));
    gpio_config_t int_in = {
        .pin_bit_mask = BIT64(PIN_TOUCH_INT),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&int_in);
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
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

esp_err_t board_lcd_init(uint32_t pclk_hz, esp_lcd_panel_handle_t *out)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = pclk_hz,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = 30,
            .hsync_back_porch = 145,
            .hsync_front_porch = 170,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 23,
            .vsync_front_porch = 12,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * 10,
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .flags.fb_in_psram = 1,
    };
    for (int i = 0; i < 16; i++) {
        cfg.data_gpio_nums[i] = LCD_DATA_PINS[i];
    }
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    *out = s_panel;
    return ESP_OK;
}

esp_err_t board_lcd_set_pclk(uint32_t pclk_hz)
{
    return s_panel ? esp_lcd_rgb_panel_set_pclk(s_panel, pclk_hz) : ESP_ERR_INVALID_STATE;
}

void board_lcd_delete(void)
{
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
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
        exio_set_mask(BIT(EXIO_DISP) | BIT(EXIO_BL_AUX), false);
    } else {
        uint8_t duty = PWM_DIMMEST - (uint8_t)((PWM_DIMMEST - PWM_BRIGHTEST) * pct / 100);
        if (exp_write(EXP_REG_PWM, duty) != ESP_OK) {
            ESP_LOGW(TAG, "backlight pwm write failed");
        }
        exio_set_mask(BIT(EXIO_DISP) | BIT(EXIO_BL_AUX), true);
    }
    s_backlight_pct = pct;
}

void board_panel_power(bool on)
{
    exio_set_mask(BIT(EXIO_LCD_VDD_EN), on);
}

void board_touch_sleep(void)
{
    if (s_touch && esp_lcd_touch_enter_sleep(s_touch) != ESP_OK) {
        ESP_LOGW(TAG, "gt911 sleep command failed");
    }
}

uint16_t board_battery_mv(void)
{
    uint8_t reg = EXP_REG_ADC;
    uint8_t d[2] = {0};
    if (i2c_master_transmit_receive(s_exp, &reg, 1, d, sizeof(d), 100) != ESP_OK) {
        return 0;
    }
    uint32_t raw = ((uint32_t)d[1] << 8) | d[0];
    return (uint16_t)(raw * 3 * 3300 / 1023);
}

static void park_pin(int pin)
{
    /* Strapping pins keep whatever the board's resistors give them, so the
     * next boot samples the same strap values. */
    if (is_strapping_pin(pin)) {
        return;
    }
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    gpio_hold_en(pin);
}

void board_lcd_pins_park(void)
{
    for_each_lcd_pin(park_pin);
    gpio_deep_sleep_hold_en();
}
