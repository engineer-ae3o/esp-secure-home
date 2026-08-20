#pragma once


#include "telegram.hpp"
#include "wifi.hpp"

#include "esp_err.h"

#include <span>
#include <cstdint>
#include <optional>
#include <string_view>


namespace storage {

    constexpr inline uint32_t PASSWORD_LEN   = 8;
    constexpr inline uint32_t MAX_RECIPIENTS = 10;

    using pswd_t       = std::array<char, PASSWORD_LEN>;
    using recipient_t  = std::array<char, telegram::CHAT_ID_LEN>;
    using recipients_t = std::array<recipient_t, MAX_RECIPIENTS>;

    // WiFi credentials. Persisted so the password entry only has
    // to happen once per network change, not on every boot.
    struct wifi_creds_t {
        wifi::ssid_t ssid{};
        wifi::pswd_t password{};
    };

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    [[nodiscard]] bool check_pswd(std::string_view pswd_to_cmp);

    [[nodiscard]] esp_err_t change_pswd(std::string_view new_pswd);

    [[nodiscard]] esp_err_t add_recipient(std::string_view chat_id_to_add);

    [[nodiscard]] esp_err_t rm_recipient(std::string_view chat_id_to_rm);

    [[nodiscard]] esp_err_t set_wifi_creds(std::string_view ssid, std::string_view password);

    [[nodiscard]] std::optional<wifi_creds_t> get_wifi_creds();

    [[nodiscard]] std::optional<std::span<recipient_t>> get_recipients();

} // namespace storage
