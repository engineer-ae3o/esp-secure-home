#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

#include "portmacro.h"
#include "utils.hpp"

#include "driver/gpio.h"
#include "esp_err.h"

#include <array>
#include <numeric>
#include <utility>
#include <expected>
#include <algorithm>

namespace pad {

    constexpr inline uint32_t DEBOUNCE_MS = 50;

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
        StaticTimer_t m_deb_timer_tcb{};

        QueueHandle_t m_event_queue{};
        StaticQueue_t m_queue_tcb{};

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

            // Set all columns as input pullups with interrupt on falling edge
            const gpio_config_t col_pins_config = {
                .pin_bit_mask = 1ULL << (std::accumulate(m_config.col_pins.begin(), m_config.col_pins.end(), 0)),
                .mode         = GPIO_MODE_INPUT,
                .pull_up_en   = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type    = GPIO_INTR_NEGEDGE,
            };
            TRY(gpio_config(&col_pins_config));

            if constexpr (init_isr_service) {
                TRY_WITH_FUNC(gpio_install_isr_service(isr_flags), cleanup());
            }

            // Add the ISR for the column pins. The same ISR is used for all the column pins
            for (const auto& col_pin : m_config.col_pins) {
                TRY_WITH_FUNC(gpio_isr_handler_add(col_pin, irq_handler, this), cleanup());
                TRY_WITH_FUNC(gpio_intr_enable(col_pin), cleanup());
            }

            // Set all rows as output push pull
            const gpio_config_t row_pins_config = {
                .pin_bit_mask = 1ULL << (std::accumulate(m_config.row_pins.begin(), m_config.row_pins.end(), 0)),
                .mode         = GPIO_MODE_OUTPUT,
                .pull_up_en   = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type    = GPIO_INTR_DISABLE,
            };
            TRY_WITH_FUNC(gpio_config(&row_pins_config), cleanup());

            // Set all row pins low so any key press triggers the falling
            // edge interrupt on the pressed column key immediately
            for (const auto& row_pin : m_config.row_pins) {
                gpio_set_level(row_pin, 0);
            }

            // Can't fail since stack allocated
            m_event_queue    = xQueueCreateStatic(queue_length, sizeof(KEYS[0][0]), m_queue_buffer.data(), &m_queue_tcb);
            m_debounce_timer = xTimerCreateStatic("Deb timer", pdMS_TO_TICKS(DEBOUNCE_MS), pdFALSE, this, deb_timer_cb, &m_deb_timer_tcb);

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

            if (m_event_queue) {
                vQueueDelete(m_event_queue);
                m_queue_buffer.fill('\0');
                m_queue_tcb   = {};
                m_event_queue = nullptr;
            }

            if (m_debounce_timer) {
                xTimerStop(m_debounce_timer, portMAX_DELAY);
                xTimerDelete(m_debounce_timer, portMAX_DELAY);
                m_deb_timer_tcb  = {};
                m_debounce_timer = nullptr;
            }

            m_config         = {};
            m_is_initialized = false;
        }

        static void irq_handler(void* arg) {
            const auto& driver = *static_cast<keypad_t<init_isr_service, isr_flags, queue_length>*>(arg);

            // Disable interrupts on all the column pin's lines. They will be
            // re-enabled by the debounce timer after it's done scanning.
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

        static void deb_timer_cb(TimerHandle_t xTimer) {
            // Get timer ID
            const auto& keypad = *static_cast<keypad_t<init_isr_service, isr_flags, queue_length>*>(pvTimerGetTimerID(xTimer));

            // Set all row pins high
            for (const auto& row_pin : keypad.m_config.row_pins) {
                gpio_set_level(row_pin, 1);
            }

            // Keypad scanning
            uint8_t row{};
            uint8_t column{};
            bool    found{};

            [&] {
                // Get row on which the press was detected
                for (uint8_t r = 0; r < ROWS; r++) {
                    // Set a row pin low and read all its column pins to determine if any of
                    // them is the key that was pressed, that is, the pin that would be low.
                    gpio_set_level(keypad.m_config.row_pins[r], 0);

                    // If any column is low, then itself and its corresponding row are the right pair
                    for (uint8_t c = 0; c < COLUMNS; c++) {
                        if (gpio_get_level(keypad.m_config.col_pins[c]) == 0) {
                            row    = r;
                            column = c;
                            found  = true;
                            return;
                        }
                    }
                }
            }();

            // Send only first detected keypad press to the event queue
            if (found) {
                xQueueSend(keypad->m_event_queue, &KEYS[row][column], 0);
            }

            // Take all row pins back to their default low state
            for (const auto& row_pin : keypad.m_config.row_pins) {
                gpio_set_level(row_pin, 0);
            }

            // Re-enable interrupts on all the column pins
            for (const auto& col_pin : keypad.m_config.col_pins) {
                gpio_intr_enable(col_pin);
            }
        }
    };

} // namespace pad
