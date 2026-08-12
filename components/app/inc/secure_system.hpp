#pragma once


#include "esp_err.h"

#include <cstdint>


namespace crypto {

    constexpr inline uint32_t PASSWORD_LEN = 8;
    constexpr inline uint32_t MAX_PNUMBERS = 10;

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

} // namespace crypto
