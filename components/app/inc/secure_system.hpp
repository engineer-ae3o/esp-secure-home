#pragma once


#include "sim800l.hpp"

#include "esp_err.h"

#include <cstdint>
#include <string_view>


namespace crypto {

    constexpr inline uint32_t PASSWORD_LEN = 8;
    constexpr inline uint32_t MAX_PNUMBERS = 10;

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    void give_imsi(const gsm::imsi_t& imsi);

    [[nodiscard]] esp_err_t change_password(std::string_view new_pswd);

    [[nodiscard]] esp_err_t add_pnumber(std::string_view new_pnumber);

    [[nodiscard]] bool check_if_right_pswd(std::string_view pswd_to_cmp);

} // namespace crypto
