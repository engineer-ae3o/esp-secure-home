#pragma once


#include "esp_err.h"

#include <span>
#include <cstdint>


namespace file {

    enum class name_t : uint8_t {
        PSWD = 0,
        RECIPIENTS,
        WIFI_CREDS,
    };

    void create();

    void open();

    void close();

    [[nodiscard]] bool is_first_boot();

    [[nodiscard]] esp_err_t write(name_t file, std::span<const uint8_t> buf);

    [[nodiscard]] esp_err_t read(name_t file, std::span<uint8_t> buf);

} // namespace file
