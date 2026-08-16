#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "utils.hpp"
#include "tasks.hpp"
#include "system.hpp"
#include "config.hpp"
#include "keypad.hpp"
#include "switch.hpp"
#include "screen.hpp"
#include "display.hpp"
#include "sim800l.hpp"
#include "storage.hpp"

#include "esp_log.h"
#include "portmacro.h"
#include "esp_system.h"
#include "esp_littlefs.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <string_view>


namespace tasks {

    namespace {

        // Start off with at the lowest privilege level
        std::atomic<bool> g_admin_mode = false;

        // The display queue to which display requests are passed into
        QueueHandle_t g_display_queue{};

        // Helpers
        void deinit_all() {
            ESP_LOGI("Info", "Deinitializing the system. Cleaning resources");

            TRY_THEN_LOG(gsm::deinit(), "Failed to deinitialize the SIM800L module");
            TRY_THEN_LOG(storage::deinit(), "Failed to deinitialize the storage interface");
            TRY_THEN_LOG(gpio_uninstall_isr_service(), "Failed to uninstall the gpio isr service");

            // We use the shutdown screen here directly. This is safe as no other thread is actively using or driving it
            TRY_THEN_LOG(display::shutdown_screen(), "Failed to display the power down screen");
            TRY_THEN_LOG(display::deinit(), "Failed to deinitialize the display");

            TRY_THEN_LOG(esp_vfs_littlefs_unregister(static_cast<const char*>(config::FILESYSTEM_PARTITION_LABEL)), "Failed to unmount filesystem");

            if (g_display_queue) {
                vQueueDelete(g_display_queue);
                g_display_queue = nullptr;
            }

            ESP_LOGI("Info", "Resources cleaned up");
        }

