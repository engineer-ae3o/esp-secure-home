#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "system.hpp"
#include "utils.hpp"
#include "tasks.hpp"
#include "config.hpp"
#include "keypad.hpp"
#include "switch.hpp"
#include "display.hpp"
#include "sim800l.hpp"
#include "storage.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "portmacro.h"
#include "esp_system.h"
#include "esp_littlefs.h"

#include <cstdint>
#include <string_view>
#include <utility>
#include <optional>


namespace tasks {

    namespace {

        void deinit_all() {
            ESP_LOGI("Info", "Deinitializing the system. Cleaning resources");
            TRY_THEN_LOG(gsm::deinit(), "Failed to deinitialize the SIM800L module");
            TRY_THEN_LOG(storage::deinit(), "Failed to deinitialize the storage interface");
            TRY_THEN_LOG(display::shutdown_screen(), "Failed to display the power down screen");
            TRY_THEN_LOG(display::deinit(), "Failed to deinitialize the display");
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

            // Register a shutdown handler to get called before any reboot
            TRY_WITH_FUNC_VOID(esp_register_shutdown_handler(deinit_all), utils::fatal());

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

            // Initialize the display
            TRY_WITH_FUNC_VOID(display::init(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::clear_screen(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::backlight_on(), utils::fatal());

            // Initialize the SIM800L module. The SIM800L requires around 2-3s after power on to fully stablize.
            TRY_WITH_FUNC_VOID(gsm::init(), utils::fatal());
            TRY_THEN_LOG(gsm::get_sim_status(), "SIM card not ready"); // Not a fatal error. We'll retry later on.

            // Initialize the storage interface
            TRY_WITH_FUNC_VOID(storage::init(), utils::fatal());

            ESP_LOGI("Init", "Done initializing all components");
        }

        // Helpers
        std::optional<nc::type_t> check_for_switch_break() {
            uint32_t notification{};
            xTaskNotifyWait(0, UINT32_MAX, &notification, 0);

            if (notification & std::to_underlying(nc::type_t::REED)) {
                return nc::type_t::REED;
            }

            if (notification & std::to_underlying(nc::type_t::TAMPER)) {
                return nc::type_t::TAMPER;
            }

            return std::nullopt;
        }

        //
        struct display_message_t {
            std::string_view top_message;
            std::string_view bottom_message;

            uint32_t delay_ms{};
            bool     got_to_previous_when_done{};
        };

        enum class display_event_t : uint8_t {
            BOOTUP,
            PSWD_REQ,
            TAMPER_SWITCH_BROKEN_ADMIN,
            REED_SWITCH_BROKEN_ADMIN,
            TAMPER_SWITCH_BROKEN_NO_ADMIN,
            REED_SWITCH_BROKEN_NO_ADMIN,
        };

        QueueHandle_t display_event_queue{};

        // Tasks
        [[noreturn]] void system_task(void* arg) {
            constexpr const char* TAG = "System_task";
            ESP_LOGI(TAG, "System_task started");

            // Initialize the keypad
            pad::keypad_t<> keypad;
            TRY_WITH_FUNC_VOID(keypad.init({.row_pins = config::KEYPAD_ROW_PINS, .col_pins = config::KEYPAD_COLUMN_PINS}), utils::fatal());

            // Initialize the reed and tamper switches. The keypad initializes the gpio isr service
            // which these two depend on, hence why it's initialized first.
            nc::switch_t<nc::type_t::REED> reed;
            TRY_WITH_FUNC_VOID(reed.init({.pin = config::REED_SWITCH_PIN, .recv_task_handle = xTaskGetCurrentTaskHandle()}), utils::fatal());

            nc::switch_t<nc::type_t::TAMPER> tamper;
            TRY_WITH_FUNC_VOID(tamper.init({.pin = config::TAMPER_SWITCH_PIN, .recv_task_handle = xTaskGetCurrentTaskHandle()}), utils::fatal());

            // get_event_queue() only fails if called when not initialized. Safe to extract the value directly
            auto* keypad_event_queue = keypad.get_event_queue().value();
            char  recv_key{};

            // Start off with at the lowest privilege level
            bool admin_mode = false;

            // Bootup screen, and request for the password to enter admin mode
            TRY_WITH_FUNC_VOID(display::bootup_screen(), utils::fatal());
            TRY_THEN_LOG(display::clear_screen(), "Failed to clear display screen");
            sys::println("Enter password:", 0);

            while (true) {
                // Check if any switch has been broken
                auto switch_event = check_for_switch_break();
                if (switch_event) {
                    // A switch has been broken. Behaviour depends on whether we're in admin mode or not
                    switch (switch_event.value()) {
                        case nc::type_t::REED:
                            sys::reed_switch_broken(admin_mode);
                            break;

                        case nc::type_t::TAMPER:
                            sys::tamper_switch_broken(admin_mode);
                            break;

                        default:
                            ESP_LOGW(TAG, "Invalid switch event");
                            break;
                    }
                }

                // Check for any key press event
                auto ret = xQueueReceive(keypad_event_queue, &recv_key, 0);
                if (ret == pdPASS) {
                    ESP_LOGI(TAG, "Key pressed: %c", recv_key);
                }

                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }

        [[noreturn]] void display_task(void* arg) {
            constexpr const char* TAG = "Display_task";
            ESP_LOGI(TAG, "Display_task started");

            while (true) {
                vTaskDelay(pdMS_TO_TICKS(portMAX_DELAY));
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
    }

} // namespace tasks
