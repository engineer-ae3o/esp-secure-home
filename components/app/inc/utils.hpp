#pragma once

#include "config.hpp"

#include "SEGGER_RTT.h"

#include "FreeRTOS.h"
#include "portmacro.h"
#include "projdefs.h"
#include "semphr.h"
#include "task.h"

#include <cstdint>
#include <utility>
#include <source_location>

namespace utils {

    namespace {

        SemaphoreHandle_t g_log_mutex{};
        StaticSemaphore_t g_log_mutex_buffer{};

        struct scoped_mutex_t {
        public:
            scoped_mutex_t() {
                xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(portMAX_DELAY));
            }

            ~scoped_mutex_t() {
                xSemaphoreGive(g_log_mutex);
            }

            scoped_mutex_t(const scoped_mutex_t&)            = delete;
            scoped_mutex_t& operator=(const scoped_mutex_t&) = delete;
            scoped_mutex_t(scoped_mutex_t&&)                 = delete;
            scoped_mutex_t& operator=(scoped_mutex_t&&)      = delete;
        };

    } // namespace

    inline void init() {
        g_log_mutex = xSemaphoreCreateMutexStatic(&g_log_mutex_buffer);
    }

    inline void deinit() {
        if (g_log_mutex) {
            vSemaphoreDelete(g_log_mutex);
            g_log_mutex = nullptr;
        }
    }

    enum class level_t : uint8_t { INFO, WARN, ERROR };

    constexpr inline level_t SYSTEM_LOG_LEVEL = level_t::INFO;

    template<level_t level, typename... Args>
    void log(const char* tag, const char* fmt, Args... args) {
        // Filter based on the system log level
        if constexpr (std::to_underlying(level) >= std::to_underlying(SYSTEM_LOG_LEVEL)) {
            // Acquire lock
            [[maybe_unused]] scoped_mutex_t mutex;

            // Set the output color
            if constexpr (level == level_t::INFO) {
                SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_GREEN);
            } else if constexpr (level == level_t::WARN) {
                SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_YELLOW);
            } else if constexpr (level == level_t::ERROR) {
                SEGGER_RTT_WriteString(0, RTT_CTRL_TEXT_RED);
            } else {
                static_assert(false);
            }

            // Get time stamp of the caller and write tag.
            const uint32_t time_stamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            SEGGER_RTT_printf(0, "(%lums) [%s]: ", time_stamp_ms, tag);

            // Write the actual message
            SEGGER_RTT_printf(0, fmt, args...);

            // End line and clear the used color
            SEGGER_RTT_WriteString(0, "\r\n" RTT_CTRL_RESET);
        }
    }

    enum class [[nodiscard]] error_t : uint8_t {
        // Success
        NONE,

        // Standard errors common to all modules
        ERR_FAIL,
        ERR_TIMEOUT,
        ERR_INVALID_STATE,
        ERR_INVALID_ARG,
        ERR_HAL_FAIL,

        // GSM module driver errors
        GSM_SIM_NOT_FOUND,
        GSM_SIM_NOT_REGISTERED,
        GSM_MODULE_NOT_ALIVE,
        GSM_BAD_NETWORK_CONN,
        GSM_SMS_SEND_FAIL,

        // File IO errors
        FILE_FAILED_TO_SEEK,
        FILE_FAILED_TO_SYNC,
        FILE_FAILED_TO_READ,
        FILE_FAILED_TO_WRITE,
        FILE_FAILED_TO_OPEN,
        FILE_FAILED_TO_CLOSE,
        FILE_FS_CORRUPTED,
        FILE_FS_FAILED_TO_FORMAT,
        FILE_FS_FAILED_TO_MOUNT,
        FILE_FS_FAILED_TO_UNMOUNT,
    };

    [[noreturn]] inline void panic() {
        log<level_t::ERROR>("Panic", "System ran into a fatal error. Halting...");
        __asm volatile("bkpt #0");
        while (true) {
        }
    }

    [[noreturn]] inline void restart(const char* reason, std::source_location loc = std::source_location::current()) {
        log<level_t::INFO>("Restart", "Reboot requested from %s (%s:%u)", loc.function_name(), loc.file_name(), loc.line());
        log<level_t::INFO>("Restart", "Reason: %s", reason);
        NVIC_SystemReset();
        while (true) {
        }
    }

    inline void assert_check(bool cond, const char* msg, std::source_location loc = std::source_location::current()) {
        if constexpr (config::ASSERTS_ENABLED) {
            if (!cond) {
                log<level_t::ERROR>(
                    "Assert", "Assert failed: %s. Source: %s (%s:%u)", msg, loc.function_name(), loc.file_name(), loc.line());
                panic();
            }
        }
    }

    inline error_t gpio_enable_clk(GPIO_TypeDef* handle) {
        if (handle == GPIOA) {
            __HAL_RCC_GPIOA_CLK_ENABLE();
        } else if (handle == GPIOB) {
            __HAL_RCC_GPIOB_CLK_ENABLE();
        } else if (handle == GPIOC) {
            __HAL_RCC_GPIOC_CLK_ENABLE();
        } else if (handle == GPIOD) {
            __HAL_RCC_GPIOD_CLK_ENABLE();
        } else {
            return error_t::ERR_INVALID_ARG;
        }
        return error_t::NONE;
    }

    // Needed for conversion since FreeRTOS uses words
    consteval size_t bytes_to_words(size_t byte) {
        return byte / 4;
    }

} // namespace utils

#define ASSERT(x) utils::assert_check(x, #x)

// Macros for error checking and propagation to reduce verbosity
#define TRY(func)                                                                                                                          \
    do {                                                                                                                                   \
        if (auto ret_ = (func); ret_ != utils::error_t::NONE) {                                                                            \
            return ret_;                                                                                                                   \
        }                                                                                                                                  \
    } while (0)

#define TRY_HAL(func)                                                                                                                      \
    do {                                                                                                                                   \
        if (auto ret_ = (func); ret_ != HAL_OK) {                                                                                          \
            return utils::error_t::ERR_HAL_FAIL;                                                                                           \
        }                                                                                                                                  \
    } while (0)
