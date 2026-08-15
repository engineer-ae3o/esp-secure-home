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

#include <atomic>
#include <cstdint>
#include <utility>
#include <optional>
#include <string_view>


namespace tasks {

    namespace {

        // Thedisplay queue to which display requests are passed into
        QueueHandle_t g_display_queue{};

        // Start off with at the lowest privilege level
        std::atomic<bool> g_admin_mode = false;

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

            // Register a shutdown handler to get called before any reboot
            TRY_WITH_FUNC_VOID(esp_register_shutdown_handler(deinit_all), utils::fatal());

            // Initialize the gpio isr service
            TRY_WITH_FUNC_VOID(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1), utils::fatal());

            // Create the display queue with a size of 16 elements to hold as many display requests as possible
            g_display_queue = xQueueCreate(16, sizeof(display_request_t));
            if (g_display_queue == nullptr) {
                ESP_LOGE(TAG, "Failed to create the display queue");
                utils::fatal();
            }

            ESP_LOGI("Init", "Done initializing all components");
        }

        // Tasks
        [[noreturn]] void system_task(void* arg) {
            constexpr const char* TAG = "System_task";
            ESP_LOGI(TAG, "System_task started");

            // Initialize the keypad
            pad::keypad_t<false> keypad;
            TRY_WITH_FUNC_VOID(keypad.init({.row_pins = config::KEYPAD_ROW_PINS, .col_pins = config::KEYPAD_COLUMN_PINS}), utils::fatal());

            // get_event_queue() only fails if called when not initialized. Safe to extract the value directly
            auto* keypad_event_queue = keypad.get_event_queue().value();
            char  recv_key{};

            // Push the password request screens to the display queue
            xQueueSend(g_display_queue, &password_req, portMAX_DELAY);

            while (true) {
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

            TRY_WITH_FUNC_VOID(display::bootup_screen(), utils::fatal());

            display_request_t request = {};

            while (true) {
                // Block till a request is received
                xQueueReceive(g_display_queue, &request, portMAX_DELAY);
                const auto& [return_to_prev_scr, screen_type, duration_ms] = request;

                // Keep track of the current screen before it is changed
                const auto previous_screen = g_current_screen.load(std::memory_order_seq_cst);

                // Update the global current screen variable and switch to the requested screen
                g_current_screen = screen_type;
                sys::println(SCREEN_MAP_LUT[std::to_underlying(screen_type)].first, 0);
                sys::println(SCREEN_MAP_LUT[std::to_underlying(screen_type)].second, 1);

                if (return_to_prev_scr) {
                    // Hold the current screen for the requested amount of time
                    vTaskDelay(pdMS_TO_TICKS(duration_ms));

                    // Update the global current screen variable and switch back to the previous screen
                    g_current_screen = previous_screen;
                    sys::println(SCREEN_MAP_LUT[std::to_underlying(previous_screen)].first, 0);
                    sys::println(SCREEN_MAP_LUT[std::to_underlying(previous_screen)].second, 1);
                }

                // If return_to_prev_scr is false, there is no reason to block.
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
                    reed_switch_broken = true;
                }

                if (notification & std::to_underlying(nc::type_t::TAMPER)) {
                    tamper_switch_broken = true;
                }

                if (reed_switch_broken) {
                    if (g_admin_mode) {
                        xQueueSend(g_display_queue, &reed_switch_broken_admin, portMAX_DELAY);
                    } else {
                        xQueueSend(g_display_queue, &reed_switch_broken_no_admin, portMAX_DELAY);
                    }
                }

                if (tamper_switch_broken) {
                    if (g_admin_mode) {
                        xQueueSend(g_display_queue, &tamper_switch_broken_admin, portMAX_DELAY);
                    } else {
                        xQueueSend(g_display_queue, &tamper_switch_broken_no_admin, portMAX_DELAY);
                    }
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

        // Spawn a temporary thread to initialize the SIM800L after 8s to avoid blocking the whole system for this period of time
        ret = xTaskCreate(
            [](void* arg) {
                vTaskDelay(pdMS_TO_TICKS(8'000)); // Block this thread for at least 8s
                ESP_LOGI("GSM Init Task", "Initializing the SIM800L");
                // Initialize the SIM800L module. The SIM800L requires around sometime after power on to fully stablize.
                // There's still a chance for the initialization to fail. Reboot so we retry instead of hard crashing.
                TRY_WITH_FUNC_VOID(gsm::init(), utils::reboot());
                TRY_THEN_LOG(gsm::get_sim_status(), "SIM card not ready"); // Not a fatal error. We'll retry later on.
                ESP_LOGI("GSM Init Task", "Done initializing the SIM800L");
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
