#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "wifi.hpp"
#include "utils.hpp"
#include "tasks.hpp"
#include "system.hpp"
#include "config.hpp"
#include "keypad.hpp"
#include "switch.hpp"
#include "screen.hpp"
#include "display.hpp"
#include "storage.hpp"
#include "telegram.hpp"
#include "multitap.hpp"
#include "ui_helpers.hpp"

#include "esp_log.h"
#include "portmacro.h"
#include "esp_system.h"
#include "esp_littlefs.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <string_view>


namespace tasks {

    namespace {

        // Start off at the lowest privilege level
        std::atomic<bool> g_admin_mode = false;

        // The display queue to which display requests are passed into
        QueueHandle_t g_display_queue{};

        // Helpers
        void deinit_all() {
            ESP_LOGI("Info", "Deinitializing the system. Cleaning resources");

            TRY_THEN_LOG(wifi::deinit(), "Failed to deinitialize WiFi");
            TRY_THEN_LOG(storage::deinit(), "Failed to deinitialize the storage interface");
            TRY_THEN_LOG(gpio_uninstall_isr_service(), "Failed to uninstall the gpio isr service");

            // We use the shutdown screen here directly. This is safe as no other thread is actively using or driving it
            TRY_THEN_LOG(display::shutdown_screen(), "Failed to display the power down screen");
            TRY_THEN_LOG(display::deinit(), "Failed to deinitialize the display");
            ui::deinit();

            TRY_THEN_LOG(esp_vfs_littlefs_unregister(static_cast<const char*>(config::FILESYSTEM_PARTITION_LABEL)), "Failed to unmount filesystem");

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

            // Bring up the WiFi radio.
            TRY_WITH_FUNC_VOID(wifi::init(), utils::fatal());

            // Initialize the gpio isr service
            TRY_WITH_FUNC_VOID(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1), utils::fatal());

            // Create the display queue with a size of 32 to hold as many display requests as possible
            g_display_queue = ui::init(32);

