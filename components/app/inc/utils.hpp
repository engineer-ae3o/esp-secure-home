#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include <source_location>


#define TRY(func)                                                                                                                                    \
    do {                                                                                                                                             \
        if (auto ret_ = (func); ret_ != ESP_OK) {                                                                                                    \
            ESP_LOGE("ERROR", "%s:(%s):Line %d failed: %s", __FILE__, __PRETTY_FUNCTION__, __LINE__, esp_err_to_name(ret_));                         \
            return ret_;                                                                                                                             \
        }                                                                                                                                            \
    } while (0)

#define TRY_WITH_FUNC(func, err_cb)                                                                                                                  \
    do {                                                                                                                                             \
        if (auto ret_ = (func); ret_ != ESP_OK) {                                                                                                    \
            ESP_LOGE("ERROR", "%s:(%s):Line %d failed: %s", __FILE__, __PRETTY_FUNCTION__, __LINE__, esp_err_to_name(ret_));                         \
            (err_cb);                                                                                                                                \
            return ret_;                                                                                                                             \
        }                                                                                                                                            \
    } while (0)

#define TRY_WITH_FUNC_VOID(func, err_cb)                                                                                                             \
    do {                                                                                                                                             \
        if (auto ret_ = (func); ret_ != ESP_OK) {                                                                                                    \
            ESP_LOGE("ERROR", "%s:(%s):Line %d failed: %s", __FILE__, __PRETTY_FUNCTION__, __LINE__, esp_err_to_name(ret_));                         \
            (err_cb);                                                                                                                                \
        }                                                                                                                                            \
    } while (0)

#define TRY_THEN_LOG(func, msg)                                                                                                                      \
    do {                                                                                                                                             \
        if (auto ret_ = (func); ret_ != ESP_OK) {                                                                                                    \
            ESP_LOGE("ERROR", "%s: %s", msg, esp_err_to_name(ret_));                                                                                 \
        }                                                                                                                                            \
    } while (0)

namespace utils {

    [[noreturn]] inline void fatal(const std::source_location& location = std::source_location::current()) {
        ESP_LOGE("FATAL", "Unrecoverable error from %s (%s): %u", location.function_name(), location.file_name(), location.line());
        esp_system_abort("Fatal error. Cannot recover");
    }

    [[noreturn]] inline void reboot(const std::source_location& location = std::source_location::current()) {
        ESP_LOGE("FATAL", "Unrecoverable error from %s (%s): %u", location.function_name(), location.file_name(), location.line());
        ESP_LOGE("FATAL", "Rebooting system.");
        esp_restart();
    }

} // namespace utils
