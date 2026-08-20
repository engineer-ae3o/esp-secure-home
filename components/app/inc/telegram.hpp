#pragma once


#include "esp_err.h"

#include <cstdint>
#include <string_view>


namespace telegram {

    constexpr inline uint32_t MAX_MSG_LEN = 512;
    constexpr inline uint32_t CHAT_ID_LEN = 10;

    [[nodiscard]] esp_err_t send_message(std::string_view msg, std::string_view chat_id);

} // namespace telegram
