#pragma once


#include <cstdint>
#include <string_view>


namespace sys {

    void println(std::string_view msg, uint8_t line);

    void alert_on_reed_switch_break(bool is_admin_mode);

    void alert_on_tamper_switch_break(bool is_admin_mode);

} // namespace sys
