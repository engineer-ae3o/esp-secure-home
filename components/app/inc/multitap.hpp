#pragma once


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <span>
#include <array>
#include <cctype>
#include <cstdint>


namespace multitap {

    // How long a repeated press of the same key is still treated
    // as "cycling" rather than starting a brand new character.
    constexpr inline uint32_t KEY_TIMEOUT_MS = 1000;

    // Character sets per digit key. Deliberately excludes most symbols beyond
    // what's on key '1'. The 4x4 keypad has no room to cover the full WPA2
    // symbol range. Covers lowercase letters, digits, space, and a handful of
    // common punctuation. Uppercase is handled separately via a case toggle,
    // not baked into the cycle sets.
    constexpr inline auto KEY_0 = std::array{' ', '0'};
    constexpr inline auto KEY_1 = std::array{'.', ',', '-', '_', '1'};
    constexpr inline auto KEY_2 = std::array{'a', 'b', 'c', '2'};
    constexpr inline auto KEY_3 = std::array{'d', 'e', 'f', '3'};
    constexpr inline auto KEY_4 = std::array{'g', 'h', 'i', '4'};
    constexpr inline auto KEY_5 = std::array{'j', 'k', 'l', '5'};
    constexpr inline auto KEY_6 = std::array{'m', 'n', 'o', '6'};
    constexpr inline auto KEY_7 = std::array{'p', 'q', 'r', 's', '7'};
    constexpr inline auto KEY_8 = std::array{'t', 'u', 'v', '8'};
    constexpr inline auto KEY_9 = std::array{'w', 'x', 'y', 'z', '9'};

    // Returns the char at m_cycle_idx (wrapped) for the given digit key, or
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

    // Tracks in progress multi tap state for a single text entry
    // field. Owned by whichever UI state is doing the text entry.
    class session_t {
    public:
        void reset() {
            *this = {};
        }

        [[nodiscard]] bool is_pending() const {
            return m_last_char_pending;
        }

        // Called when a digit key is pressed. buf/len is the text buffer being built;
        // this either overwrites the last (still m_last_char_pending) char or appends
        // a new one. Returns false if the buffer is full and the press was dropped.
        [[nodiscard]] bool on_digit(char key, std::span<char> buf, size_t& len) {
            if (m_last_char_pending && (key == m_last_key) && ((xTaskGetTickCount() - m_last_press_tick) < pdMS_TO_TICKS(KEY_TIMEOUT_MS))) {
                // If the last key press is still pending, if this key press is the same key press
                // as before and the timeout has not yet expired just increment the cycle index.
                m_cycle_idx++;
            } else {
                // But if the last key press is not pending, or a new character has been inputted,
                // or the timeout has expired, we reset the cycle counter and save the last key press
                if (!m_last_char_pending) {
                    if (len >= buf.size()) {
                        // Drop since the last character is not cyclable and there isn't space in the buffer to add new elements
                        return false;
                    } else {
                        // Increment the length since the last character is not pending. This
                        // means it can't be overwritten, so we have to append a new character.
                        len++;
                    }
                }
                // Reset the cycle index and save this key press
                m_cycle_idx = 0;
                m_last_key  = key;
            }

            // Write the character into the back of the buffer, and capitalize if necessary
            buf[len - 1] = m_caps_lock ? static_cast<char>(std::toupper(char_for(key, m_cycle_idx))) : char_for(key, m_cycle_idx);

            m_last_char_pending = true;
            m_last_press_tick   = xTaskGetTickCount();

            return true;
        }

        // Call once per poll iteration when no key is pressed. Finalizes the
        // pending char once the multi tap window has elapsed, so the next press
        // of the same key starts a new character instead of cycling this one.
        void tick_timeout() {
            if (m_last_char_pending && ((xTaskGetTickCount() - m_last_press_tick) >= pdMS_TO_TICKS(KEY_TIMEOUT_MS))) {
                m_last_char_pending = false;
            }
        }

        // Toggles case for future characters. If a char is currently pending,
        // recases it in place too, without moving the cycle position.
        void toggle_caps(std::span<char> buf, size_t len) {
            m_caps_lock = !m_caps_lock;
            if (m_last_char_pending && len > 0) {
                // Make the last character in the buffer uppercase or lowercase depending on m_caps_lock
                buf[len - 1] = m_caps_lock ? static_cast<char>(std::toupper(buf[len - 1])) : static_cast<char>(std::tolower(buf[len - 1]));
            }
        }

        // Backspace: caller removes the char from their buffer/len as usual, then
        // calls this to make sure the next digit press starts a fresh character
        // rather than resuming a cycle that no longer corresponds to anything.
        void on_backspace() {
            m_last_char_pending = false;
        }

    private:
        char m_last_key{};
        bool m_caps_lock{};
        bool m_last_char_pending{}; // true if the last char is still "live"/cyclable

        uint8_t m_cycle_idx{};

        TickType_t m_last_press_tick{};
    };

} // namespace multitap
