#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include <array>
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

    // Build a std::array of char from C-style arrays of char
    template<size_t... N>
    consteval auto concat(const char (&... strs)[N]) {
        // Get the length of the strings leaving out their null terminator while adding the extra
        // byte at the end for our own null terminator since this array of char feeds a C API
        constexpr size_t full_size = (... + (N - 1)) + 1;

        // Final storage for the to be built string
        std::array<char, full_size> final_string{};

        size_t idx    = 0;
        auto   append = [&](const char* str, size_t len) {
            // Append the string but leave out it's null terminator
            for (size_t i = 0; i < (len - 1); i++) {
                final_string[idx++] = str[i];
            }
        };

        // Append all the strings
        (append(strs, N), ...);

        // Append the final null terminator
        final_string[idx] = '\0';
        static_assert(idx == full_size - 1);

        return final_string;
    }

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
