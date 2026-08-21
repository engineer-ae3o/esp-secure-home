#pragma once


#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"

#include "config.hpp"
#include "utils.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <string_view>


namespace {

    // This file provides a bunch of helper utilities for the system and display tasks

    // The canned (static and pre authored) display screens. Anything whose text is known
    // up front lives here. Anything data dependent (menus, typed digits, phone number
    // lists, etc.) is sent as free form text instead see make_custom_request() below.
    enum class screen_t : uint8_t {
        PASSWORD_REQUEST,
        TAMPER_SWITCH_BROKEN_ADMIN,
        REED_SWITCH_BROKEN_ADMIN,
        TAMPER_SWITCH_BROKEN_NO_ADMIN,
        REED_SWITCH_BROKEN_NO_ADMIN,
        COUNT,
    };

    // Since the display task handles all printing to the screen, the system task has to
    // create a request and pass that to the display task telling it what it is to
    // display, for how long and whether or not to return to the previous screen.
    //
    // NOTE: duration_ms is ignored if return_to_prev is false since this implies that the
    // new screen to be displayed should last until the next display request.
    //
    // If use_custom_text is set, line0/line1 are shown verbatim instead of looking
    // screen_type up in SCREEN_MAP_LUT. This is how the interactive (keypad driven) UI
    // menus, typed digits, phone number lists, feedback messages gets rendered, while
    // still going through the same single queue/single writer path as the canned alerts.
    struct display_request_t {
        bool return_to_prev{};
        bool use_custom_text{};

        screen_t screen_type{};
        uint16_t duration_ms{};

        std::array<char, config::LCD_COLUMNS> line0{};
        std::array<char, config::LCD_COLUMNS> line1{};
    };

    // Builds a display line of exactly LCD_COLUMNS characters.
    // It copies in as much of text as fits and space pads the rest.
    constexpr std::array<char, config::LCD_COLUMNS> make_line(std::string_view text) {
        std::array<char, config::LCD_COLUMNS> line = utils::make_filled_array<char, config::LCD_COLUMNS>(' ');
        const size_t                          len  = std::min<size_t>(text.size(), config::LCD_COLUMNS);
        for (size_t i = 0; i < len; i++) {
            line[i] = text[i];
        }
        return line;
    }

    // Builds a display request carrying free form text rather than a canned screen_type.
    constexpr display_request_t
    make_custom_request(std::string_view line0, std::string_view line1, bool return_to_prev = false, uint16_t duration_ms = 0) {
        return {
            .return_to_prev  = return_to_prev,
            .use_custom_text = true,
            .screen_type     = screen_t::PASSWORD_REQUEST, // This is unused as use_custom_text takes priority
            .duration_ms     = duration_ms,
            .line0           = make_line(line0),
            .line1           = make_line(line1),
        };
    }

    // Craft ready to use display requests
    // Request for the password to enter admin mode
    constexpr inline display_request_t password_req = {
        .return_to_prev = false,
        .screen_type    = screen_t::PASSWORD_REQUEST,
        .duration_ms    = 0,
    };

    // When the tamper switch is broken but in admin mode
    constexpr inline display_request_t tamper_switch_broken_admin = {
        .return_to_prev = true, // Normal event. Go back to the previous screen when done here
        .screen_type    = screen_t::TAMPER_SWITCH_BROKEN_ADMIN,
        .duration_ms    = 4000, // 4s should be enough
    };

    // When the tamper switch is broken but not in admin mode
    constexpr inline display_request_t tamper_switch_broken_no_admin = {
        .return_to_prev = false, // This is a intruder alert. Do not return to the previous screen.
        .screen_type    = screen_t::TAMPER_SWITCH_BROKEN_NO_ADMIN,
        .duration_ms    = 0,
    };

    // When the reed switch is broken but in admin mode
    constexpr inline display_request_t reed_switch_broken_admin = {
        .return_to_prev = true, // Normal event. Go back to the previous screen when done here
        .screen_type    = screen_t::REED_SWITCH_BROKEN_ADMIN,
        .duration_ms    = 2000, // 2s should be enough
    };

    // When the reed switch is broken but not in admin mode
    constexpr inline display_request_t reed_switch_broken_no_admin = {
        .return_to_prev = false, // This is a intruder alert. Do not return to the previous screen.
        .screen_type    = screen_t::REED_SWITCH_BROKEN_NO_ADMIN,
        .duration_ms    = 0,
    };

    // Type representing the messages to be displayed
    using screen_message_t = std::pair<std::string_view, std::string_view>;

    // LUT mapping the screen types to the message it is to display
    constexpr inline std::array<screen_message_t, std::to_underlying(screen_t::COUNT)> SCREEN_MAP_LUT = {{
        [std::to_underlying(screen_t::PASSWORD_REQUEST)] =
            {
                // The default screen after boot. User can enter the password to enter admin mode.
                "Enter password: ",
                "",
            },
        [std::to_underlying(screen_t::TAMPER_SWITCH_BROKEN_ADMIN)] =
            {
                // The tamper switch guards the control box containing the components. Not standard behaviour for
                // a verified user to open the box. Display a warning to the user, but no need for an SMS.
                "Please leave the",
                "  control box.  ",
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
                "  Intruder in   ",
                "the control box.",
            },
        [std::to_underlying(screen_t::REED_SWITCH_BROKEN_NO_ADMIN)] =
            {
                // Intruder alert if any switch has been broken while not in admin mode
                "An intruder has ",
                " opened a door. ",
            },
    }};

} // namespace