            // Initialize the display
            TRY_WITH_FUNC_VOID(display::init(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::clear_screen(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::backlight_on(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::bootup_screen(), utils::fatal());

            // Register a shutdown handler to get called before any reboot
            TRY_WITH_FUNC_VOID(esp_register_shutdown_handler(deinit_all), utils::fatal());

            ESP_LOGI("Init", "Done initializing all components");
        }

        namespace tasks {

            [[noreturn]] void system(void* arg) {
                constexpr const char* TAG = "System_task";
                ESP_LOGI(TAG, "System_task started");

                // Initialize the keypad
                pad::keypad_t<false> keypad;
                TRY_WITH_FUNC_VOID(keypad.init({.row_pins = config::KEYPAD_ROW_PINS, .col_pins = config::KEYPAD_COLUMN_PINS}), utils::fatal());

                // get_event_queue() only fails if called when not initialized. Safe to extract the value directly
                auto* keypad_event_queue = keypad.get_event_queue().value();
                char  recv_key{};

                ui::state_t state = ui::state_t::AWAITING_PASSWORD;

                // Digit entry buffer, sized for the largest thing ever typed into it (a chat ID).
                std::array<char, telegram::CHAT_ID_LEN> input_buf{};
                size_t                                  input_len = 0;

                // Holds the first entry of a new password while the user is asked to confirm it.
                std::array<char, storage::PASSWORD_LEN> pw_pending{};

                size_t   menu_idx        = 0; // Selected item in the admin menu
                size_t   list_idx        = 0; // Selected item while viewing/removing recipients
                uint32_t failed_attempts = 0; // Consecutive failed password attempts (brute force protection)

                TickType_t lockout_until_tick = 0;
                TickType_t last_activity_tick = xTaskGetTickCount();

                // WiFi setup state
                std::array<wifi::ap_info_t, wifi::MAX_SCAN_RESULTS> wifi_scan_results{};
                size_t                                              wifi_scan_count = 0;
                size_t                                              wifi_list_idx   = 0;

                std::array<char, wifi::SSID_LEN + 1>         wifi_selected_ssid{};
                std::array<char, wifi::PASSWORD_MAX_LEN + 1> wifi_pw_buf{};
                size_t                                       wifi_pw_len = 0;

                multitap::session_t mt_session{};

                // Push the password request screen to the display queue
                ui::send(password_req);

                while (true) {
                    auto ret = xQueueReceive(keypad_event_queue, &recv_key, 0);

                    // Handle lockout expiry / admin idle timeout / multi-tap timeout even when no key was pressed
                    if (ret != pdPASS) {
                        const auto now = xTaskGetTickCount();

                        if (state == ui::state_t::WIFI_PW_ENTRY) {
                            mt_session.tick_timeout();
                        }

                        if (state == ui::state_t::LOCKED_OUT && now >= lockout_until_tick) {
                            ESP_LOGI(TAG, "Lockout period over. Accepting password attempts again");
                            failed_attempts = 0;
                            state           = ui::state_t::AWAITING_PASSWORD;
                            input_len       = 0;
                            ui::render_password_prompt({});
                        } else if (state != ui::state_t::AWAITING_PASSWORD && state != ui::state_t::LOCKED_OUT &&
                                   (now - last_activity_tick) >= pdMS_TO_TICKS(config::ADMIN_IDLE_TIMEOUT_MS)) {
                            ESP_LOGI(TAG, "Admin session idle for too long. Logging out");
                            g_admin_mode = false;
                            state        = ui::state_t::AWAITING_PASSWORD;
                            input_len    = 0;
                            ui::send(password_req);
                        }

                        vTaskDelay(pdMS_TO_TICKS(config::KEYPAD_POLL_PERIOD_MS));
                        continue;
                    }

                    ESP_LOGI(TAG, "Key pressed: %c", recv_key);
                    last_activity_tick = xTaskGetTickCount();

                    switch (state) {
                        case ui::state_t::AWAITING_PASSWORD: {
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
                                    state           = ui::state_t::ADMIN_MENU;
                                    menu_idx        = 0;
                                    ui::render_menu(menu_idx);
                                    break;
                                }

                                input_len = 0;
                                failed_attempts++;
                                ESP_LOGW(TAG, "Incorrect password entered (%u/%u attempts)", failed_attempts, config::MAX_PASSWORD_ATTEMPTS);

                                if (failed_attempts >= config::MAX_PASSWORD_ATTEMPTS) {
                                    state              = ui::state_t::LOCKED_OUT;
                                    lockout_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(config::LOCKOUT_DURATION_MS);
                                    ESP_LOGW(TAG, "Too many failed attempts. Locking keypad for %lu ms", config::LOCKOUT_DURATION_MS);
                                    ui::send(make_custom_request("Too many tries", "Keypad locked"));
                                } else {
                                    ui::send_feedback("Wrong password");
                                }
                                break;
                            }

                            if (state == ui::state_t::AWAITING_PASSWORD) {
                                ui::render_password_prompt({input_buf.data(), input_len});
                            }
                            break;
                        }

                        case ui::state_t::LOCKED_OUT: {
                            // Ignore all input till the lockout period is over (handled above).
                            break;
                        }

                        case ui::state_t::ADMIN_MENU: {
                            if (recv_key == 'C') {
                                menu_idx = (menu_idx == 0) ? (ui::ADMIN_MENU_ITEMS.size() - 1) : (menu_idx - 1);
                                ui::render_menu(menu_idx);
                            } else if (recv_key == 'D') {
                                menu_idx = (menu_idx + 1) % ui::ADMIN_MENU_ITEMS.size();
                                ui::render_menu(menu_idx);
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                input_len    = 0;
                                ui::send(password_req);
                            } else if (recv_key == 'A') {
                                switch (menu_idx) {
                                    case 0: // View numbers
                                    {
                                        auto recipients = storage::get_recipients();
                                        if (!recipients || recipients->empty()) {
                                            ui::send_feedback("No numbers", "registered yet");
                                            ui::render_menu(menu_idx);
                                        } else {
                                            list_idx = 0;
                                            state    = ui::state_t::VIEW_NUMBERS;
                                            ui::render_number("Number",
                                                              list_idx,
                                                              recipients->size(),
                                                              {(*recipients)[list_idx].data(), (*recipients)[list_idx].size()});
                                        }
                                        break;
                                    }
                                    case 1: // Add number
                                        input_len = 0;
                                        state     = ui::state_t::ADD_NUMBER;
                                        ui::send(make_custom_request("New chat ID:", ""));
                                        break;
                                    case 2: // Remove number
                                    {
                                        auto recipients = storage::get_recipients();
                                        if (!recipients || recipients->empty()) {
                                            ui::send_feedback("No numbers", "registered yet");
                                            ui::render_menu(menu_idx);
                                        } else {
                                            list_idx = 0;
                                            state    = ui::state_t::RM_NUMBER;
                                            ui::render_number("Delete?",
                                                              list_idx,
                                                              recipients->size(),
                                                              {(*recipients)[list_idx].data(), (*recipients)[list_idx].size()});
                                        }
                                        break;
                                    }
                                    case 3: // Change password
                                        input_len = 0;
                                        state     = ui::state_t::CHANGE_PW_NEW;
                                        ui::send(make_custom_request("New password:", ""));
                                        break;
                                    case 4: // WiFi setup
                                    {
                                        state = ui::state_t::WIFI_SCANNING;
                                        ui::send(make_custom_request("Scanning for", "networks..."));

                                        esp_err_t scan_ret = wifi::scan(wifi_scan_results, wifi_scan_count);
                                        if (scan_ret != ESP_OK || wifi_scan_count == 0) {
                                            ESP_LOGW(TAG, "WiFi scan found nothing or failed: %s", esp_err_to_name(scan_ret));
                                            ui::send_feedback("No networks", "found");
                                            state = ui::state_t::ADMIN_MENU;
                                            ui::render_menu(menu_idx);
                                            break;
                                        }

                                        wifi_list_idx = 0;
                                        state         = ui::state_t::WIFI_LIST;
                                        ui::render_wifi_entry(wifi_list_idx, wifi_scan_count, wifi_scan_results[wifi_list_idx]);
                                        break;
                                    }
                                    default:
                                        break;
                                }
                            }
                            break;
                        }

                        case ui::state_t::VIEW_NUMBERS: {
                            auto recipients = storage::get_recipients();
                            if (!recipients || recipients->empty()) {
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            }

                            if (recv_key == 'C') {
                                list_idx = (list_idx == 0) ? (recipients->size() - 1) : (list_idx - 1);
                            } else if (recv_key == 'D') {
                                list_idx = (list_idx + 1) % recipients->size();
                            } else if (recv_key == '*' || recv_key == 'B' || recv_key == 'A') {
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                ui::send(password_req);
                                break;
                            }

                            ui::render_number(
                                "Number", list_idx, recipients->size(), {(*recipients)[list_idx].data(), (*recipients)[list_idx].size()});
                            break;
                        }

                        case ui::state_t::ADD_NUMBER: {
                            if (recv_key >= '0' && recv_key <= '9') {
                                if (input_len < telegram::CHAT_ID_LEN) {
                                    input_buf[input_len++] = recv_key;
                                }
                            } else if (recv_key == 'B') {
                                if (input_len > 0) {
                                    input_buf[--input_len] = '\0';
                                }
                            } else if (recv_key == '*') {
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                ui::send(password_req);
                                break;
                            } else if (recv_key == 'A') {
                                if (input_len != telegram::CHAT_ID_LEN) {
                                    break; // Not enough digits yet. Ignore.
                                }

                                if (auto err = storage::add_recipient({input_buf.data(), input_len}); err == ESP_OK) {
                                    ESP_LOGI(TAG, "Recipient added");
                                    ui::send_feedback("Number added");
                                } else {
                                    ESP_LOGW(TAG, "Failed to add recipient: %s", esp_err_to_name(err));
                                    ui::send_feedback("Add failed", (err == ESP_ERR_INVALID_STATE) ? "Already exists" : "Storage full?");
                                }

                                input_len = 0;
                                state     = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            }

                            if (state == ui::state_t::ADD_NUMBER) {
                                ui::send(make_custom_request("New chat ID:", {input_buf.data(), input_len}));
                            }
                            break;
                        }

                        case ui::state_t::RM_NUMBER: {
                            auto recipients = storage::get_recipients();
                            if (!recipients || recipients->empty()) {
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            }

                            if (recv_key == 'C') {
                                list_idx = (list_idx == 0) ? (recipients->size() - 1) : (list_idx - 1);
                            } else if (recv_key == 'D') {
                                list_idx = (list_idx + 1) % recipients->size();
                            } else if (recv_key == '*' || recv_key == 'B') {
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                ui::send(password_req);
                                break;
                            } else if (recv_key == 'A') {
                                const auto& target = (*recipients)[list_idx];
                                if (auto err = storage::rm_recipient({target.data(), target.size()}); err == ESP_OK) {
                                    ESP_LOGI(TAG, "Recipient removed");
                                    ui::send_feedback("Number removed");
                                } else {
                                    ESP_LOGW(TAG, "Failed to remove recipient: %s", esp_err_to_name(err));
                                    ui::send_feedback("Removal failed");
                                }
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            }

                            if (state == ui::state_t::RM_NUMBER) {
                                ui::render_number(
                                    "Delete?", list_idx, recipients->size(), {(*recipients)[list_idx].data(), (*recipients)[list_idx].size()});
                            }
                            break;
                        }

                        case ui::state_t::CHANGE_PW_NEW: {
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
                                state     = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                ui::send(password_req);
                                break;
                            } else if (recv_key == 'A') {
                                if (input_len != storage::PASSWORD_LEN) {
                                    break; // Not enough digits yet. Ignore.
                                }
                                std::copy_n(input_buf.begin(), storage::PASSWORD_LEN, pw_pending.begin());
                                input_len = 0;
                                state     = ui::state_t::CHANGE_PW_CONFIRM;
                                ui::send(make_custom_request("Confirm pswd:", ""));
                                break;
                            }

                            if (state == ui::state_t::CHANGE_PW_NEW) {
                                std::array<char, storage::PASSWORD_LEN> mask{};
                                mask.fill('*');
                                ui::send(make_custom_request("New password:", {mask.data(), input_len}));
                            }
                            break;
                        }

                        case ui::state_t::CHANGE_PW_CONFIRM: {
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
                                state     = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                ui::send(password_req);
                                break;
                            } else if (recv_key == 'A') {
                                if (input_len != storage::PASSWORD_LEN) {
                                    break; // Not enough digits yet. Ignore.
                                }

                                const bool matches = std::equal(pw_pending.begin(), pw_pending.end(), input_buf.begin());
                                input_len          = 0;

                                if (!matches) {
                                    ESP_LOGW(TAG, "Password confirmation mismatch");
                                    ui::send_feedback("Mismatch", "Try again");
                                    state = ui::state_t::CHANGE_PW_NEW;
                                    ui::send(make_custom_request("New password:", ""));
                                    break;
                                }

                                if (auto err = storage::change_pswd({pw_pending.data(), pw_pending.size()}); err == ESP_OK) {
                                    ESP_LOGI(TAG, "Password changed");
                                    ui::send_feedback("Password", "changed");
                                } else {
                                    ESP_LOGW(TAG, "Failed to change password: %s", esp_err_to_name(err));
                                    ui::send_feedback("Change failed");
                                }

                                pw_pending.fill('\0');
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            }

                            if (state == ui::state_t::CHANGE_PW_CONFIRM) {
                                std::array<char, storage::PASSWORD_LEN> mask{};
                                mask.fill('*');
                                ui::send(make_custom_request("Confirm pswd:", {mask.data(), input_len}));
                            }
                            break;
                        }

                        case ui::state_t::WIFI_SCANNING: {
                            // Transient - scan() is synchronous and already moved state on
                            // by the time control would reach here.
                            break;
                        }

                        case ui::state_t::WIFI_LIST: {
                            if (recv_key == 'C') {
                                wifi_list_idx = (wifi_list_idx == 0) ? (wifi_scan_count - 1) : (wifi_list_idx - 1);
                            } else if (recv_key == 'D') {
                                wifi_list_idx = (wifi_list_idx + 1) % wifi_scan_count;
                            } else if (recv_key == '*') {
                                state = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                ui::send(password_req);
                                break;
                            } else if (recv_key == 'A') {
                                const auto& chosen = wifi_scan_results[wifi_list_idx];
                                wifi_selected_ssid.fill('\0');
                                std::strncpy(wifi_selected_ssid.data(), chosen.ssid.data(), wifi_selected_ssid.size() - 1);
                                const size_t ssid_len = std::strlen(wifi_selected_ssid.data());

                                if (chosen.authmode == WIFI_AUTH_OPEN) {
                                    // No password needed - connect straight away.
                                    state = ui::state_t::WIFI_CONNECTING;
                                    ui::send(make_custom_request("Connecting to", {wifi_selected_ssid.data(), ssid_len}));

                                    esp_err_t conn_ret = wifi::connect({wifi_selected_ssid.data(), ssid_len}, "");
                                    if (conn_ret == ESP_OK) {
                                        [[maybe_unused]] auto _ = storage::set_wifi_creds({wifi_selected_ssid.data(), ssid_len}, "");
                                        ui::send_feedback("Connected!");
                                    } else {
                                        ui::send_feedback("Connect failed", "Check network");
                                    }
                                    state = ui::state_t::ADMIN_MENU;
                                    ui::render_menu(menu_idx);
                                } else {
                                    wifi_pw_len = 0;
                                    wifi_pw_buf.fill('\0');
                                    mt_session.reset();
                                    state = ui::state_t::WIFI_PW_ENTRY;
                                    ui::send(make_custom_request("Password:", ""));
                                }
                                break;
                            }

                            ui::render_wifi_entry(wifi_list_idx, wifi_scan_count, wifi_scan_results[wifi_list_idx]);
                            break;
                        }

                        case ui::state_t::WIFI_PW_ENTRY: {
                            if (recv_key >= '0' && recv_key <= '9') {
                                if (!mt_session.on_digit(recv_key, wifi_pw_buf, wifi_pw_len)) {
                                    break; // Buffer full, press dropped
                                }
                            } else if (recv_key == 'B') {
                                if (wifi_pw_len > 0) {
                                    wifi_pw_buf[--wifi_pw_len] = '\0';
                                }
                                mt_session.on_backspace();
                            } else if (recv_key == 'C') {
                                mt_session.toggle_caps(wifi_pw_buf, wifi_pw_len);
                            } else if (recv_key == '*') {
                                state = ui::state_t::WIFI_LIST;
                                ui::render_wifi_entry(wifi_list_idx, wifi_scan_count, wifi_scan_results[wifi_list_idx]);
                                break;
                            } else if (recv_key == '#') {
                                g_admin_mode = false;
                                state        = ui::state_t::AWAITING_PASSWORD;
                                ui::send(password_req);
                                break;
                            } else if (recv_key == 'A') {
                                if (wifi_pw_len < 8) { // WPA2 minimum
                                    ui::send_feedback("Too short", "Min 8 chars");
                                    break;
                                }

                                const size_t ssid_len = std::strlen(wifi_selected_ssid.data());
                                state                 = ui::state_t::WIFI_CONNECTING;
                                ui::send(make_custom_request("Connecting to", {wifi_selected_ssid.data(), ssid_len}));

                                esp_err_t conn_ret = wifi::connect({wifi_selected_ssid.data(), ssid_len}, {wifi_pw_buf.data(), wifi_pw_len});
                                if (conn_ret == ESP_OK) {
                                    [[maybe_unused]] auto _ =
                                        storage::set_wifi_creds({wifi_selected_ssid.data(), ssid_len}, {wifi_pw_buf.data(), wifi_pw_len});
                                    ui::send_feedback("Connected!");
                                } else {
                                    ui::send_feedback("Connect failed", "Wrong password?");
                                }

                                wifi_pw_buf.fill('\0');
                                wifi_pw_len = 0;
                                state       = ui::state_t::ADMIN_MENU;
                                ui::render_menu(menu_idx);
                                break;
                            }

                            if (state == ui::state_t::WIFI_PW_ENTRY) {
                                // Mask everything except the char currently being cycled (if
                                // any), so the user can see what they're about to lock in -
                                // same trick old T9 phones used for password fields.
                                std::array<char, wifi::PASSWORD_MAX_LEN> display_buf{};
                                for (size_t i = 0; i < wifi_pw_len; i++) {
                                    const bool is_live_char = mt_session.is_pending() && (i == wifi_pw_len - 1);
                                    display_buf[i]          = is_live_char ? wifi_pw_buf[i] : '*';
                                }
                                // Show only the trailing LCD_COLUMNS characters so typing past 16 chars scrolls.
                                const size_t start = wifi_pw_len > config::LCD_COLUMNS ? (wifi_pw_len - config::LCD_COLUMNS) : 0;
                                ui::send(make_custom_request("Password:", {display_buf.data() + start, wifi_pw_len - start}));
                            }
                            break;
                        }

                        case ui::state_t::WIFI_CONNECTING: {
                            // Transient - connect() is synchronous, state has already moved
                            // on by the time control would reach here.
                            break;
                        }
                    }
                }
            }

