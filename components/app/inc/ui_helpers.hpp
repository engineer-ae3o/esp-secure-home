#pragma once


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "storage.hpp"
#include "screen.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "wifi.hpp"

#include "esp_log.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <string_view>


namespace ui {

    // Keypad UI / admin mode state machine
    //
    // Keypad layout is a standard 4x4 (digits 0-9, A-D, *, #). A-D/*/# are reserved
    // as control keys:
    //
    //   A: OK / confirm / select
    //   B: backspace (delete last typed char)
    //   C: scroll up / previous item  (in WIFI_PW_ENTRY: toggle upper/lowercase instead)
    //   D: scroll down / next item
    //   *: cancel current entry / go back one level
    //   #: logout: return to the password prompt from anywhere in admin mode
    //
    // Everything except WiFi passwords is digits-only as far as user entry goes
    // (admin password, Telegram chat IDs). WiFi passwords use multi tap text entry
    // (see multitap.hpp) since real WPA2 passwords need letters/symbols the keypad
    // has no dedicated keys for.
    enum class state_t : uint8_t {
        AWAITING_PASSWORD, // Default/locked state. User is typing the admin password.
        LOCKED_OUT,        // Too many failed attempts. Ignoring input until the lockout expires.
        ADMIN_MENU,        // Top level admin menu.
        VIEW_NUMBERS,      // Scrolling through the registered Telegram recipients (read only).
        ADD_NUMBER,        // Typing a new recipient's chat ID to register.
        RM_NUMBER,         // Scrolling through registered recipients to pick one to remove.
        CHANGE_PW_NEW,     // Typing the new password.
        CHANGE_PW_CONFIRM, // Retyping the new password to confirm it.
        WIFI_SCANNING,     // Transient. Shows "Scanning..." while wifi::scan() runs.
        WIFI_LIST,         // Scrolling through found networks to pick one.
        WIFI_PW_ENTRY,     // Multi tap password entry for the selected network.
        WIFI_CONNECTING,   // Transient. Shows "Connecting..." while wifi::connect() runs.
    };

    constexpr inline std::array<std::string_view, 5> ADMIN_MENU_ITEMS = {
        "View numbers",
        "Add number",
        "Remove number",
        "Change password",
        "WiFi setup",
    };

    // The display queue to which display requests are passed into
    inline QueueHandle_t g_display_queue{};

    QueueHandle_t init(size_t queue_length) {
        g_display_queue = xQueueCreate(queue_length, sizeof(display_request_t));
        if (g_display_queue == nullptr) {
            ESP_LOGE("UI", "Failed to create the display queue");
            utils::fatal();
        }
        return g_display_queue;
    }

    void deinit() {
        if (g_display_queue) {
            vQueueDelete(g_display_queue);
            g_display_queue = nullptr;
        }
    }

    // Sends a request to the display queue, blocking until there's room for it.
    [[__gnu__::__always_inline__]] inline void send(const display_request_t& request) {
        xQueueSend(g_display_queue, &request, portMAX_DELAY);
    }

    // Shows a transient feedback message for config::MESSAGE_DURATION_MS.
    [[__gnu__::__always_inline__]] inline void send_feedback(std::string_view line0, std::string_view line1 = {}) {
        send(make_custom_request(line0, line1, true, config::UI_MESSAGE_DURATION_MS));
    }

    // Renders the password entry screen, masking typed digits as asterisks.
    [[__gnu__::__always_inline__]] inline void render_password_prompt(std::string_view input) {
        constexpr auto mask = utils::make_filled_array<char, storage::PASSWORD_LEN>('*');
        send(make_custom_request("Enter password:", {mask.data(), input.size()}));
    }

    [[__gnu__::__always_inline__]] inline void render_menu(size_t menu_idx) {
        send(make_custom_request("-- Admin Menu --", ADMIN_MENU_ITEMS[menu_idx]));
    }

    // Renders "<label> (i/n)" on line0 and the value itself on line1. Used by both the view and remove recipient flows.
    [[__gnu__::__always_inline__]] inline void render_number(std::string_view label, size_t idx, size_t count, std::string_view value) {
        std::array<char, config::LCD_COLUMNS> header{};

        const int    written    = snprintf(header.data(), header.size(), "%.*s (%zu/%zu)", label.size(), label.data(), idx + 1, count);
        const size_t header_len = std::min(header.size() - 1, static_cast<size_t>(std::max(written, 0)));

        send(make_custom_request({header.data(), header_len}, value));
    }

    // Renders "Network (i/n)" on line0 and the (possibly truncated) SSID on line1. Truncated SSIDs get a
    // trailing '>' so it's obvious there's more to the name than what's shown on the 16 column display.
    [[__gnu__::__always_inline__]] inline void render_wifi_entry(size_t idx, size_t count, const wifi::ap_info_t& ap) {
        // Get the first line to display
        std::array<char, config::LCD_COLUMNS> header{};

        const int    written    = snprintf(header.data(), header.size(), "Network (%zu/%zu)", idx + 1, count);
        const size_t header_len = std::min(header.size() - 1, static_cast<size_t>(std::max(written, 0)));

        const auto ssid_sv   = std::string_view{ap.ssid.data()};
        auto       ssid_line = utils::make_filled_array<char, config::LCD_COLUMNS>(' ');

        // Determine if the SSID will be longer than the screen length
        const bool truncated = ssid_sv.size() > config::LCD_COLUMNS;

        // Get the amount to copy; reserve the extra byte at the end if the SSID will be truncated, so we can to place the '<'
        const size_t copy_len = std::min<size_t>(ssid_sv.size(), config::LCD_COLUMNS - (truncated ? 1 : 0));
        std::copy_n(ssid_sv.begin(), copy_len, ssid_line.begin());

        if (truncated) {
            ssid_line[config::LCD_COLUMNS - 1] = '>';
        }

        send(make_custom_request({header.data(), header_len}, {ssid_line.data(), ssid_line.size()}));
    }

} // namespace ui
