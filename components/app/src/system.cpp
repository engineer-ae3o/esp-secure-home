#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sim800l.hpp"
#include "display.hpp"
#include "storage.hpp"
#include "system.hpp"
#include "config.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "utils.hpp"


namespace sys {

    namespace {

        constexpr const char* TAG = "System";

        constexpr const uint32_t DELAY_BETWEEN_PRINTS_MS = 2000;

        void send_sms(std::string_view sms) {
            // Get all the stored phone numbers
            auto pnumbers = storage::get_pnumbers();
            if (!pnumbers) {
                ESP_LOGW(TAG, "SMS send requested, but no curently stored phone numbers");
                return;
            }

            // Then send the SMS to all the registered phone numbers
            for (const auto& pnumber : pnumbers.value()) {
                esp_err_t ret = gsm::send_sms(sms, {pnumber.data(), pnumber.size()});
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send SMS to %.*s: %s", pnumber.size(), pnumber.data(), esp_err_to_name(ret));
                } else {
                    ESP_LOGI(TAG, "SMS sent to %.*s", pnumber.size(), pnumber.data());
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
            // Send the SMS to all phone numbers
            ESP_LOGW(TAG, "An intruder has opened a door");
            ESP_LOGW(TAG, "Sending an SMS to all registered phone numbers");
            send_sms("An unknown person has opened a door. Take safety measures accordingly.");
        }
    }

    void on_tamper_switch_break(bool is_admin_mode) {
        if (is_admin_mode) {
            // Just log in admin mode since this is not a security breach
            ESP_LOGW(TAG, "The control box is being tampered with");
        } else {
            // Send the SMS to all phone numbers
            ESP_LOGW(TAG, "An intruder has gained access to the system control box");
            ESP_LOGW(TAG, "Sending an SMS to all registered phone numbers");
            send_sms("An unknown person has gained access to the system control box. Take safety measures accordingly.");
        }
    }

} // namespace sys
