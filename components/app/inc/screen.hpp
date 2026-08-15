#pragma once


#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <string_view>


namespace {

    // This file provides a bunch of helper utilities for the system and display tasks

    // The display screens
    enum class screen_t : uint8_t {
        PASSWORD_REQUEST,
        TAMPER_SWITCH_BROKEN_ADMIN,
        REED_SWITCH_BROKEN_ADMIN,
        TAMPER_SWITCH_BROKEN_NO_ADMIN,
        REED_SWITCH_BROKEN_NO_ADMIN,
        ADMIN_MODE,
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
        uint16_t duration_ms{};
    };

    // Updated by the display task when it serves any request. Read by the system
    // task so it knows what to do with key presses at that point in time.
    std::atomic<screen_t> g_current_screen{};
    static_assert(g_current_screen.is_always_lock_free); // NOLINT(readability-static-accessed-through-instance)

    // Craft ready to use display requests
    // Request for the password to enter admin mode
    constexpr display_request_t password_req = {
        .return_to_prev = false,
        .screen_type    = screen_t::PASSWORD_REQUEST,
        .duration_ms    = 0,
    };

    // When the tamper switch is broken but in admin mode
    constexpr display_request_t tamper_switch_broken_admin = {
        .return_to_prev = true, // Normal event. Go back to the previous screen when done here
        .screen_type    = screen_t::TAMPER_SWITCH_BROKEN_ADMIN,
        .duration_ms    = 4000, // 4s should be enough
    };

    // When the tamper switch is broken but not in admin mode
    constexpr display_request_t tamper_switch_broken_no_admin = {
        .return_to_prev = false, // This is a intruder alert. Do not return to the previous screen.
        .screen_type    = screen_t::TAMPER_SWITCH_BROKEN_NO_ADMIN,
        .duration_ms    = 0,
    };

    // When the reed switch is broken but in admin mode
    constexpr display_request_t reed_switch_broken_admin = {
        .return_to_prev = true, // Normal event. Go back to the previous screen when done here
        .screen_type    = screen_t::REED_SWITCH_BROKEN_ADMIN,
        .duration_ms    = 2000, // 2s should be enough
    };

    // When the reed switch is broken but not in admin mode
    constexpr display_request_t reed_switch_broken_no_admin = {
        .return_to_prev = false, // This is a intruder alert. Do not return to the previous screen.
        .screen_type    = screen_t::REED_SWITCH_BROKEN_NO_ADMIN,
        .duration_ms    = 0,
    };

    // When the reed switch is broken but not in admin mode
    constexpr display_request_t admin_screen = {
        .return_to_prev = false, // This should be held till the user decides to change the screen, or a switch was broken
        .screen_type    = screen_t::ADMIN_MODE,
        .duration_ms    = 0,
    };

    // Type representing the messages to be displayed
    using screen_message_t = std::pair<std::string_view, std::string_view>;

    // LUT mapping the screen types to the message it is to display
    constexpr std::array<screen_message_t, std::to_underlying(screen_t::COUNT)> SCREEN_MAP_LUT = {{
        [std::to_underlying(screen_t::PASSWORD_REQUEST)] =
            {
                // The default screen after boot. User can enter the password to enter admin mode.
                "Enter password:",
                "",
            },
        [std::to_underlying(screen_t::TAMPER_SWITCH_BROKEN_ADMIN)] =
            {
                // The tamper switch guards the control box containing the components. Not standard behaviour for
                // a verified user to open the box. Display a warning to the user, but no need for an SMS.
                "Please leave the",
                "  control box  ",
            },
        [std::to_underlying(screen_t::REED_SWITCH_BROKEN_ADMIN)] =
            {
                // The reed switch guards the doors. It is standard behaviour for it to be broken in admin mode.
                "  A door has   ",
                "  been opened  ",
            },
        [std::to_underlying(screen_t::TAMPER_SWITCH_BROKEN_NO_ADMIN)] =
            {
                // Intruder alert if any switch has been broken while not in admin mode
                "Intruder detecte",
                "d in control box",
            },
        [std::to_underlying(screen_t::REED_SWITCH_BROKEN_NO_ADMIN)] =
            {
                // Intruder alert if any switch has been broken while not in admin mode
                "An intruder has",
                "opened a door(s)",
            },
        [std::to_underlying(screen_t::ADMIN_MODE)] =
            {
                // Admin mode. Show options to view all phone numbers and add or remove a phone number
                "",
                "",
            },
    }};

} // namespace
