#include "telegram.hpp"
#include "display.hpp"
#include "storage.hpp"
#include "system.hpp"
#include "config.hpp"
#include "wifi.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "utils.hpp"


namespace sys {

    namespace {

        constexpr const char* TAG = "System";

        void send_alert(std::string_view msg) {
            if (!wifi::is_connected()) {
                ESP_LOGW(TAG, "Alert requested, but WiFi is not connected. Dropping: %.*s", static_cast<int>(msg.size()), msg.data());
                return;
            }

            auto recipients = storage::get_recipients();
            if (!recipients) {
                ESP_LOGW(TAG, "Alert requested, but no currently stored recipients");
                return;
            }

            for (const auto& recipient : recipients.value()) {
                esp_err_t ret = telegram::send_message(msg, {recipient.data(), recipient.size()});
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send alert to %.*s: %s", recipient.size(), recipient.data(), esp_err_to_name(ret));
                } else {
                    ESP_LOGI(TAG, "Alert sent to %.*s", recipient.size(), recipient.data());
                }
            }
        }

    } // namespace

    void println(std::string_view msg, uint8_t line) {
        // Track consecutive failures
        static uint32_t consc_err_counter = 0;

        auto ret = display::println(msg, line);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send message to the display. Perharps it is disconnected?: %s", esp_err_to_name(ret));
            consc_err_counter++;
            if (consc_err_counter >= config::MAX_CONSC_ERRORS) {
                ESP_LOGE(TAG, "Too many LCD write failures (%u consecutive failures). Rebooting system", consc_err_counter);
                utils::reboot();
            }
        } else {
            consc_err_counter = 0;
        }
    }

    void on_reed_switch_break(bool is_admin_mode) {
        if (is_admin_mode) {
            // Just log in admin mode since this is not a security breach
            ESP_LOGI(TAG, "Door opened in admin mode");
        } else {
            ESP_LOGW(TAG, "An intruder has opened a door");
            ESP_LOGW(TAG, "Sending an alert to all registered recipients");
            send_alert("An unknown person has opened a door. Take safety measures accordingly.");
        }
    }

    void on_tamper_switch_break(bool is_admin_mode) {
        if (is_admin_mode) {
            // Just log in admin mode since this is not a security breach
            ESP_LOGW(TAG, "The control box is being tampered with");
        } else {
            ESP_LOGW(TAG, "An intruder has gained access to the system control box");
            ESP_LOGW(TAG, "Sending an alert to all registered recipients");
            send_alert("An unknown person has gained access to the system control box. Take safety measures accordingly.");
        }
    }

} // namespace sys
