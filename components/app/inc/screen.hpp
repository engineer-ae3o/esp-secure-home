#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"

#include <array>
#include <cstdint>
#include <utility>
#include <string_view>

namespace {

    // This file provides a bunch of helper utilities for the system and display tasks

    // The display screens
    enum class screen_t : uint8_t {
        BOOTUP_SCREEN,
        PASSWORD_REQUEST,
        TAMPER_SWITCH_BROKEN_ADMIN,
        REED_SWITCH_BROKEN_ADMIN,
        TAMPER_SWITCH_BROKEN_NO_ADMIN,
        REED_SWITCH_BROKEN_NO_ADMIN,
        COUNT,
    };

    // Since the display task handles all printing to the screen, the system task has to
    // create a request and pass that to the display task telling it the screen it is to
    // display, for how long and whether or not to return to the previous screen.
    // NOTE: duration_ms is ignored if return_to_prev is false since this implies that the
    // new screen to be displayed should last until the next display request.
    struct display_request_t {
        bool     return_to_prev{};
        screen_t screen_type{};
        uint32_t duration_ms{};
    };

    // Craft ready to use display requests
    // On bootup
    constexpr display_request_t bootup = {
        .return_to_prev = false,
        .screen_type    = screen_t::BOOTUP_SCREEN,
        .duration_ms    = portMAX_DELAY,
    };

    // Request for the password to enter admin mode
    constexpr display_request_t password_req = {
        .return_to_prev = false,
        .screen_type    = screen_t::PASSWORD_REQUEST,
        .duration_ms    = portMAX_DELAY,
    };

    // When the tamper switch is broken but in admin mode
    constexpr display_request_t tamper_switch_broken_admin = {
        .return_to_prev = true, // Normal event. Go back to the previous screen when done here
        .screen_type    = screen_t::TAMPER_SWITCH_BROKEN_ADMIN,
        .duration_ms    = 3000, // 3s should be enough
    };

    // When the tamper switch is broken but not in admin mode
    constexpr display_request_t tamper_switch_broken_no_admin = {
        .return_to_prev = false, // This is a intruder alert. Do not return to the previous screen.
        .screen_type    = screen_t::TAMPER_SWITCH_BROKEN_NO_ADMIN,
        .duration_ms    = portMAX_DELAY,
    };

    // When the reed switch is broken but in admin mode
    constexpr display_request_t reed_switch_broken_admin = {
        .return_to_prev = true, // Normal event. Go back to the previous screen when done here
        .screen_type    = screen_t::REED_SWITCH_BROKEN_ADMIN,
        .duration_ms    = 3000, // 3s should be enough
    };

    // When the reed switch is broken but not in admin mode
    constexpr display_request_t reed_switch_broken_no_admin = {
        .return_to_prev = false, // This is a intruder alert. Do not return to the previous screen.
        .screen_type    = screen_t::REED_SWITCH_BROKEN_NO_ADMIN,
        .duration_ms    = portMAX_DELAY,
    };

    // Type representing the messages to be displayed
    using screen_message_t = std::pair<std::string_view, std::string_view>;

    // LUT mapping the screen types to the message it is to display
    constexpr std::array<screen_message_t, std::to_underlying(screen_t::COUNT)> SCREEN_MAP_LUT = {{}};


} // namespace
