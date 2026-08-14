#include "storage.hpp"
#include "esp_err.h"
#include "sim800l.hpp"
#include "utils.hpp"
#include "file.hpp"

#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>


namespace storage {

    namespace {

        constexpr const char* TAG = "Storage";

        constexpr pswd_t DEFAULT_PASSWORD = {'1', '2', '3', '4', '5', '6', '7', '8'};

        // The kind of data that will be stored in the two files
        struct pswd_file_data_t {
            pswd_t pswd{};
        };

        struct pnumbers_file_data_t {
            uint32_t   num_of_pnumbers{};
            pnumbers_t pnumbers{};
        };

        bool g_is_initialized = false;

        // Storage for the data being stored in the files at runtime
        pswd_file_data_t     g_pswd_storage{};
        pnumbers_file_data_t g_pnumbers_storage{};

        // Helpers
        void cleanup() {
            memset(&g_pswd_storage, 0, sizeof(g_pswd_storage));
            memset(&g_pnumbers_storage, 0, sizeof(g_pnumbers_storage));
            file::close();
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (file::is_first_boot()) [[unlikely]] {
            // Since first boot, create all necessary files and zero out the files.
            ESP_LOGI(TAG, "First boot. Creating and zeroing out the required directory and files");
            file::create();

            // Default configuration
            constexpr pswd_file_data_t pswd_data = {
                .pswd = DEFAULT_PASSWORD,
            };

            constexpr pnumbers_file_data_t pnumbers_data = {
                .num_of_pnumbers = 0,
                .pnumbers        = {},
            };

            // Write the data to flash
            TRY_WITH_FUNC(file::write(file::name_t::PSWD, {reinterpret_cast<const uint8_t*>(&pswd_data), sizeof(pswd_data)}), cleanup());
            TRY_WITH_FUNC(file::write(file::name_t::PNUMBERS, {reinterpret_cast<const uint8_t*>(&pnumbers_data), sizeof(pnumbers_data)}), cleanup());

            // Reboot the system once the default password and phone numbers have been written to flash
            cleanup();
            utils::reboot();
            while (true);
        }

        // Since not first boot, open the files and load the data there.
        file::open();
        TRY_WITH_FUNC(file::read(file::name_t::PSWD, {reinterpret_cast<uint8_t*>(&g_pswd_storage), sizeof(g_pswd_storage)}), cleanup());
        TRY_WITH_FUNC(file::read(file::name_t::PNUMBERS, {reinterpret_cast<uint8_t*>(&g_pnumbers_storage), sizeof(g_pnumbers_storage)}), cleanup());

        /**
         * Commented out for obvious reasons. Is only useful during development.
         * 
         * // Get a buffer to store the data temporarily so they can be null terminated and read
         * std::array<char, std::max(PASSWORD_LEN, gsm::PHONE_NUMBER_LEN) + 1> temp{};
         * 
         * memcpy(temp.data(), &g_pswd_storage.pswd, PASSWORD_LEN);
         * temp.back() = '\0';
         * ESP_LOGI(TAG, "Current password: %s", temp.data());
         * 
         * for (size_t i = 0; i < g_pnumbers_storage.num_of_pnumbers; i++) {
         *     memcpy(temp.data(), &g_pnumbers_storage.pnumbers[i], gsm::PHONE_NUMBER_LEN);
         *     temp.back() = '\0';
         *     ESP_LOGI(TAG, "Phone number %zu: %s", i, temp.data());
         * }
         */

        g_is_initialized = true;
        return ESP_OK;
    }

    esp_err_t deinit() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        cleanup();
        return ESP_OK;
    }

    esp_err_t change_pswd(std::string_view new_pswd) {
        if (new_pswd.data() == nullptr || new_pswd.length() != PASSWORD_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        // Error out if the new password is already the current password
        if (check_pswd(new_pswd)) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        // Save the old password to revert back to incase the write to the file failed
        const auto old_pswd = g_pswd_storage.pswd;

        // Copy the new password to our local storage
        for (size_t i = 0; i < PASSWORD_LEN; i++) {
            g_pswd_storage.pswd[i] = new_pswd[i];
        }

        // Attempt to write the new password to flash
        if (auto ret = file::write(file::name_t::PSWD, {reinterpret_cast<uint8_t*>(&g_pswd_storage), sizeof(g_pswd_storage)}); ret != ESP_OK) {
            // If the write failed, revert to the old password
            g_pswd_storage.pswd = old_pswd;
            return ret;
        }

        // The write succeeded, g_pswd_storage and the file already have the new password. Nothing else to do.

        return ESP_OK;
    }

    bool check_pswd(std::string_view pswd_to_cmp) {
        if (pswd_to_cmp.data() == nullptr || pswd_to_cmp.length() != PASSWORD_LEN) {
            return false;
        }
        for (size_t i = 0; i < PASSWORD_LEN; i++) {
            if (pswd_to_cmp[i] != g_pswd_storage.pswd[i]) {
                return false;
            }
        }
        return true;
    }

    esp_err_t add_pnumber(std::string_view pnumber_to_add) {

        return ESP_OK;
    }

    esp_err_t rm_pnumber(std::string_view pnumber_to_rm) {

        return ESP_OK;
    }

    std::optional<std::span<pnumber_t>> get_pnumbers() {
        if (g_pnumbers_storage.num_of_pnumbers == 0) {
            return std::nullopt;
        }
        return {};
    }

} // namespace storage
