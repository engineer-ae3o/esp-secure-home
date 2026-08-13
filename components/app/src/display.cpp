#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "utils.hpp"
#include "config.hpp"
#include "display.hpp"

#include "i2cdev.h"
#include "hd44780.h"
#include "pcf8574.h"

#include "esp_err.h"
#include "esp_log.h"

#include <utility>
#include <cstdint>
#include <string_view>


namespace display {

    namespace {

        // LCD handle
        i2c_dev_t g_pcf8574{};
        bool      g_is_initialized = false;

        // LCD config
        constinit hd44780_t g_hd44780_config = {
            .write_cb =
                [](const hd44780_t* lcd, uint8_t data) {
                    static uint32_t consc_err_counter = 0;
                    if (auto ret = pcf8574_port_write(&g_pcf8574, data); ret != ESP_OK) {
                        ESP_LOGW("LCD", "Failed to write data to the LCD: %s", esp_err_to_name(ret));
                        consc_err_counter++;
                        if (consc_err_counter >= config::MAX_CONSC_ERRORS) {
                            ESP_LOGE("LCD", "Too many write failures: (%u consecutive failures). Rebooting system", consc_err_counter);
                            utils::reboot();
                        }
                        return ret;
                    }
                    consc_err_counter = 0;
                    return ESP_OK;
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

        constexpr uint32_t POWER_ON_SCREEN_WAIT_MS   = 2000;
        constexpr uint32_t POWER_DOWN_SCREEN_WAIT_MS = POWER_ON_SCREEN_WAIT_MS;

        void cleanup() {
            TRY_THEN_LOG(pcf8574_free_desc(&g_pcf8574), "Failed to free PCF8574 resources");
            TRY_THEN_LOG(i2cdev_done(), "Failed to cleanup the I2C interface");
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(i2cdev_init());
        TRY_WITH_FUNC(pcf8574_init_desc(&g_pcf8574, config::LCD_ADDR, config::LCD_PORT, config::LCD_SDA_PIN, config::LCD_SCL_PIN), cleanup());
        TRY_WITH_FUNC(hd44780_init(&g_hd44780_config), cleanup());

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

        // Set the cursor to the beginning of the line
        TRY(hd44780_gotoxy(&g_hd44780_config, 0, line));

        // Write whitespaces to clear all characters
        for (uint32_t i = 0; i < config::LCD_COLUMNS; ++i) {
            TRY(hd44780_putc(&g_hd44780_config, ' '));
        }

        // Move the cursor back to the beginning of the line
        TRY(hd44780_gotoxy(&g_hd44780_config, 0, line));

        return ESP_OK;
    }

    esp_err_t clear_screen() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(hd44780_clear(&g_hd44780_config));
        return ESP_OK;
    }

    esp_err_t put_char(char c, uint8_t column, uint8_t line) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }
        if (column >= config::LCD_COLUMNS || line >= config::LCD_ROWS) {
            return ESP_ERR_INVALID_ARG;
        }

        // Set the cursor and write the character
        TRY(hd44780_gotoxy(&g_hd44780_config, column, line));
        TRY(hd44780_putc(&g_hd44780_config, c));

        return ESP_OK;
    }

    esp_err_t println(std::string_view msg, uint8_t line, bool pad_to_whitespace) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }
        if (line >= config::LCD_ROWS || msg.length() > config::LCD_COLUMNS) {
            return ESP_ERR_INVALID_ARG;
        }

        TRY(hd44780_gotoxy(&g_hd44780_config, 0, line));

        // Write characters of the message
        for (char c : msg) {
            TRY(hd44780_putc(&g_hd44780_config, c));
        }

        if (pad_to_whitespace) {
            const size_t pad_count = config::LCD_COLUMNS - msg.length();
            for (size_t i = 0; i < pad_count; ++i) {
                TRY(hd44780_putc(&g_hd44780_config, ' '));
            }
        }

        return ESP_OK;
    }

    esp_err_t backlight_on(bool on) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(hd44780_switch_backlight(&g_hd44780_config, static_cast<uint32_t>(on)));
        return ESP_OK;
    }

    esp_err_t bootup_screen() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(println(" Booting up.... ", 0));
        vTaskDelay(pdMS_TO_TICKS(POWER_ON_SCREEN_WAIT_MS));

        TRY(println("  Initializing  ", 0));
        TRY(println("   components   ", 1));
        vTaskDelay(pdMS_TO_TICKS(POWER_ON_SCREEN_WAIT_MS));

        TRY(println("     Setup      ", 0));
        TRY(println("    Complete    ", 1));
        vTaskDelay(pdMS_TO_TICKS(POWER_ON_SCREEN_WAIT_MS));

        return ESP_OK;
    }

    esp_err_t shutdown_screen() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(println(" Shutting down  ", 0));
        TRY(println(" all components ", 1));
        vTaskDelay(pdMS_TO_TICKS(POWER_DOWN_SCREEN_WAIT_MS));

        TRY(println("    Shutdown    ", 0));
        TRY(println("    Complete    ", 1));
        vTaskDelay(pdMS_TO_TICKS(POWER_DOWN_SCREEN_WAIT_MS));

        return ESP_OK;
    }

} // namespace display