            [[noreturn]] void display(void* arg) {
                constexpr const char* TAG = "Display_task";
                ESP_LOGI(TAG, "Display_task started");

                // The last screen that was meant to persist (i.e. not itself a transient
                // "return_to_prev" alert). This is what a transient alert reverts back to
                // once its hold duration elapses - it can be a canned screen or the system
                // task's current interactive UI state (menu, in-progress digit entry, etc.).
                display_request_t persistent_request = password_req;
                display_request_t request            = {};

                // Renders a single request to the LCD, whether it's a canned screen_type or free form text.
                auto render_display_request = [](const display_request_t& request) {
                    if (request.use_custom_text) {
                        sys::println({request.line0.data(), request.line0.size()}, 0);
                        sys::println({request.line1.data(), request.line1.size()}, 1);
                    } else {
                        sys::println(SCREEN_MAP_LUT[std::to_underlying(request.screen_type)].first, 0);
                        sys::println(SCREEN_MAP_LUT[std::to_underlying(request.screen_type)].second, 1);
                    }
                };

                while (true) {
                    // Block till a request is received
                    xQueueReceive(g_display_queue, &request, portMAX_DELAY);

                    // Immediately render the requested screen
                    render_display_request(request);

                    if (request.return_to_prev) {
                        // Hold the current screen for the requested amount of time, then
                        // revert to whatever was persistently displayed before it.
                        vTaskDelay(pdMS_TO_TICKS(request.duration_ms));
                        render_display_request(persistent_request);
                    } else {
                        // Since we do not have to return to the previous screen, this request
                        // now becomes the screen we revert back to upon a new display request.
                        persistent_request = request;
                    }

                    // If return_to_prev is false, there is no reason to block here.
                    // Instead the screen will be held till the next display request.
                }
            }

