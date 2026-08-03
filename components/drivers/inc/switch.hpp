#pragma once

#include "stm32f1xx_hal.h"

#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <utility>

namespace nc {

    enum class type_t : uint8_t {
        REED  = 0x01U,
        LIMIT = 0x02U,
    };

    struct config_t {
        GPIO_TypeDef* port{};
        uint16_t      pin{};
        IRQn_Type     irq_type{};
        TaskHandle_t  calling_task_handle{};
    };

    template<type_t type>
    class switch_t {
    private:
        bool     m_is_initialized{};
        config_t m_config{};

    public:
        /**
         * @brief Configures the pin to be used for detection for both
         *        NC reed and NC limit switches
         * 
         * @note The irq handler still has to be called in the corresponding
         *       EXTI interrupt handler for the used pins
         */
        utils::error_t init(const config_t& config) {
            if (m_is_initialized) {
                return utils::error_t::ERR_INVALID_STATE;
            }

            m_config = config;

            // Configure the clock
            TRY(utils::gpio_enable_clk(m_config.port));

            // Set pin as input with interrupt on the rising edge
            GPIO_InitTypeDef pin_init = {
                .Pin   = m_config.pin,
                .Mode  = GPIO_MODE_IT_RISING,
                .Pull  = GPIO_PULLUP,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(m_config.port, &pin_init);

            // Enable interrupt and set priority to lowest
            HAL_NVIC_SetPriority(m_config.irq_type, 15, 0);
            HAL_NVIC_EnableIRQ(m_config.irq_type);

            m_is_initialized = true;

            return utils::error_t::NONE;
        }

        /**
         * @brief Deinitializes the pin and sets as analog to reduce power
         *        consumption on the pin. Also disables the corresponding NVIC irq
         */
        utils::error_t deinit() {
            if (!m_is_initialized) {
                return utils::error_t::ERR_INVALID_STATE;
            }

            // Set pin as analog to reduce power draw
            GPIO_InitTypeDef pin_deinit = {
                .Pin   = m_config.pin,
                .Mode  = GPIO_MODE_ANALOG,
                .Pull  = GPIO_NOPULL,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(m_config.port, &pin_deinit);

            HAL_NVIC_DisableIRQ(m_config.irq_type);

            m_config         = {};
            m_is_initialized = false;

            return utils::error_t::NONE;
        }

        /**
         * @brief Sends a task notification to the task that is to
         *        wait for the switch breaking detection
         * 
         * @note This has to be called from the irq handler in the
         *       interrupt vector table for the used gpio pin
         */
        void irq_handler() {
            // Clear the interrupt pending flag
            __HAL_GPIO_EXTI_CLEAR_IT(m_config.pin);

            // Send notification to calling task
            BaseType_t higher_priority_task_woken{};
            xTaskNotifyFromISR(m_config.calling_task_handle, std::to_underlying(type), eSetBits, &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
    };

} // namespace nc
