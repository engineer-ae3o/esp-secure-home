#pragma once

#include "esp_err.h"

namespace ss {

    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t deinit();

} // namespace ss
