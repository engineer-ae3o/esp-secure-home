#pragma once


#include "sim800l.hpp"

#include "esp_err.h"

#include <span>
#include <cstdint>
#include <optional>
#include <string_view>


namespace storage {

    constexpr inline uint32_t PASSWORD_LEN = 8;
    constexpr inline uint32_t MAX_PNUMBERS = 10;

    using pswd_t     = std::array<char, PASSWORD_LEN>;
    using pnumber_t  = std::array<char, gsm::PHONE_NUMBER_LEN>;
    using pnumbers_t = std::array<pnumber_t, MAX_PNUMBERS>;

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    [[nodiscard]] bool check_pswd(std::string_view pswd_to_cmp);

    [[nodiscard]] esp_err_t change_pswd(std::string_view new_pswd);

    [[nodiscard]] esp_err_t add_pnumber(std::string_view pnumber_to_add);

    [[nodiscard]] esp_err_t rm_pnumber(std::string_view pnumber_to_rm);

    [[nodiscard]] std::optional<std::span<pnumber_t>> get_pnumbers();

} // namespace storage
