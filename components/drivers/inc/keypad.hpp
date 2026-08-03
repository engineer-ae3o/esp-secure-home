#pragma once

#include "stm32f1xx_hal.h"

#include "utils.hpp"

#include "FreeRTOS.h"
#include "timers.h"
#include "queue.h"

#include <array>
#include <expected>

namespace pad {

    constexpr inline uint8_t DEBOUNCE_TIME_MS = 50;

    constexpr inline uint8_t ROWS    = 4;
    constexpr inline uint8_t COLUMNS = 4;

    constexpr inline std::array<std::array<char, COLUMNS>, ROWS> KEYS = {{
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'},
    }};

    struct config_t {
        GPIO_TypeDef* row_port{};
        GPIO_TypeDef* col_port{};

        std::array<uint16_t, ROWS>    row_pins{};
        std::array<uint16_t, COLUMNS> col_pins{};
    };

    template<uint8_t queue_length>
    class keypad_t {
    private:
        bool m_is_initialized{};

        config_t m_config{};
        uint16_t m_row_pins{};
        uint16_t m_col_pins{};

        TimerHandle_t m_debounce_timer{};
        StaticTimer_t m_debounce_timer_structure{};

        QueueHandle_t                                            m_event_queue{};
        StaticQueue_t                                            m_queue_structure{};
        std::array<uint8_t, (queue_length * sizeof(KEYS[0][0]))> m_queue_buffer{};

    public:
        keypad_t() = default;

        ~keypad_t() noexcept {
            if (m_is_initialized) {
                (void)deinit();
            }
        };

        keypad_t(const keypad_t&)            = delete;
        keypad_t& operator=(const keypad_t&) = delete;

        keypad_t(keypad_t&&)            = delete;
        keypad_t& operator=(keypad_t&&) = delete;

