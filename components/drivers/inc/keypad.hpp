#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

#include "portmacro.h"
#include "utils.hpp"

#include "driver/gpio.h"
#include "esp_err.h"

#include <array>
#include <expected>

namespace pad {

    constexpr inline uint32_t DEBOUNCE_TIME_MS = 50;

    constexpr inline uint32_t ROWS    = 4;
    constexpr inline uint32_t COLUMNS = 4;

    constexpr inline std::array<std::array<char, COLUMNS>, ROWS> KEYS = {{
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'},
    }};

    struct config_t {
        std::array<gpio_num_t, ROWS>    row_pins{};
        std::array<gpio_num_t, COLUMNS> col_pins{};
    };

    template<bool init_isr_service = true, int isr_flags = ESP_INTR_FLAG_LEVEL1, uint32_t queue_length = ROWS * COLUMNS>
    class keypad_t {
    private:
        bool     m_is_initialized{};
        config_t m_config{};

        TimerHandle_t m_debounce_timer{};
        StaticTimer_t m_debounce_timer_structure{};

        QueueHandle_t m_event_queue{};
        StaticQueue_t m_queue_structure{};

        std::array<uint8_t, (queue_length * sizeof(KEYS[0][0]))> m_queue_buffer{};

    public:
        keypad_t() = default;

        ~keypad_t() noexcept {
            if (m_is_initialized) {
                cleanup();
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
         *                   to configure the keypad.
         * 
         * @return ESP_OK on success, error code otherwise.
         */
        esp_err_t init(const config_t& config) {
            if (m_is_initialized) {
                return ESP_ERR_INVALID_STATE;
            }

            m_config = config;

            // Do this now to avoid recomputing the OR'ed mask multiple times
            m_col_pins = (m_config.col_pins[0] | m_config.col_pins[1] | m_config.col_pins[2] | m_config.col_pins[3]);
            m_row_pins = (m_config.row_pins[0] | m_config.row_pins[1] | m_config.row_pins[2] | m_config.row_pins[3]);

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

            return ESP_OK;
        }

        /**
         * @brief Deinitializes the gpio pins used for the scanning.
         * 
         * @return ESP_OK on success, error code otherwise.
         */
        esp_err_t deinit() {
            if (!m_is_initialized) {
                return ESP_ERR_INVALID_STATE;
            }

            cleanup();
            return ESP_OK;
        }

        /**
         * @brief Returns the queue in which keypad events are
         *        pushed into. Pretty straightforward.
         * 
         * @return The event queue
         */
        [[nodiscard]] std::expected<QueueHandle_t, esp_err_t> get_event_queue() const {
            if (!m_is_initialized) {
                return std::unexpected(ESP_ERR_INVALID_STATE);
            }
            return m_event_queue;
        };

    private:
        void cleanup() {
            // Deinitialize all used gpio pins
            for (const auto& col_pin : m_config.col_pins) {
                gpio_intr_disable(col_pin);
                gpio_isr_handler_remove(col_pin);
                gpio_reset_pin(col_pin);
            }

            for (const auto& row_pin : m_config.row_pins) {
                gpio_intr_disable(row_pin);
                gpio_isr_handler_remove(row_pin);
                gpio_reset_pin(row_pin);
            }

            if constexpr (init_isr_service) {
                gpio_uninstall_isr_service();
            }

            m_config = {};

            if (m_event_queue) {
                vQueueDelete(m_event_queue);
                m_queue_buffer.fill('\0');
                m_queue_structure = {};
                m_event_queue     = nullptr;
            }

            if (m_debounce_timer) {
                xTimerStop(m_debounce_timer, portMAX_DELAY);
                xTimerDelete(m_debounce_timer, portMAX_DELAY);
                m_debounce_timer_structure = {};
                m_debounce_timer           = nullptr;
            }

            m_is_initialized = false;
        }

        static void irq_handler(void* arg) {
            const auto& driver = *static_cast<keypad_t<init_isr_service, isr_flags, queue_length>*>(arg);

            // Disable interrupts on all the column pins. They will be
            // reenabled by the debounce timer after it's done scanning.
            for (const auto& col_pin : driver.m_config.col_pins) {
                gpio_intr_disable(col_pin);
            }

            // Start the debounce timer
            BaseType_t higher_priority_task_woken = pdFALSE;
            xTimerStartFromISR(driver.m_debounce_timer, &higher_priority_task_woken);

            // Yield if the timer was woken up, but reenable the interrupts if it wasn't
            if (higher_priority_task_woken) {
                portYIELD_FROM_ISR();
            } else {
                for (const auto& col_pin : driver.m_config.col_pins) {
                    gpio_intr_enable(col_pin);
                }
            }
        }

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
