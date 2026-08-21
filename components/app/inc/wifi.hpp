#pragma once


#include "esp_err.h"
#include "esp_wifi_types_generic.h"

#include <array>
#include <cstdint>
#include <string_view>


namespace wifi {

    constexpr inline uint32_t MAX_SCAN_RESULTS = 12; // Enough for most environments; excess APs are just not shown
    constexpr inline uint32_t SSID_LEN         = 32;
    constexpr inline uint32_t PASSWORD_MAX_LEN = 63; // WPA2 PSK max password length

    using ssid_t = std::array<char, SSID_LEN + 1>;
    using pswd_t = std::array<char, PASSWORD_MAX_LEN + 1>;

    struct ap_info_t {
        ssid_t           ssid{};
        int8_t           rssi{};
        wifi_auth_mode_t authmode{};
    };

    // Initializes the WiFi radio, netif and the event loop.
    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    [[nodiscard]] bool is_connected();

    // Blocking scan. Populates out with up to MAX_SCAN_RESULTS APs and
    // sets count to how many were found. Takes a couple of seconds.
    [[nodiscard]] esp_err_t scan(std::array<ap_info_t, MAX_SCAN_RESULTS>& out, size_t& count);

    // Connects to the given SSID. Pass an empty password for open networks.
    // Blocks until connected or a timeout is hit. On success, these credentials
    // become the ones auto reconnect uses on future disconnects.
    [[nodiscard]] esp_err_t connect(std::string_view ssid, std::string_view password);

} // namespace wifi
