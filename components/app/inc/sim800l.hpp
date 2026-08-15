#pragma once


#include "esp_err.h"

#include <cstdint>
#include <string_view>


namespace gsm {

    constexpr inline uint32_t MAX_SMS_LEN      = 255;
    constexpr inline uint32_t PHONE_NUMBER_LEN = 14;

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    [[nodiscard]] esp_err_t get_sim_status();

    [[nodiscard]] esp_err_t send_sms(std::string_view sms, std::string_view pnumber);

} // namespace gsm
