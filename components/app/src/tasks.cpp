#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "portmacro.h"

#include "utils.hpp"
#include "tasks.hpp"
#include "config.hpp"
#include "keypad.hpp"
#include "switch.hpp"
#include "display.hpp"
#include "sim800l.hpp"
#include "secure_system.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_littlefs.h"


namespace tasks {

    namespace {

        void deinit_all() {
            ESP_LOGI("Info", "Deinitializing the system. Cleaning resources");
            TRY_THEN_LOG(display::shutdown_screen(), "Failed to display the power down screen");
            TRY_THEN_LOG(display::deinit(), "Failed to deinitialize the display");
            TRY_THEN_LOG(gsm::deinit(), "Failed to deinitialize the SIM800L module");
            TRY_THEN_LOG(crypto::deinit(), "Failed to deinitialize the crypto interface");
            TRY_THEN_LOG(esp_vfs_littlefs_unregister(config::FILESYSTEM_BASE_PATH), "Failed to unmount filesystem");
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
            constexpr esp_vfs_littlefs_conf_t config = {
                .base_path              = FILESYSTEM_BASE_PATH,
                .partition_label        = FILESYSTEM_PARTITION_LABEL,
                .partition              = nullptr,
                .blockdev               = nullptr,
                .format_if_mount_failed = 1,
                .read_only              = 0,
                .dont_mount             = 0,
                .grow_on_mount          = 1,
            };
            TRY_WITH_FUNC_VOID(esp_vfs_littlefs_register(&config), utils::fatal());

            // Initialize the crypto interface
            TRY_WITH_FUNC_VOID(crypto::init(), utils::fatal());

            // Initialize the display
            TRY_WITH_FUNC_VOID(display::init(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::clear_screen(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::backlight_on(), utils::fatal());
            TRY_WITH_FUNC_VOID(display::bootup_screen(), utils::fatal());

            // Initialize the SIM800L module. The SIM800L requires around 2-3s after power on to fully stablize.
            TRY_WITH_FUNC_VOID(gsm::init(), utils::fatal());
        }

        // Tasks
        [[noreturn]] void display_task(void* arg) {
            while (true) {
                vTaskDelay(portMAX_DELAY);
            }
        }

        [[noreturn]] void switch_task(void* arg) {
            while (true) {
                vTaskDelay(portMAX_DELAY);
            }
        }

    } // namespace

    void run() {
        // Initialize all used resources
        init_all();

        constexpr const char* TAG = "Tasks";

        // Create the tasks
        BaseType_t ret = xTaskCreate(display_task, "display_task", config::DISPLAY_TASK_STACK, nullptr, config::DISPLAY_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the display task");
            utils::fatal();
        }

        ret = xTaskCreate(switch_task, "switch_task", config::SWITCH_TASK_STACK, nullptr, config::SWITCH_TASK_PRIORITY, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create the switch task");
            utils::fatal();
        }
    }

} // namespace tasks
