#pragma once


#include "esp_err.h"

#include <array>
#include <cstdint>
#include <expected>


namespace gsm {

    constexpr inline uint32_t MAX_SMS_LEN      = 255;
    constexpr inline uint32_t IMSI_LEN         = 32;
    constexpr inline uint32_t PHONE_NUMBER_LEN = 14;

    using imsi_t = std::array<char, IMSI_LEN>;

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    [[nodiscard]] esp_err_t get_sim_status();

    [[nodiscard]] esp_err_t send_sms(const char* sms, const char* number, bool check_sim_status = true);

    [[nodiscard]] std::expected<imsi_t, esp_err_t> get_imsi();

} // namespace gsm