        void init_all() {
            using namespace config;

            constexpr const char* TAG = "Reset Reason";
            switch (auto reason = esp_reset_reason(); reason) {
                case ESP_RST_UNKNOWN:
                    ESP_LOGW(TAG, "Reset reason unknown");
                    break;
                case ESP_RST_POWERON:
                    ESP_LOGI(TAG, "Regular power on");
                    break;
                case ESP_RST_SW:
                    ESP_LOGI(TAG, "Rebooting from previous call to esp_restart()");
                    break;
                case ESP_RST_PANIC:
                    ESP_LOGW(TAG, "Reset due to panic handler rebooting the system");
                    break;
                case ESP_RST_INT_WDT:
                    ESP_LOGW(TAG, "Interrupt watchdog reset");
                    break;
                case ESP_RST_USB:
                    ESP_LOGI(TAG, "Reset from the USB controller");
                    break;
                case ESP_RST_TASK_WDT:
                    ESP_LOGW(TAG, "Task watchdog reset");
                    break;
                case ESP_RST_WDT:
                    ESP_LOGW(TAG, "Reset from any other watchdog");
                    break;
                case ESP_RST_DEEPSLEEP:
                    ESP_LOGI(TAG, "Coming from deepsleep");
                    break;
                case ESP_RST_BROWNOUT:
                    ESP_LOGE(TAG, "Reset due to brownout. Could happen again");
                    break;
                case ESP_RST_PWR_GLITCH:
                    ESP_LOGE(TAG, "Reset due to power glitch. Could happen again");
                    break;
                case ESP_RST_CPU_LOCKUP:
                    ESP_LOGE(TAG, "CPU lockup (double exception)");
                    break;
                default:
                    ESP_LOGW(TAG, "Invalid reset reason: %d", std::to_underlying(reason));
                    break;
            }

            // Mount the filesystem
            constexpr esp_vfs_littlefs_conf_t lfs_config = {
                .base_path              = static_cast<const char*>(FILESYSTEM_BASE_PATH),
                .partition_label        = static_cast<const char*>(FILESYSTEM_PARTITION_LABEL),
                .partition              = nullptr,
                .blockdev               = nullptr,
                .format_if_mount_failed = 1,
                .read_only              = 0,
                .dont_mount             = 0,
                .grow_on_mount          = 1,
            };
            TRY_WITH_FUNC_VOID(esp_vfs_littlefs_register(&lfs_config), utils::fatal());

            // Initialize the storage interface
            TRY_WITH_FUNC_VOID(storage::init(), utils::fatal());

            // Initialize the display
            TRY_WITH_FUNC_VOID(display::init(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::clear_screen(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::backlight_on(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::bootup_screen(), utils::fatal());

            // Register a shutdown handler to get called before any reboot
            TRY_WITH_FUNC_VOID(esp_register_shutdown_handler(deinit_all), utils::fatal());

            // Initialize the gpio isr service
            TRY_WITH_FUNC_VOID(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1), utils::fatal());

            // Create the display queue with a size of 16 elements to hold as many display requests as possible
            g_display_queue = xQueueCreate(32, sizeof(display_request_t));
            if (g_display_queue == nullptr) {
                ESP_LOGE(TAG, "Failed to create the display queue");
                utils::fatal();
            }

            ESP_LOGI("Init", "Done initializing all components");
        }

        // Keypad UI / admin mode state machine
        //
        // Keypad layout is a standard 4x4 (digits 0-9, A-D, *, #). Since there's no
        // way to type symbols like '+', A-D/*/# are reserved as control keys and only
        // digits are ever accepted as data (passwords and phone numbers are digits-only
        // as far as user entry is concerned):
        //
        //   A       - OK / confirm / select
        //   B       - backspace (delete last typed digit)
        //   C       - scroll up / previous item
        //   D       - scroll down / next item
        //   *       - cancel current entry / go back one level
        //   #       - logout: return to the password prompt from anywhere in admin mode
        enum class ui_state_t : uint8_t {
            AWAITING_PASSWORD, // Default/locked state. User is typing the admin password.
            LOCKED_OUT,        // Too many failed attempts. Ignoring input until the lockout expires.
            ADMIN_MENU,        // Top level admin menu.
            VIEW_NUMBERS,      // Scrolling through the registered phone numbers (read only).
            ADD_NUMBER,        // Typing a new phone number to register.
            RM_NUMBER,         // Scrolling through registered numbers to pick one to remove.
            CHANGE_PW_NEW,     // Typing the new password.
            CHANGE_PW_CONFIRM, // Re-typing the new password to confirm it.
        };

        constexpr std::array<std::string_view, 4> ADMIN_MENU_ITEMS = {
            "View numbers",
            "Add number",
            "Remove number",
            "Change password",
        };

        constexpr size_t COUNTRY_CODE_LEN = sizeof(config::COUNTRY_PNUMBER_CODE) - 1; // Leave out the null terminator
        constexpr size_t PHONE_DIGITS_LEN = gsm::PHONE_NUMBER_LEN - COUNTRY_CODE_LEN; // Digits the user actually types

        // Sends a request to the display queue, blocking until there's room for it.
        void ui_send(const display_request_t& request) {
            xQueueSend(g_display_queue, &request, portMAX_DELAY);
        }

        // Shows a transient feedback message (e.g. "Wrong password", "Number added") for
        // config::UI_MESSAGE_DURATION_MS, after which the display reverts to whatever is
        // sent next automatically re-renders it - this itself does not block the caller.
        void ui_send_feedback(std::string_view line0, std::string_view line1 = "") {
            ui_send(make_custom_request(line0, line1, true, config::UI_MESSAGE_DURATION_MS));
        }

        // Renders the password entry screen, masking typed digits as asterisks.
        void ui_render_password_prompt(std::string_view input) {
            std::array<char, storage::PASSWORD_LEN> mask{};
            mask.fill('*');
            ui_send(make_custom_request("Enter password:", {mask.data(), input.size()}));
        }

        void ui_render_menu(size_t menu_idx) {
            ui_send(make_custom_request("-- Admin Menu --", ADMIN_MENU_ITEMS[menu_idx]));
        }

        // Renders "<label> (i/n)" on line0 and the phone number itself on line1. Used by
        // both the view and remove number flows.
        void ui_render_number(std::string_view label, size_t idx, size_t count, std::string_view pnumber) {
            std::array<char, config::LCD_COLUMNS> header{};

            const int written =
                snprintf(header.data(), header.size(), "%.*s (%zu/%zu)", static_cast<int>(label.size()), label.data(), idx + 1, count);
            const size_t header_len = std::min(header.size(), static_cast<size_t>(std::max(written, 0)));
            ui_send(make_custom_request({header.data(), header_len}, pnumber));
        }

        // Renders a single request to the LCD, whether it's a canned screen_type or free-form text.
        void render_display_request(const display_request_t& request) {
            if (request.use_custom_text) {
                sys::println({request.line0.data(), request.line0.size()}, 0);
                sys::println({request.line1.data(), request.line1.size()}, 1);
            } else {
                sys::println(SCREEN_MAP_LUT[std::to_underlying(request.screen_type)].first, 0);
                sys::println(SCREEN_MAP_LUT[std::to_underlying(request.screen_type)].second, 1);
            }
        }

        [[noreturn]] void system_task(void* arg) {
            constexpr const char* TAG = "System_task";
            ESP_LOGI(TAG, "System_task started");

            // Initialize the keypad
            pad::keypad_t<false> keypad;
            TRY_WITH_FUNC_VOID(keypad.init({.row_pins = config::KEYPAD_ROW_PINS, .col_pins = config::KEYPAD_COLUMN_PINS}), utils::fatal());

            // get_event_queue() only fails if called when not initialized. Safe to extract the value directly
            auto* keypad_event_queue = keypad.get_event_queue().value();
            char  recv_key{};

            ui_state_t state = ui_state_t::AWAITING_PASSWORD;

            // Digit entry buffer, sized for the largest thing ever typed into it (a phone number).
            std::array<char, PHONE_DIGITS_LEN> input_buf{};
            size_t                             input_len = 0;

            // Holds the first entry of a new password while the user is asked to confirm it.
            std::array<char, storage::PASSWORD_LEN> pw_pending{};

            size_t   menu_idx        = 0; // Selected item in the admin menu
            size_t   list_idx        = 0; // Selected item while viewing/removing phone numbers
            uint32_t failed_attempts = 0; // Consecutive failed password attempts (brute force protection)

            TickType_t lockout_until_tick = 0;
            TickType_t last_activity_tick = xTaskGetTickCount();

            // Push the password request screen to the display queue
            ui_send(password_req);

            while (true) {
                auto ret = xQueueReceive(keypad_event_queue, &recv_key, 0);

                // Handle lockout expiry / admin idle timeout even when no key was pressed
                if (ret != pdPASS) {
                    const auto now = xTaskGetTickCount();

                    if (state == ui_state_t::LOCKED_OUT && now >= lockout_until_tick) {
                        ESP_LOGI(TAG, "Lockout period over. Accepting password attempts again");
                        failed_attempts = 0;
                        state           = ui_state_t::AWAITING_PASSWORD;
                        input_len       = 0;
                        ui_render_password_prompt({});
                    } else if (state != ui_state_t::AWAITING_PASSWORD && state != ui_state_t::LOCKED_OUT &&
                               (now - last_activity_tick) >= pdMS_TO_TICKS(config::ADMIN_IDLE_TIMEOUT_MS)) {
                        ESP_LOGI(TAG, "Admin session idle for too long. Logging out");
                        g_admin_mode = false;
                        state        = ui_state_t::AWAITING_PASSWORD;
                        input_len    = 0;
                        ui_send(password_req);
                    }

                    vTaskDelay(pdMS_TO_TICKS(config::KEYPAD_POLL_PERIOD_MS));
                    continue;
                }

                ESP_LOGI(TAG, "Key pressed: %c", recv_key);
                last_activity_tick = xTaskGetTickCount();

                switch (state) {

                    case ui_state_t::AWAITING_PASSWORD: {
                        if (recv_key >= '0' && recv_key <= '9') {
                            if (input_len < storage::PASSWORD_LEN) {
                                input_buf[input_len++] = recv_key;
                            }
                        } else if (recv_key == 'B') {
                            if (input_len > 0) {
                                input_buf[--input_len] = '\0';
                            }
                        } else if (recv_key == '*') {
                            input_len = 0;
                        } else if (recv_key == 'A') {
                            if (input_len != storage::PASSWORD_LEN) {
                                break; // Not enough digits typed yet. Ignore.
                            }

                            if (storage::check_pswd({input_buf.data(), input_len})) {
                                ESP_LOGI(TAG, "Correct password entered. Entering admin mode");
                                failed_attempts = 0;
                                input_len       = 0;
                                g_admin_mode    = true;
                                state           = ui_state_t::ADMIN_MENU;
                                menu_idx        = 0;
                                ui_render_menu(menu_idx);
                                break;
                            }

                            input_len = 0;
                            failed_attempts++;
                            ESP_LOGW(TAG, "Incorrect password entered (%u/%u attempts)", failed_attempts, config::MAX_PASSWORD_ATTEMPTS);

                            if (failed_attempts >= config::MAX_PASSWORD_ATTEMPTS) {
                                state              = ui_state_t::LOCKED_OUT;
                                lockout_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(config::LOCKOUT_DURATION_MS);
                                ESP_LOGW(TAG, "Too many failed attempts. Locking keypad for %lu ms", config::LOCKOUT_DURATION_MS);
                                ui_send(make_custom_request("Too many tries", "Keypad locked"));
                            } else {
                                ui_send_feedback("Wrong password");
                            }
                            break;
                        }

                        if (state == ui_state_t::AWAITING_PASSWORD) {
                            ui_render_password_prompt({input_buf.data(), input_len});
                        }
                        break;
                    }

                    case ui_state_t::LOCKED_OUT: {
                        // Ignore all input till the lockout period is over (handled above).
                        break;
                    }

                    case ui_state_t::ADMIN_MENU: {
                        if (recv_key == 'C') {
                            menu_idx = (menu_idx == 0) ? (ADMIN_MENU_ITEMS.size() - 1) : (menu_idx - 1);
                            ui_render_menu(menu_idx);
                        } else if (recv_key == 'D') {
                            menu_idx = (menu_idx + 1) % ADMIN_MENU_ITEMS.size();
                            ui_render_menu(menu_idx);
                        } else if (recv_key == '#') {
                            g_admin_mode = false;
                            state        = ui_state_t::AWAITING_PASSWORD;
                            input_len    = 0;
                            ui_send(password_req);
                        } else if (recv_key == 'A') {
                            switch (menu_idx) {
                                case 0: // View numbers
                                {
                                    auto pnumbers = storage::get_pnumbers();
                                    if (!pnumbers || pnumbers->empty()) {
                                        ui_send_feedback("No numbers", "registered yet");
                                        ui_render_menu(menu_idx);
                                    } else {
                                        list_idx = 0;
                                        state    = ui_state_t::VIEW_NUMBERS;
                                        ui_render_number(
                                            "Number", list_idx, pnumbers->size(), {(*pnumbers)[list_idx].data(), (*pnumbers)[list_idx].size()});
                                    }
                                    break;
                                }
                                case 1: // Add number
                                    input_len = 0;
                                    state     = ui_state_t::ADD_NUMBER;
                                    ui_send(make_custom_request("New number:", static_cast<const char*>(config::COUNTRY_PNUMBER_CODE)));
                                    break;
                                case 2: // Remove number
                                {
                                    auto pnumbers = storage::get_pnumbers();
                                    if (!pnumbers || pnumbers->empty()) {
                                        ui_send_feedback("No numbers", "registered yet");
                                        ui_render_menu(menu_idx);
                                    } else {
                                        list_idx = 0;
                                        state    = ui_state_t::RM_NUMBER;
                                        ui_render_number(
                                            "Delete?", list_idx, pnumbers->size(), {(*pnumbers)[list_idx].data(), (*pnumbers)[list_idx].size()});
                                    }
                                    break;
                                }
                                case 3: // Change password
                                    input_len = 0;
                                    state     = ui_state_t::CHANGE_PW_NEW;
                                    ui_send(make_custom_request("New password:", ""));
                                    break;
                                default:
                                    break;
                            }
                        }
                        break;
                    }

                    case ui_state_t::VIEW_NUMBERS: {
                        auto pnumbers = storage::get_pnumbers();
                        if (!pnumbers || pnumbers->empty()) {
                            state = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        }

                        if (recv_key == 'C') {
                            list_idx = (list_idx == 0) ? (pnumbers->size() - 1) : (list_idx - 1);
                        } else if (recv_key == 'D') {
                            list_idx = (list_idx + 1) % pnumbers->size();
                        } else if (recv_key == '*' || recv_key == 'B' || recv_key == 'A') {
                            state = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        } else if (recv_key == '#') {
                            g_admin_mode = false;
                            state        = ui_state_t::AWAITING_PASSWORD;
                            ui_send(password_req);
                            break;
                        }

                        ui_render_number("Number", list_idx, pnumbers->size(), {(*pnumbers)[list_idx].data(), (*pnumbers)[list_idx].size()});
                        break;
                    }

                    case ui_state_t::ADD_NUMBER: {
                        if (recv_key >= '0' && recv_key <= '9') {
                            if (input_len < PHONE_DIGITS_LEN) {
                                input_buf[input_len++] = recv_key;
                            }
                        } else if (recv_key == 'B') {
                            if (input_len > 0) {
                                input_buf[--input_len] = '\0';
                            }
                        } else if (recv_key == '*') {
                            state = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        } else if (recv_key == '#') {
                            g_admin_mode = false;
                            state        = ui_state_t::AWAITING_PASSWORD;
                            ui_send(password_req);
                            break;
                        } else if (recv_key == 'A') {
                            if (input_len != PHONE_DIGITS_LEN) {
                                break; // Not enough digits yet. Ignore.
                            }

                            std::array<char, gsm::PHONE_NUMBER_LEN> full_number{};
                            std::copy_n(static_cast<const char*>(config::COUNTRY_PNUMBER_CODE), COUNTRY_CODE_LEN, full_number.begin());
                            std::copy_n(input_buf.begin(), input_len, full_number.begin() + COUNTRY_CODE_LEN);

                            if (auto err = storage::add_pnumber({full_number.data(), full_number.size()}); err == ESP_OK) {
                                ESP_LOGI(TAG, "Phone number added");
                                ui_send_feedback("Number added");
                            } else {
                                ESP_LOGW(TAG, "Failed to add phone number: %s", esp_err_to_name(err));
                                ui_send_feedback("Add failed", (err == ESP_ERR_INVALID_STATE) ? "Already exists" : "Storage full?");
                            }

                            input_len = 0;
                            state     = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        }

                        if (state == ui_state_t::ADD_NUMBER) {
                            std::array<char, gsm::PHONE_NUMBER_LEN> preview{};
                            std::copy_n(static_cast<const char*>(config::COUNTRY_PNUMBER_CODE), COUNTRY_CODE_LEN, preview.begin());
                            std::copy_n(input_buf.begin(), input_len, preview.begin() + COUNTRY_CODE_LEN);
                            ui_send(make_custom_request("New number:", {preview.data(), COUNTRY_CODE_LEN + input_len}));
                        }
                        break;
                    }

                    case ui_state_t::RM_NUMBER: {
                        auto pnumbers = storage::get_pnumbers();
                        if (!pnumbers || pnumbers->empty()) {
                            state = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        }

                        if (recv_key == 'C') {
                            list_idx = (list_idx == 0) ? (pnumbers->size() - 1) : (list_idx - 1);
                        } else if (recv_key == 'D') {
                            list_idx = (list_idx + 1) % pnumbers->size();
                        } else if (recv_key == '*' || recv_key == 'B') {
                            state = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        } else if (recv_key == '#') {
                            g_admin_mode = false;
                            state        = ui_state_t::AWAITING_PASSWORD;
                            ui_send(password_req);
                            break;
                        } else if (recv_key == 'A') {
                            const auto& target = (*pnumbers)[list_idx];
                            if (auto err = storage::rm_pnumber({target.data(), target.size()}); err == ESP_OK) {
                                ESP_LOGI(TAG, "Phone number removed");
                                ui_send_feedback("Number removed");
                            } else {
                                ESP_LOGW(TAG, "Failed to remove phone number: %s", esp_err_to_name(err));
                                ui_send_feedback("Removal failed");
                            }
                            state = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        }

                        if (state == ui_state_t::RM_NUMBER) {
                            ui_render_number("Delete?", list_idx, pnumbers->size(), {(*pnumbers)[list_idx].data(), (*pnumbers)[list_idx].size()});
                        }
                        break;
                    }

                    case ui_state_t::CHANGE_PW_NEW: {
                        if (recv_key >= '0' && recv_key <= '9') {
                            if (input_len < storage::PASSWORD_LEN) {
                                input_buf[input_len++] = recv_key;
                            }
                        } else if (recv_key == 'B') {
                            if (input_len > 0) {
                                input_buf[--input_len] = '\0';
                            }
                        } else if (recv_key == '*') {
                            input_len = 0;
                            state     = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        } else if (recv_key == '#') {
                            g_admin_mode = false;
                            state        = ui_state_t::AWAITING_PASSWORD;
                            ui_send(password_req);
                            break;
                        } else if (recv_key == 'A') {
                            if (input_len != storage::PASSWORD_LEN) {
                                break; // Not enough digits yet. Ignore.
                            }
                            std::copy_n(input_buf.begin(), storage::PASSWORD_LEN, pw_pending.begin());
                            input_len = 0;
                            state     = ui_state_t::CHANGE_PW_CONFIRM;
                            ui_send(make_custom_request("Confirm pswd:", ""));
                            break;
                        }

                        if (state == ui_state_t::CHANGE_PW_NEW) {
                            std::array<char, storage::PASSWORD_LEN> mask{};
                            mask.fill('*');
                            ui_send(make_custom_request("New password:", {mask.data(), input_len}));
                        }
                        break;
                    }

                    case ui_state_t::CHANGE_PW_CONFIRM: {
                        if (recv_key >= '0' && recv_key <= '9') {
                            if (input_len < storage::PASSWORD_LEN) {
                                input_buf[input_len++] = recv_key;
                            }
                        } else if (recv_key == 'B') {
                            if (input_len > 0) {
                                input_buf[--input_len] = '\0';
                            }
                        } else if (recv_key == '*') {
                            input_len = 0;
                            state     = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        } else if (recv_key == '#') {
                            g_admin_mode = false;
                            state        = ui_state_t::AWAITING_PASSWORD;
                            ui_send(password_req);
                            break;
                        } else if (recv_key == 'A') {
                            if (input_len != storage::PASSWORD_LEN) {
                                break; // Not enough digits yet. Ignore.
                            }

                            const bool matches = std::equal(pw_pending.begin(), pw_pending.end(), input_buf.begin());
                            input_len          = 0;

                            if (!matches) {
                                ESP_LOGW(TAG, "Password confirmation mismatch");
                                ui_send_feedback("Mismatch", "Try again");
                                state = ui_state_t::CHANGE_PW_NEW;
                                ui_send(make_custom_request("New password:", ""));
                                break;
                            }

                            if (auto err = storage::change_pswd({pw_pending.data(), pw_pending.size()}); err == ESP_OK) {
                                ESP_LOGI(TAG, "Password changed");
                                ui_send_feedback("Password", "changed");
                            } else {
                                ESP_LOGW(TAG, "Failed to change password: %s", esp_err_to_name(err));
                                ui_send_feedback("Change failed");
                            }

                            pw_pending.fill('\0');
                            state = ui_state_t::ADMIN_MENU;
                            ui_render_menu(menu_idx);
                            break;
                        }

                        if (state == ui_state_t::CHANGE_PW_CONFIRM) {
                            std::array<char, storage::PASSWORD_LEN> mask{};
                            mask.fill('*');
                            ui_send(make_custom_request("Confirm pswd:", {mask.data(), input_len}));
                        }
                        break;
                    }
                }
            }
        }

        [[noreturn]] void display_task(void* arg) {
            constexpr const char* TAG = "Display_task";
            ESP_LOGI(TAG, "Display_task started");

            // The last screen that was meant to persist (i.e. not itself a transient
            // "return_to_prev" alert). This is what a transient alert reverts back to
            // once its hold duration elapses - it can be a canned screen or the system
            // task's current interactive UI state (menu, in-progress digit entry, etc.).
            display_request_t persistent_request = password_req;
            display_request_t request            = {};

            while (true) {
                // Block till a request is received
                xQueueReceive(g_display_queue, &request, portMAX_DELAY);

                render_display_request(request);

                if (request.return_to_prev) {
                    // Hold the current screen for the requested amount of time, then
                    // revert to whatever was persistently displayed before it.
                    vTaskDelay(pdMS_TO_TICKS(request.duration_ms));
                    render_display_request(persistent_request);
                } else {
                    // This request now becomes the new baseline to revert back to.
                    persistent_request = request;
                }

                // If return_to_prev is false, there is no reason to block here.
                // Instead the screen will be held till the next display request.
            }
        }

        [[noreturn]] void switch_task(void* arg) {
            constexpr const char* TAG = "Switch_task";
            ESP_LOGI(TAG, "Switch_task started");

            // Initialize the reed and tamper switches.
            nc::switch_t<nc::type_t::REED, false> reed;
            TRY_WITH_FUNC_VOID(reed.init({.pin = config::REED_SWITCH_PIN, .recv_task_handle = xTaskGetCurrentTaskHandle()}), utils::fatal());

            nc::switch_t<nc::type_t::TAMPER, false> tamper;
            TRY_WITH_FUNC_VOID(tamper.init({.pin = config::TAMPER_SWITCH_PIN, .recv_task_handle = xTaskGetCurrentTaskHandle()}), utils::fatal());

            while (true) {
                uint32_t notification{};

                bool reed_switch_broken   = false;
                bool tamper_switch_broken = false;

                // Block till a switch break
                xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);

                if (notification & std::to_underlying(nc::type_t::REED)) {
                    ESP_LOGI(TAG, "Reed switch broken");
                    reed_switch_broken = true;
                }

                if (notification & std::to_underlying(nc::type_t::TAMPER)) {
                    ESP_LOGI(TAG, "Tamper switch broken");
                    tamper_switch_broken = true;
                }

                if (reed_switch_broken) {
                    const auto& reed_broken_request = g_admin_mode ? reed_switch_broken_admin : reed_switch_broken_no_admin;
                    xQueueSend(g_display_queue, &reed_broken_request, portMAX_DELAY);
                    sys::on_reed_switch_break(g_admin_mode);
                }

                if (tamper_switch_broken) {
                    const auto& tamper_broken_request = g_admin_mode ? tamper_switch_broken_admin : tamper_switch_broken_no_admin;
                    xQueueSend(g_display_queue, &tamper_broken_request, portMAX_DELAY);
                    sys::on_tamper_switch_break(g_admin_mode);
                }
            }
        }

    } // namespace

