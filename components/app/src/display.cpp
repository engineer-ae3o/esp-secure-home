#include "esp_err.h"
#include "utils.hpp"
#include "config.hpp"
#include "display.hpp"

#include "i2cdev.h"
#include "hd44780.h"
#include "pcf8574.h"

#include "driver/gpio.h"

#include <stdint.h>
#include <utility>
#include <string_view>


namespace display {

    namespace {

        // LCD handle
        i2c_dev_t g_pcf8574{};
        bool      g_is_initialized = false;

        constexpr uint32_t POWER_ON_SCREEN_WAIT_MS   = 2'500;
        constexpr uint32_t POWER_DOWN_SCREEN_WAIT_MS = POWER_ON_SCREEN_WAIT_MS;

        // LCD config
        constexpr hd44780 g_hd44780_config = {
            .write_cb =
                [](const hd44780* lcd, uint8_t data) {
                    // Keep track of consecutive write errors
                    static uint32_t consv_err_counter = 0;
                    if (auto ret = pcf8574_port_write(&g_pcf8574, data); ret != ESP_OK) {
                        ESP_LOGW("LCD", "Failed to write data to LCD: %s", esp_err_to_name(ret));
                        consv_err_counter++;
                        if (consv_err_counter >= config::MAX_CONSV_ERRORS) {
                            ESP_LOGE("LCD", "Too many write failures: %u. Rebooting system", consv_err_counter);
                            utils::reboot();
                        }
                    } else {
                        consv_err_counter++;
                    }
                    return 0;
                },
            .pins =
                {
                    .rs = 0,
                    .e  = 2,
                    .d4 = 4,
                    .d5 = 5,
                    .d6 = 6,
                    .d7 = 7,
                    .bl = 3,
                },
            .font      = HD44780_FONT_5X8,
            .lines     = 2,
            .backlight = false,
        };


        // Helpers
        void cleanup() {
            TRY_THEN_LOG(gpio_set_level(config::LCD_LED_PIN, 0), "Failed power down the LCD backlight");
            TRY_THEN_LOG(gpio_reset_pin(config::LCD_LED_PIN), "Failed to reset the LCD backlight gpio");
            TRY_THEN_LOG(pcf8574_free_desc(&g_pcf8574), "Failed to free PCF8574 resources");
            TRY_THEN_LOG(i2cdev_done(), "Failed to cleanup the I2C interface");
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        // Initialize the I2C interface
        TRY(i2cdev_init());

        // Initialize the PCF8574 and the LCD
        TRY(pcf8574_init_desc(&g_pcf8574, config::LCD_ADDR, config::LCD_PORT, config::LCD_SDA_PIN, config::LCD_SCL_PIN));
        TRY(hd44780_init(&g_hd44780_config));

        // Initialize the backlight gpio pin
        constexpr gpio_config_t backlight_config = {
            .pin_bit_mask = 1ULL << std::to_underlying(config::LCD_LED_PIN),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        TRY(gpio_config(&backlight_config));

        g_is_initialized = true;
        return ESP_OK;
    }

    esp_err_t deinit() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        cleanup();
        return ESP_OK;
    }

    esp_err_t clear_line(uint8_t line) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (line >= config::LCD_ROWS) {
            return ESP_ERR_INVALID_ARG;
        }

        // TODO: Clear the given line

        return ESP_OK;
    }

    esp_err_t clear_screen() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        // TODO: Clear the full screen

        return ESP_OK;
    }

    esp_err_t put_char(unsigned char c, uint8_t column, uint8_t line) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (column >= config::LCD_COLUMNS || line >= config::LCD_ROWS) {
            return ESP_ERR_INVALID_ARG;
        }

        // TODO: Print the character at the given position

        return ESP_OK;
    }

    esp_err_t println(std::string_view msg, uint8_t line, bool pad_to_whitespace) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (msg.data() == nullptr || msg.empty() || msg.length() > config::LCD_COLUMNS || line >= config::LCD_ROWS) {
            return ESP_ERR_INVALID_ARG;
        }

        // TODO: Print the message at the given line

        return ESP_OK;
    }

    esp_err_t backlight_on(bool on) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(gpio_set_level(config::LCD_LED_PIN, static_cast<uint32_t>(on)));

        return ESP_OK;
    }

    esp_err_t bootup_screen() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(println("", 0));
        TRY(println("", 1));

        vTaskDelay(pdMS_TO_TICKS(POWER_ON_SCREEN_WAIT_MS));

        return ESP_OK;
    }

    esp_err_t shutdown_screen() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(println("", 0));
        TRY(println("", 1));

        vTaskDelay(pdMS_TO_TICKS(POWER_DOWN_SCREEN_WAIT_MS));

        return ESP_OK;
    }

} // namespace display
