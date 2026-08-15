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
                ESP_LOGE(TAG, "No curently stored phone numbers");
            }

            // Then send the SMS to all the registered phone numbers
            for (const auto& pnumber : pnumbers.value()) {
            }
        }

    } // namespace

    void println(std::string_view msg, uint8_t line) {
        static uint32_t consc_err_counter = 0;
        if (auto ret = display::println(msg, line); ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send message to the display. Perharps it is disconnected?: %s", esp_err_to_name(ret));
            // Track consecutive failures
            consc_err_counter++;
            if (consc_err_counter >= config::MAX_CONSC_ERRORS) {
                ESP_LOGE(TAG, "Too many LCD write failures (%u consecutive failures). Rebooting system", consc_err_counter);
                utils::reboot();
            }
        } else {
            consc_err_counter = 0;
        }
    }

    void reed_switch_broken(bool is_admin_mode) {
        if (is_admin_mode) {
            // The reed switch guards the doors. It is standard behaviour for it to be broken in admin mode.
            println("  A door has   ", 0);
            println("  been opened  ", 1);
            ESP_LOGI(TAG, "Door opened in admin mode");
        } else {
            // Intruder alert if any switch has been broken while not in admin mode
            println("An intruder has", 0);
            println("opened a door(s)", 1);

            // Send the SMS to all registered phone numbers
            send_sms("An unknown person has opened one or more doors. Take safety measures accordingly.");
        }
    }

    void tamper_switch_broken(bool is_admin_mode) {
        if (is_admin_mode) {
            // The tamper switch guards the control box containing the components. Not standard behaviour for
            // a verified user to open the box. Display a warning to the user, but no need for an SMS.
            println("The control box", 0);
            println("  is not to be  ", 1);
            vTaskDelay(pdMS_TO_TICKS(DELAY_BETWEEN_PRINTS_MS));
            println(" tampered with. ", 0);
            println("  Please close. ", 1);
        } else {
            // Intruder alert if any switch has been broken while not in admin mode
            println("Intruder detecte", 0);
            println("d in control box", 1);

            // Send the SMS to all registered phone numbers
            send_sms("An unknown person has gained access to the system control box. Take safety measures accordingly.");
        }
    }

} // namespace sys
