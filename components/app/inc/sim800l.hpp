#pragma once


#include "esp_err.h"

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>


namespace gsm {

    constexpr inline uint32_t MAX_SMS_LEN      = 255;
    constexpr inline uint32_t IMSI_BUF_SIZE    = 16;
    constexpr inline uint32_t PHONE_NUMBER_LEN = 14;

    using imsi_t = std::array<char, IMSI_BUF_SIZE>;

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    [[nodiscard]] esp_err_t get_sim_status();

    [[nodiscard]] esp_err_t send_sms(std::string_view sms, std::string_view number, bool check_sim_status = true);

    [[nodiscard]] std::expected<imsi_t, esp_err_t> get_imsi();

} // namespace gsm
