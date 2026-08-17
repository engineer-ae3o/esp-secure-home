#pragma once


#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include <array>
#include <string_view>


namespace config {

    // LCD configuration (PCF8574 standard address)
    constexpr inline uint8_t    LCD_ADDR = 0x27;
    constexpr inline i2c_port_t LCD_PORT = I2C_NUM_0;

    constexpr inline gpio_num_t LCD_SDA_PIN = GPIO_NUM_1;
    constexpr inline gpio_num_t LCD_SCL_PIN = GPIO_NUM_2;

    constexpr inline uint32_t LCD_COLUMNS = 16;
    constexpr inline uint32_t LCD_ROWS    = 2;

    // WiFi / Telegram
    constexpr inline const char TELEGRAM_BOT_TOKEN[] = "REPLACE_WITH_YOUR_BOT_TOKEN";

    // Keypad matrix pins (4x4)
    constexpr inline std::array<gpio_num_t, 4> KEYPAD_ROW_PINS = {{
        GPIO_NUM_6,  // Row 0
        GPIO_NUM_7,  // Row 1
        GPIO_NUM_15, // Row 2
        GPIO_NUM_16, // Row 3
    }};

    constexpr inline std::array<gpio_num_t, 4> KEYPAD_COLUMN_PINS = {{
        GPIO_NUM_3,  // Column 0
        GPIO_NUM_9,  // Column 1
        GPIO_NUM_10, // Column 2
        GPIO_NUM_11, // Column 3
    }};

    // Reed and Tamper switches
    constexpr inline gpio_num_t REED_SWITCH_PIN   = GPIO_NUM_12;
    constexpr inline gpio_num_t TAMPER_SWITCH_PIN = GPIO_NUM_13;

    // Filesystem
    constexpr inline const char FILESYSTEM_BASE_PATH[]       = "/lfs";
    constexpr inline const char FILESYSTEM_PARTITION_LABEL[] = "storage";

    // Error tracking
    constexpr inline uint32_t MAX_CONSC_ERRORS = 5;

    // Admin mode / keypad UI
    constexpr inline uint32_t MAX_PASSWORD_ATTEMPTS  = 5;              // Brute force protection
    constexpr inline uint32_t LOCKOUT_DURATION_MS    = 60 * 1000;      // How long keypad is locked after too many failed attempts
    constexpr inline uint32_t ADMIN_IDLE_TIMEOUT_MS  = 10 * 60 * 1000; // Auto-logout from admin mode after this much inactivity
    constexpr inline uint32_t UI_MESSAGE_DURATION_MS = 2 * 1000;       // How long transient feedback messages (e.g. "Wrong password") are shown
    constexpr inline uint32_t KEYPAD_POLL_PERIOD_MS  = 100;            // How often the system task polls the keypad queue

    // Task stack configuration
    constexpr inline uint32_t DISPlAY_TASK_STACK    = 6 * 1024;
    constexpr inline uint32_t DISPlAY_TASK_PRIORITY = 10;

    constexpr inline uint32_t SWITCH_TASK_STACK    = 6 * 1024;
    constexpr inline uint32_t SWITCH_TASK_PRIORITY = 16;

    constexpr inline uint32_t SYSTEM_TASK_STACK    = 8 * 1024;
    constexpr inline uint32_t SYSTEM_TASK_PRIORITY = 14;

    constexpr inline uint32_t WIFI_TASK_STACK    = 4 * 1024;
    constexpr inline uint32_t WIFI_TASK_PRIORITY = 8;

    constexpr inline uint32_t MAX_WIFI_CONSC_STATUS_ERRORS = 10;
    constexpr inline uint32_t WIFI_TASK_PERIOD_MS          = 30 * 1000;

} // namespace config
