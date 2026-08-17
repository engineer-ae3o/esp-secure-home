#pragma once


#include "esp_err.h"

#include <cstdint>
#include <string_view>


namespace telegram {

    constexpr inline uint32_t MAX_MSG_LEN = 512;

    // Telegram chat IDs are numeric and can be negative (groups), but for a
    // single-recipient personal chat they're positive and comfortably fit in
    // this many digits. Adjust to match whatever you size storage::pnumber_t as.
    constexpr inline uint32_t CHAT_ID_LEN = 10;

    [[nodiscard]] esp_err_t send_message(std::string_view msg, std::string_view chat_id);

} // namespace telegram
