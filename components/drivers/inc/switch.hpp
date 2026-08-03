#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "utils.hpp"

#include "driver/gpio.h"
#include "esp_err.h"

#include <utility>

namespace nc {

    enum class type_t : uint8_t {
        REED  = 0,
        LIMIT = 1,
    };

    struct config_t {
        gpio_num_t   pin{GPIO_NUM_NC};
        TaskHandle_t calling_task_handle{};
    };

    template<type_t type, bool init_isr_service = false, int isr_flags = ESP_INTR_FLAG_LEVEL1>
    class switch_t {
    public:
        /**
         * @brief Configures the pin to be used for detection for both
         *        NC reed and NC limit switches.
         * 
         * @return ESP_OK on success, error code otherwise.
         */
        esp_err_t init(const config_t& config) {
            if (m_is_initialized) {
                return ESP_ERR_INVALID_STATE;
            }

            m_config = config;

            // Set the pin as input with interrupt on the rising edge
            const gpio_config_t irq_config = {
                .pin_bit_mask = 1ULL << std::to_underlying(m_config.pin),
                .mode         = GPIO_MODE_INPUT,
                .pull_up_en   = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type    = GPIO_INTR_POSEDGE,
            };
            TRY(gpio_config(&irq_config));

            if constexpr (init_isr_service) {
                TRY_WITH_FUNC(gpio_install_isr_service(isr_flags), cleanup());
            }

            // Add the ISR for the IRQ pin
            TRY_WITH_FUNC(gpio_isr_handler_add(m_config.pin, irq_handler, this), cleanup());

            m_is_initialized = true;
            return ESP_OK;
        }

        /**
         * @brief Deinitializes the gpio pin for the switch's pin.
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

    private:
        bool     m_is_initialized{};
        config_t m_config{};

        void cleanup() {
            gpio_intr_disable(m_config.pin);
            gpio_isr_handler_remove(m_config.pin);
            gpio_reset_pin(m_config.pin);

            if constexpr (init_isr_service) {
                gpio_uninstall_isr_service();
            }

            m_config         = {};
            m_is_initialized = false;
        }

        static void irq_handler(void* arg) {
            const auto& driver = *static_cast<switch_t<type, init_isr_service, isr_flags>*>(arg);
            // Send notification to the calling task
            BaseType_t higher_priority_task_woken{};
            xTaskNotifyFromISR(driver->m_config.calling_task_handle, std::to_underlying(type), eSetBits, &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
    };

} // namespace nc
