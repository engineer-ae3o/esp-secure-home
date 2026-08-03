#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "utils.hpp"
#include "tasks.hpp"
#include "config.hpp"
#include "keypad.hpp"
#include "switch.hpp"
#include "secure_system.hpp"

#include "i2cdev.h"
#include "hd44780.h"
#include "pcf8574.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_littlefs.h"

namespace tasks {

    namespace {

        // LCD handle
        i2c_dev_t g_pcf8574{};

        // LCD config
        constexpr hd44780 g_hd44780_config = {
            .write_cb =
                [](const hd44780* lcd, uint8_t data) {
                    // Keep track of consecutive write errors
                    static uint32_t consv_err_counter = 0;
                    if (auto ret = pcf8574_port_write(&g_pcf8574, data); ret != ESP_OK) {
                        ESP_LOGW("LCD", "Failed to write data to LCD: %s", esp_err_to_name(ret));
                        consv_err_counter++;
                        if (consv_err_counter >= config::MAX_CONSV_ERRORS) {
                            ESP_LOGE("LCD", "Too many write failures: %u. Rebooting system", consv_err_counter);
                            utils::reboot();
                        }
                    } else {
                        consv_err_counter++;
                    }
                    return 0;
                },
            .pins =
                {
                    .rs = 0,
                    .e  = 2,
                    .d4 = 4,
                    .d5 = 5,
                    .d6 = 6,
                    .d7 = 7,
                    .bl = 3,
                },
            .font      = HD44780_FONT_5X8,
            .lines     = 2,
            .backlight = false,
        };

        void deinit_all() {
            ESP_LOGI("Info", "Deinitializing the system. Cleaning resources");
            TRY_THEN_LOG(pcf8574_free_desc(&g_pcf8574), "Failed to free PCF8574 resources");
            TRY_THEN_LOG(i2cdev_done(), "Failed to cleanup the I2C subsystem/interface");
            TRY_THEN_LOG(ss::deinit(), "Failed to deinitialize the secure subsystem");
            TRY_THEN_LOG(esp_vfs_littlefs_unregister(config::FILESYSTEM_BASE_PATH), "Failed to unmount filesystem");
            ESP_LOGI("Info", "Resources cleaned up. Rebooting.......");
        }

        void init_all() {
            using namespace config;
            using namespace utils;

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
            TRY_WITH_FUNC_VOID(esp_register_shutdown_handler(deinit_all), fatal());

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
            TRY_WITH_FUNC_VOID(esp_vfs_littlefs_register(&config), fatal());

            // Initialize the secure and crypto interface
            TRY_WITH_FUNC_VOID(ss::init(), fatal());

            // Initialize the I2C interface/subsystem
            TRY_WITH_FUNC_VOID(i2cdev_init(), fatal());

            // Initialize the PCF8574 and the LCD
            TRY_WITH_FUNC_VOID(pcf8574_init_desc(&g_pcf8574, LCD_ADDR, LCD_PORT, LCD_SDA, LCD_SCL), fatal());
            TRY_WITH_FUNC_VOID(hd44780_init(&g_hd44780_config), fatal());
        }

        // Tasks
        [[noreturn]] void display_task(void* arg) {
            while (true) {
            }
        }

    } // namespace

    void run() {
        init_all();
    }

} // namespace tasks
