#pragma once


#include "freertos/FreeRTOS.h"

#include <array>
#include <cctype>
#include <cstdint>


namespace multitap {

    // How long a repeated press of the same key is still treated as "cycling"
    // rather than starting a brand new character.
    constexpr inline uint32_t TIMEOUT_MS = 700;

    // Character sets per digit key. Deliberately excludes most symbols beyond
    // what's on key '1' - the 4x4 keypad has no room to cover the full WPA2
    // symbol range. Covers lowercase letters, digits, space, and a handful of
    // common punctuation. Uppercase is handled separately via a case toggle,
    // not baked into the cycle sets.
    constexpr inline std::array<char, 2> KEY_0 = {' ', '0'};
    constexpr inline std::array<char, 5> KEY_1 = {'.', ',', '-', '_', '1'};
    constexpr inline std::array<char, 4> KEY_2 = {'a', 'b', 'c', '2'};
    constexpr inline std::array<char, 4> KEY_3 = {'d', 'e', 'f', '3'};
    constexpr inline std::array<char, 4> KEY_4 = {'g', 'h', 'i', '4'};
    constexpr inline std::array<char, 4> KEY_5 = {'j', 'k', 'l', '5'};
    constexpr inline std::array<char, 4> KEY_6 = {'m', 'n', 'o', '6'};
    constexpr inline std::array<char, 5> KEY_7 = {'p', 'q', 'r', 's', '7'};
    constexpr inline std::array<char, 4> KEY_8 = {'t', 'u', 'v', '8'};
    constexpr inline std::array<char, 5> KEY_9 = {'w', 'x', 'y', 'z', '9'};

    // Returns the char at `cycle_idx` (wrapped) for the given digit key, or
    // '\0' if the key isn't one of the digit keys this engine handles.
    constexpr char char_for(char key, uint8_t cycle_idx) {
        switch (key) {
            case '0':
                return KEY_0[cycle_idx % KEY_0.size()];
            case '1':
                return KEY_1[cycle_idx % KEY_1.size()];
            case '2':
                return KEY_2[cycle_idx % KEY_2.size()];
            case '3':
                return KEY_3[cycle_idx % KEY_3.size()];
            case '4':
                return KEY_4[cycle_idx % KEY_4.size()];
            case '5':
                return KEY_5[cycle_idx % KEY_5.size()];
            case '6':
                return KEY_6[cycle_idx % KEY_6.size()];
            case '7':
                return KEY_7[cycle_idx % KEY_7.size()];
            case '8':
                return KEY_8[cycle_idx % KEY_8.size()];
            case '9':
                return KEY_9[cycle_idx % KEY_9.size()];
            default:
                return '\0';
        }
    }

    // Tracks in-progress multi-tap state for a single text entry field. Owned
    // by whichever ui_state_t is doing text entry (e.g. WIFI_PW_ENTRY);
    // reset when entering/leaving that state.
    struct session_t {
        char       last_key{};
        bool       pending{}; // true if the last char is still "live"/cyclable
        bool       caps_active{};
        uint8_t    cycle_idx{};
        TickType_t last_press_tick{};

        void reset() {
            *this = {};
        }

        // Call when a digit key is pressed. `buf`/`len` is the text buffer being
        // built; this either overwrites the last (still pending) char or appends
        // a new one. Returns false if the buffer is full and the press was dropped.
        template<size_t N>
        [[nodiscard]] bool on_digit(char key, std::array<char, N>& buf, size_t& len) {
            const TickType_t now = xTaskGetTickCount();

            const bool same_key_in_window = pending && (key == last_key) && ((now - last_press_tick) < pdMS_TO_TICKS(TIMEOUT_MS));

            if (same_key_in_window) {
                cycle_idx++;
            } else {
                if (!pending && len >= N) {
                    return false; // Buffer full, nothing to overwrite, can't append
                }
                cycle_idx = 0;
                last_key  = key;
                if (!pending) {
                    len++;
                }
            }

            char c = char_for(key, cycle_idx);
            if (caps_active) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            buf[len - 1] = c;

            pending         = true;
            last_press_tick = now;
            return true;
        }

        // Call once per poll iteration when no key was pressed. Finalizes the
        // pending char once the multi-tap window has elapsed, so the next press
        // of the same key starts a new character instead of cycling this one.
        void tick_timeout() {
            if (pending && (xTaskGetTickCount() - last_press_tick) >= pdMS_TO_TICKS(TIMEOUT_MS)) {
                pending = false;
            }
        }

        // Toggles case for future characters. If a char is currently pending,
        // recases it in place too, without moving the cycle position.
        template<size_t N>
        void toggle_caps(std::array<char, N>& buf, size_t len) {
            caps_active = !caps_active;
            if (pending && len > 0) {
                char& c = buf[len - 1];

                c = caps_active ? static_cast<char>(std::toupper(c)) : static_cast<char>(std::tolower(c));
            }
        }

        // Backspace: caller removes the char from their buffer/len as usual, then
        // calls this to make sure the next digit press starts a fresh character
        // rather than resuming a cycle that no longer corresponds to anything.
        void on_backspace() {
            pending  = false;
            last_key = '\0';
        }
    };

} // namespace multitap
