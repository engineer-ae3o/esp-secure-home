#include "config.hpp"
#include "keypad.hpp"
#include "switch.hpp"
#include "utils.hpp"
#include "tasks.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_littlefs.h"

namespace tasks {

    namespace {

        // cppcheck-suppress constParameterPointer
        [[noreturn]] void display_task(void* arg) {
            while (true) {
            }
        }

        void init_all() {
            // Mount the filesystem
            constexpr esp_vfs_littlefs_conf_t config = {
                .base_path              = config::FILESYSTEM_BASE_PATH,
                .partition_label        = config::FILESYSTEM_PARTITION_LABEL,
                .partition              = nullptr,
                .blockdev               = nullptr,
                .format_if_mount_failed = 1,
                .read_only              = 0,
                .dont_mount             = 0,
                .grow_on_mount          = 1,
            };
            TRY_WITH_FUNC_VOID(esp_vfs_littlefs_register(&config), utils::fatal());
        }

    } // namespace

    void run() {
    }

} // namespace tasks
