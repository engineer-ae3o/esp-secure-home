#pragma once

#include "driver/gpio.h"

#include <array>

namespace config {

    // LCD I2C pins
    constexpr inline gpio_num_t LCD_SCL = GPIO_NUM_21;
    constexpr inline gpio_num_t LCD_SDA = GPIO_NUM_22;
    constexpr inline gpio_num_t LCD_LED = GPIO_NUM_23;

    // SIM800L's UART pins
    constexpr inline gpio_num_t GSM_GPIO_TX  = GPIO_NUM_12;
    constexpr inline gpio_num_t GSM_GPIO_RX  = GPIO_NUM_13;
    constexpr inline gpio_num_t GSM_GPIO_RST = GPIO_NUM_14;

    // Keypad matrix pins
    constexpr inline std::array<gpio_num_t, 4> KEYPAD_ROW_PINS = {{
        GPIO_NUM_15, // Row 0
        GPIO_NUM_16, // Row 1
        GPIO_NUM_17, // Row 2
        GPIO_NUM_18, // Row 3
    }};

    constexpr inline std::array<gpio_num_t, 4> KEYPAD_COLUMN_PINS = {{
        GPIO_NUM_25, // Column 0
        GPIO_NUM_26, // Column 1
        GPIO_NUM_27, // Column 2
        GPIO_NUM_28, // Column 3
    }};

    // Reed and Tamper switches' pins
    constexpr inline gpio_num_t REED_SWITCH   = GPIO_NUM_4;
    constexpr inline gpio_num_t TAMPER_SWITCH = GPIO_NUM_5;

    // SMSC for SIM Card being used. I am using GLO, so this is theirs
    constexpr inline const char SIM_CARD_SMSC[] = "AT+CSCA=\"+2348050020020\"\r";

    // Filesystem
    constexpr inline const char FILESYSTEM_BASE_PATH[]       = "/lfs";
    constexpr inline const char FILESYSTEM_PARTITION_LABEL[] = "storage";

} // namespace config
