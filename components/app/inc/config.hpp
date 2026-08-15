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

    // SIM800L UART pins
    constexpr inline gpio_num_t GSM_GPIO_TX_PIN  = GPIO_NUM_17;
    constexpr inline gpio_num_t GSM_GPIO_RX_PIN  = GPIO_NUM_18;
    constexpr inline gpio_num_t GSM_GPIO_RST_PIN = GPIO_NUM_21;

    constexpr inline uint32_t   GSM_BAUD_RATE = 9600;
    constexpr inline const char GLO_APN[]     = "gloflat";

    constexpr inline const char COUNTRY_PNUMBER_CODE[] = "+234";

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

    // Task stack configuration
    constexpr inline uint32_t DISPlAY_TASK_STACK    = 8192;
    constexpr inline uint32_t DISPlAY_TASK_PRIORITY = 12;

    constexpr inline uint32_t SYSTEM_TASK_STACK    = 8192;
    constexpr inline uint32_t SYSTEM_TASK_PRIORITY = 14;

} // namespace config
