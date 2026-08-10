#pragma once


#include "esp_err.h"

#include <string_view>


namespace display {

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

    [[nodiscard]] esp_err_t put_char(unsigned char c, uint8_t column, uint8_t line);

    [[nodiscard]] esp_err_t println(std::string_view msg, uint8_t line, bool pad_to_whitespace = true);

    [[nodiscard]] esp_err_t clear_screen();

    [[nodiscard]] esp_err_t clear_line(uint8_t line);

    [[nodiscard]] esp_err_t backlight_on(bool on = true);

    [[nodiscard]] esp_err_t bootup_screen();

    [[nodiscard]] esp_err_t shutdown_screen();

} // namespace display
