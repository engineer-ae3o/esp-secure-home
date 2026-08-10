#pragma once


#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include <array>


namespace config {

    // LCD configuration
    constexpr inline uint8_t    LCD_ADDR = 0x27;
    constexpr inline i2c_port_t LCD_PORT = I2C_NUM_0;

    constexpr inline gpio_num_t LCD_SCL_PIN = GPIO_NUM_21;
    constexpr inline gpio_num_t LCD_SDA_PIN = GPIO_NUM_26;
    constexpr inline gpio_num_t LCD_LED_PIN = GPIO_NUM_27;

    constexpr inline uint32_t LCD_COLUMNS = 16;
    constexpr inline uint32_t LCD_ROWS    = 2;

    // SIM800L's UART pins
    constexpr inline gpio_num_t GSM_GPIO_TX_PIN  = GPIO_NUM_12;
    constexpr inline gpio_num_t GSM_GPIO_RX_PIN  = GPIO_NUM_13;
    constexpr inline gpio_num_t GSM_GPIO_RST_PIN = GPIO_NUM_14;

    // Keypad matrix pins
    constexpr inline std::array<gpio_num_t, 4> KEYPAD_ROW_PINS = {{
        GPIO_NUM_15, // Row 0
        GPIO_NUM_16, // Row 1
        GPIO_NUM_17, // Row 2
        GPIO_NUM_18, // Row 3
    }};

    constexpr inline std::array<gpio_num_t, 4> KEYPAD_COLUMN_PINS = {{
        GPIO_NUM_2,  // Column 0
        GPIO_NUM_26, // Column 1
        GPIO_NUM_27, // Column 2
        GPIO_NUM_28, // Column 3
    }};

    // Reed and Tamper switches' pins
    constexpr inline gpio_num_t REED_SWITCH_PIN   = GPIO_NUM_4;
    constexpr inline gpio_num_t TAMPER_SWITCH_PIN = GPIO_NUM_5;

    // SMSC for SIM Card being used. I am using GLO, so this is theirs
    constexpr inline const char SIM_CARD_SMSC[] = "+2348050020020";

    // Filesystem
    constexpr inline const char FILESYSTEM_BASE_PATH[]       = "/lfs";
    constexpr inline const char FILESYSTEM_PARTITION_LABEL[] = "storage";

    // Error tracking
    constexpr inline uint32_t MAX_CONSV_ERRORS = 5;

    // Task stacks
    constexpr inline uint32_t DISPLAY_TASK_STACK    = 4096;
    constexpr inline uint32_t DISPLAY_TASK_PRIORITY = 8;

} // namespace config