        /**
         * @brief Initializes and configures the gpio pins according to how
         *        they will be used for the keypad scanning.
         * 
         * @param[in] config Config struct which contains the pins to use
         *                   to configure the keypad
         * 
         * @note The interrupts still have to be enabled with NVIC
         */
        utils::error_t init(const config_t& config) {
            if (m_is_initialized) {
                return utils::error_t::ERR_INVALID_STATE;
            }

            m_config = config;

            // Do this now to avoid recomputing the OR'ed mask multiple times
            m_col_pins = (m_config.col_pins[0] | m_config.col_pins[1] | m_config.col_pins[2] | m_config.col_pins[3]);
            m_row_pins = (m_config.row_pins[0] | m_config.row_pins[1] | m_config.row_pins[2] | m_config.row_pins[3]);

            // Configure the clocks
            TRY(utils::gpio_enable_clk(m_config.col_port));
            TRY(utils::gpio_enable_clk(m_config.row_port));

            // Set all columns as input pullups with interrupt on falling edge
            GPIO_InitTypeDef col_init = {
                .Pin   = m_col_pins,
                .Mode  = GPIO_MODE_IT_FALLING,
                .Pull  = GPIO_PULLUP,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(m_config.col_port, &col_init);

            // Set all rows as output push pull
            GPIO_InitTypeDef row_init = {
                .Pin   = m_row_pins,
                .Mode  = GPIO_MODE_OUTPUT_PP,
                .Pull  = GPIO_NOPULL,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(m_config.row_port, &row_init);

            // Set all row pins low by default so any press triggers
            // the falling edge irq on the pressed column key immediately
            HAL_GPIO_WritePin(m_config.row_port, m_row_pins, GPIO_PIN_RESET);

            // Can't fail since stack allocated
            m_event_queue    = xQueueCreateStatic(queue_length, sizeof(KEYS[0][0]), m_queue_buffer.data(), &m_queue_structure);
            m_debounce_timer = xTimerCreateStatic(
                "Debounce_timer", pdMS_TO_TICKS(DEBOUNCE_TIME_MS), pdFALSE, this, debounce_timer_cb, &m_debounce_timer_structure);

            m_is_initialized = true;

            return utils::error_t::NONE;
        }

        /**
         * @brief Deinitializes the gpio pins used and sets the pins to
         *        analog mode so as to reduce power consumption
         */
        utils::error_t deinit() {
            if (!m_is_initialized) {
                return utils::error_t::ERR_INVALID_STATE;
            }

            // Sets all pins as analog to reduce power draw
            GPIO_InitTypeDef col_init = {
                .Pin   = m_col_pins,
                .Mode  = GPIO_MODE_ANALOG,
                .Pull  = GPIO_NOPULL,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(m_config.col_port, &col_init);

            GPIO_InitTypeDef row_init = {
                .Pin   = m_row_pins,
                .Mode  = GPIO_MODE_ANALOG,
                .Pull  = GPIO_NOPULL,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(m_config.row_port, &row_init);

            m_config   = {};
            m_row_pins = {};
            m_col_pins = {};

            if (static_cast<bool>(m_event_queue)) {
                vQueueDelete(m_event_queue);
                m_queue_structure = {};
                m_queue_buffer    = {};
                m_event_queue     = nullptr;
            }

            if (static_cast<bool>(m_debounce_timer)) {
                xTimerStop(m_debounce_timer, portMAX_DELAY);
                xTimerDelete(m_debounce_timer, portMAX_DELAY);
                m_debounce_timer_structure = {};
                m_debounce_timer           = nullptr;
            }

            m_is_initialized = false;

            return utils::error_t::NONE;
        }

        /**
         * @brief Returns the queue in which keypad events are
         *        pushed into. Pretty straightforward.
         * 
         * @return The event queue
         */
        [[nodiscard]] std::expected<QueueHandle_t, utils::error_t> get_event_queue() const {
            if (!m_is_initialized) {
                return std::unexpected(utils::error_t::ERR_INVALID_STATE);
            }
            return m_event_queue;
        };

        /**
         * @brief This function should be called from all the NVIC
         *        interrupt handlers for the used gpio pins
         */
        void irq_handler() {
            // Clear the interrupt pending flags on all pins
            __HAL_GPIO_EXTI_CLEAR_IT(m_col_pins);

            // And disable interrupts globally on the port by masking it.
            // Will be reenabled by the debounce timer after it's done scanning
            EXTI->IMR &= ~m_col_pins;

            // Start the debounce timer
            BaseType_t higher_priority_task_woken = pdFALSE;
            // cppcheck-suppress duplicateExpression
            xTimerStartFromISR(m_debounce_timer, &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }

    private:
        static void debounce_timer_cb(TimerHandle_t xTimer) {
            // Get timer ID
            auto keypad = static_cast<keypad_t*>(pvTimerGetTimerID(xTimer));

            // Set all row pins high
            HAL_GPIO_WritePin(keypad->m_config.row_port, keypad->m_row_pins, GPIO_PIN_SET);

            // Keypad scanning
            uint8_t row{};
            uint8_t column{};
            bool    found{};

            [&] {
                // Get row on which the press was detected
                for (uint8_t i = 0; i < ROWS; i++) {
                    // Set a row pin low and read all its column pins to determine if any of
                    // them is the key that was pressed, that is, the pin that would be low
                    HAL_GPIO_WritePin(keypad->m_config.row_port, keypad->m_config.row_pins[i], GPIO_PIN_RESET);

                    // If any column is low, then itself and its corresponding row is the right one
                    for (uint8_t j = 0; j < COLUMNS; j++) {
                        if (HAL_GPIO_ReadPin(keypad->m_config.col_port, keypad->m_config.col_pins[j]) == GPIO_PIN_RESET) {
                            row    = i;
                            column = j;
                            found  = true;
                            return;
                        }
                    }
                }
            }();

            // Send only first detected keypad press to queue and if we found the key
            if (found) {
                xQueueSend(keypad->m_event_queue, &KEYS[row][column], 0);
            }

            // Take all row pins back to their default low state
            HAL_GPIO_WritePin(keypad->m_config.row_port, keypad->m_row_pins, GPIO_PIN_RESET);

            // Clear the interrupt pending flags on all pins before unmasking the EXTI interrupts
            __HAL_GPIO_EXTI_CLEAR_IT(keypad->m_col_pins);

            // Enable interrupts by unmasking EXTI interrupts
            EXTI->IMR |= keypad->m_col_pins;
        }
    };

} // namespace pad
