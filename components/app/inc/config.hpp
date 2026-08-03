#pragma once

#include "stm32f1xx_hal.h"

#include <array>
#include <string_view>

namespace config {

    // Toggle assertions
    constexpr inline bool ASSERTS_ENABLED = true;

    // Core clock speed being used
    constexpr inline uint32_t CLOCK_SPEED_HZ = 72'000'000UL;

    struct gpio_pin_t {
        GPIO_TypeDef* port{};
        uint16_t      pin{};
    };

    // These can't be const since ST's HALs take the handles as non const pointers
    // ADC pins to be used for entropy gathering for random number generation
    inline std::array<gpio_pin_t, 6> ADC_PINS = {{
        {.port = GPIOA, .pin = GPIO_PIN_3}, // ADC External Channel 3
        {.port = GPIOA, .pin = GPIO_PIN_4}, // ADC External Channel 4
        {.port = GPIOA, .pin = GPIO_PIN_5}, // ADC External Channel 5
        {.port = GPIOA, .pin = GPIO_PIN_7}, // ADC External Channel 7
        {.port = GPIOB, .pin = GPIO_PIN_0}, // ADC External Channel 8
        {.port = GPIOB, .pin = GPIO_PIN_1}  // ADC External Channel 9
    }};

    // LCD I2C pins
    inline I2C_TypeDef* LCD_I2C_PORT = I2C1;
    inline gpio_pin_t   LCD_SCL      = {.port = GPIOB, .pin = GPIO_PIN_6};
    inline gpio_pin_t   LCD_SDA      = {.port = GPIOB, .pin = GPIO_PIN_7};
    inline gpio_pin_t   LCD_LED      = {.port = GPIOB, .pin = GPIO_PIN_9};

    // GSM module UART pins
    inline USART_TypeDef* GSM_UART_PORT = USART1;
    inline gpio_pin_t     GSM_GPIO_TX   = {.port = GPIOA, .pin = GPIO_PIN_9};
    inline gpio_pin_t     GSM_GPIO_RX   = {.port = GPIOA, .pin = GPIO_PIN_10};
    inline gpio_pin_t     GSM_GPIO_RST  = {.port = GPIOA, .pin = GPIO_PIN_11};

    // USART1 DMA Channels
    inline DMA_Channel_TypeDef* GSM_UART_DMA_TX = DMA1_Channel4;
    inline DMA_Channel_TypeDef* GSM_UART_DMA_RX = DMA1_Channel5;

    // Keypad matrix pins
    inline std::array<gpio_pin_t, 4> KEYPAD_ROW_PINS = {{
        {.port = GPIOA, .pin = GPIO_PIN_0}, // Row 0
        {.port = GPIOA, .pin = GPIO_PIN_1}, // Row 1
        {.port = GPIOA, .pin = GPIO_PIN_2}, // Row 2
        {.port = GPIOA, .pin = GPIO_PIN_6}, // Row 3
    }};

    inline std::array<gpio_pin_t, 4> KEYPAD_COLUMN_PINS = {{
        {.port = GPIOB, .pin = GPIO_PIN_3}, // Column 0
        {.port = GPIOB, .pin = GPIO_PIN_4}, // Column 1
        {.port = GPIOB, .pin = GPIO_PIN_5}, // Column 2
        {.port = GPIOB, .pin = GPIO_PIN_8}, // Column 3
    }};

    // Reed and Tamper switches' pins
    inline gpio_pin_t REED_SWITCH   = {.port = GPIOB, .pin = GPIO_PIN_12};
    inline gpio_pin_t TAMPER_SWITCH = {.port = GPIOB, .pin = GPIO_PIN_13};

    // FreeRTOS params
    constexpr inline uint8_t QUEUE_SIZE = 10;

    // SMSC for SIM Card being used. I am using GLO, so this is theirs
    constexpr inline std::string_view SIM_CARD_SMSC = "AT+CSCA=\"+2348050020020\"\r";

} // namespace config
