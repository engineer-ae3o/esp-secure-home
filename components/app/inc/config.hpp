#pragma once


#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include <array>


namespace config {

    // LCD configuration (PCF8574 standard address)
    constexpr inline uint8_t    LCD_ADDR = 0x27;
    constexpr inline i2c_port_t LCD_PORT = I2C_NUM_0;

    constexpr inline gpio_num_t LCD_SDA_PIN = GPIO_NUM_1;
    constexpr inline gpio_num_t LCD_SCL_PIN = GPIO_NUM_2;
    constexpr inline gpio_num_t LCD_LED_PIN = GPIO_NUM_3;

    constexpr inline uint32_t LCD_COLUMNS = 16;
    constexpr inline uint32_t LCD_ROWS    = 2;

    // SIM800L UART pins
    constexpr inline gpio_num_t GSM_GPIO_TX_PIN  = GPIO_NUM_17;
    constexpr inline gpio_num_t GSM_GPIO_RX_PIN  = GPIO_NUM_18;
    constexpr inline gpio_num_t GSM_GPIO_RST_PIN = GPIO_NUM_21;

    // Keypad matrix pins (4x4)
    constexpr inline std::array<gpio_num_t, 4> KEYPAD_ROW_PINS = {{
        GPIO_NUM_4, // Row 0
        GPIO_NUM_5, // Row 1
        GPIO_NUM_6, // Row 2
        GPIO_NUM_7, // Row 3
    }};

    constexpr inline std::array<gpio_num_t, 4> KEYPAD_COLUMN_PINS = {{
        GPIO_NUM_8,  // Column 0
        GPIO_NUM_9,  // Column 1
        GPIO_NUM_10, // Column 2
        GPIO_NUM_11, // Column 3
    }};

    // Reed and Tamper switches
    constexpr inline gpio_num_t REED_SWITCH_PIN   = GPIO_NUM_12;
    constexpr inline gpio_num_t TAMPER_SWITCH_PIN = GPIO_NUM_13;

    // SMSC for the used SIM Card (Glo Nigeria)
    constexpr inline char     SIM_CARD_SMSC[]  = "+2348050020020";
    constexpr inline uint32_t PHONE_NUMBER_LEN = 11;
    constexpr inline uint32_t MAX_PNUMBERS     = 10;

    // Filesystem
    constexpr inline const char FILESYSTEM_BASE_PATH[]       = "/lfs";
    constexpr inline const char FILESYSTEM_PARTITION_LABEL[] = "storage";

    // Error tracking
    constexpr inline uint32_t MAX_CONSC_ERRORS = 5;

    // Task stack configuration
    constexpr inline uint32_t DISPLAY_TASK_STACK    = 4096;
    constexpr inline uint32_t DISPLAY_TASK_PRIORITY = 8;

} // namespace config