            [[noreturn]] void switches(void* arg) {
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
                        sys::alert_on_reed_switch_break(g_admin_mode);
                    }

                    if (tamper_switch_broken) {
                        const auto& tamper_broken_request = g_admin_mode ? tamper_switch_broken_admin : tamper_switch_broken_no_admin;
                        xQueueSend(g_display_queue, &tamper_broken_request, portMAX_DELAY);
                        sys::alert_on_tamper_switch_break(g_admin_mode);
                    }
                }
            }

            [[noreturn]] void wifi(void* arg) {
                constexpr const char* TAG = "WiFi_task";
                ESP_LOGI(TAG, "WiFi_task started");

                // If we have credentials saved from a previous boot, attempt to connect to it.
                if (auto creds = storage::get_wifi_creds(); creds.has_value()) {
                    const size_t ssid_len = std::strlen(creds->ssid.data());
                    const size_t pw_len   = std::strlen(creds->password.data());

                    ESP_LOGI(TAG, "Found saved WiFi credentials for \"%.*s\". Attempting to connect", ssid_len, creds->ssid.data());

                    if (auto ret = wifi::connect({creds->ssid.data(), ssid_len}, {creds->password.data(), pw_len}); ret != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to connect to saved WiFi AP on boot: %s", esp_err_to_name(ret));
                    }
                } else {
                    ESP_LOGW(TAG, "No saved WiFi credentials. Use the admin menu's \"WiFi setup\" to configure one");
                }

                // wifi::init() and the initial connect attempt (if credentials were saved)
                // already happened in init_all() before any task was created. This task just
                // periodically checks the connection is still alive - actual reconnect-on-drop
                // is handled inside wifi.cpp's own event handler; this is a backstop in case
                // that gets stuck.
                uint32_t consc_err_counter = 0;

                while (true) {
                    if (!wifi::is_connected()) {
                        ESP_LOGW(TAG, "WiFi not connected");
                        consc_err_counter++;
                        if (consc_err_counter >= config::MAX_WIFI_CONSC_STATUS_ERRORS) {
                            ESP_LOGE(TAG, "WiFi has been down too long. Rebooting to force a clean reconnection attempt");
                            utils::reboot();
                        }
                    } else {
                        consc_err_counter = 0;
                    }
                    vTaskDelay(pdMS_TO_TICKS(config::WIFI_TASK_PERIOD_MS));
                }
            }

        } // namespace tasks

    } // namespace

    void run() {
        // Initialize all used resources
        init_all();

        constexpr const char* TAG = "Tasks";

        // Create the tasks
        BaseType_t ret = xTaskCreate(tasks::system, "system_task", config::SYSTEM_TASK_STACK, nullptr, config::SYSTEM_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the system task");
            utils::fatal();
        }

        ret = xTaskCreate(tasks::display, "display_task", config::DISPlAY_TASK_STACK, nullptr, config::DISPlAY_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the display task");
            utils::fatal();
        }

        ret = xTaskCreate(tasks::switches, "switch_task", config::SWITCH_TASK_STACK, nullptr, config::SWITCH_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the switch task");
            utils::fatal();
        }

        ret = xTaskCreate(tasks::wifi, "wifi_task", config::WIFI_TASK_STACK, nullptr, config::WIFI_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the wifi task");
            utils::fatal();
        }
    }

} // namespace tasks
