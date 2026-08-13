#include "display.hpp"
#include "system.hpp"

#include "esp_err.h"
#include "esp_log.h"


namespace sys {

    constexpr const char* TAG = "System";

    void reed_switch_broken(bool is_admin_mode) {
        if (is_admin_mode) {
            // The reed switch guards the doors. It is standard behaviour for it to be broken in admin mode.
            if (auto ret = display::println("  A door has   ", 0); ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send message to the display. Perharps it is disconnected?: %s", esp_err_to_name(ret));
            }
            if (auto ret = display::println("  been opened  ", 1); ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send message to the display. Perharps it is disconnected?: %s", esp_err_to_name(ret));
            }
            ESP_LOGI(TAG, "Door opened in admin mode");
        } else {
            // Intruder alert if any switch has been broken while not in admin mode
        }
    }

    void tamper_switch_broken(bool is_admin_mode) {
        if (is_admin_mode) {
            // The tamper switch guards the control box containing the components. Not standard behaviour for
            // even a valid user to open the box. Display a warning to the user, but no need for an SMS.
            if (auto ret = display::println("You should not", 0); ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send message to the display. Perharps it is disconnected?: %s", esp_err_to_name(ret));
            }
            if (auto ret = display::println("open the control box.", 1); ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send message to the display. Perharps it is disconnected?: %s", esp_err_to_name(ret));
            }
        } else {
            // Intruder alert if any switch has been broken while not in admin mode
        }
    }

} // namespace sys
