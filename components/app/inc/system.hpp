#pragma once


#include <cstdint>
#include <string_view>


namespace sys {

    void println(std::string_view msg, uint8_t line);

    void reed_switch_broken(bool is_admin_mode);

    void tamper_switch_broken(bool is_admin_mode);

} // namespace sys