    void run() {
        // Initialize all used resources
        init_all();

        constexpr const char* TAG = "Tasks";

        // Create the tasks
        BaseType_t ret = xTaskCreate(system_task, "system_task", config::SYSTEM_TASK_STACK, nullptr, config::SYSTEM_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the system task");
            utils::fatal();
        }

        ret = xTaskCreate(display_task, "display_task", config::DISPlAY_TASK_STACK, nullptr, config::DISPlAY_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the display task");
            utils::fatal();
        }

        ret = xTaskCreate(switch_task, "switch_task", config::SWITCH_TASK_STACK, nullptr, config::SWITCH_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the switch task");
            utils::fatal();
        }

        ret = xTaskCreate(
            [](void* arg) {
                constexpr const char* TAG = "GSM Init Task";

                ESP_LOGI(TAG, "Initializing the SIM800L");
                constexpr uint32_t NUM_OF_GSM_INIT_RETRIES = 10;

                // Initialize the SIM800L module. The SIM800L requires sometime after power on for it to fully stablize.
                // There's still a chance for the initialization to fail. Retry before declaring an error and rebooting.
                for (size_t i = 0; i < NUM_OF_GSM_INIT_RETRIES; i++) {
                    if (gsm::init() == ESP_OK) {
                        ESP_LOGI(TAG, "Initialized the SIM800L on iteration %zu", i);
                        break;
                    } else {
                        ESP_LOGW(TAG, "Failed to initialize the SIM800L on iteration %zu", i);
                    }
                    if (i == (NUM_OF_GSM_INIT_RETRIES - 1)) {
                        ESP_LOGE(TAG, "Failed to initialize the SIM800L after %zu iterations. Rebooting.", i);
                        utils::reboot();
                    }
                }

                TRY_THEN_LOG(gsm::get_sim_status(), "SIM card not ready"); // Not a fatal error. We'll retry later on.
                ESP_LOGI(TAG, "Done initializing the SIM800L and the SIM card");

                // Delete this thread immediately after it's done with the initialization
                vTaskDelete(nullptr);
            },
            "gsm_init_task",
            config::GSM_INIT_TASK_STACK,
            nullptr,
            config::GSM_INIT_TASK_PRIORITY,
            nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the gsm init task");
            utils::fatal();
        }
    }

} // namespace tasks
