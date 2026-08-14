#include "storage.hpp"
#include "esp_heap_caps.h"
#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "file.hpp"

#include <span>
#include <array>
#include <cstring>
#include <optional>
#include <string_view>


namespace storage {

    namespace {

        constexpr const char* TAG = "Storage";

        // Type aliases so I don't go crazy from typing
        using pswd_t     = std::array<uint8_t, PASSWORD_LEN>;
        using pnumber_t  = std::array<char, gsm::PHONE_NUMBER_LEN>;
        using pnumbers_t = std::array<pnumber_t, MAX_PNUMBERS>;

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
        [[maybe_unused]] pswd_file_data_t     g_pswd_storage{};
        [[maybe_unused]] pnumbers_file_data_t g_pnumbers_storage{};

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

        const bool first_boot = file::is_first_boot();
        if (first_boot) [[unlikely]] {
            // Since first boot, create all necessary files and zero out the files.
            ESP_LOGI(TAG, "First boot. Zeroing out the files");
            file::create();

            constexpr pswd_file_data_t pswd_file_data = {
                .pswd = DEFAULT_PASSWORD,
            };

            constexpr pnumbers_file_data_t pnumbers_file_data = {
                .num_of_pnumbers = 0,
                .pnumbers        = {},
            };

            // Write the data to flash
            TRY(file::write(file::name_t::PSWD, {reinterpret_cast<const uint8_t*>(&g_pswd_storage), sizeof(pswd_file_data)}));
            TRY(file::write(file::name_t::PNUMBERS, {reinterpret_cast<const uint8_t*>(&g_pnumbers_storage), sizeof(pnumbers_file_data)}));

            // Reboot the system once the default password and phone numbers have been written to flash
            utils::reboot();
        }

        // Since not first boot, open the files and load the data there.
        file::open();
        TRY(file::read(file::name_t::PSWD, {reinterpret_cast<uint8_t*>(&g_pswd_storage), sizeof(g_pswd_storage)}));
        TRY(file::read(file::name_t::PNUMBERS, {reinterpret_cast<uint8_t*>(&g_pnumbers_storage), sizeof(g_pnumbers_storage)}));

        // Get a buffer to store the data temporarily so they can be null terminated and read
        std::array<char, std::max(PASSWORD_LEN, gsm::PHONE_NUMBER_LEN) + 1> temp{};

        memcpy(temp.data(), &g_pswd_storage.pswd, PASSWORD_LEN);
        temp.back() = '\0';
        ESP_LOGI(TAG, "Current password: %s", temp.data());

        for (size_t i = 0; i < g_pnumbers_storage.num_of_pnumbers; i++) {
            memcpy(temp.data(), &g_pnumbers_storage.pnumbers[i], gsm::PHONE_NUMBER_LEN);
            temp.back() = '\0';
            ESP_LOGI(TAG, "Phone number %zu: %s", i, temp.data());
        }

        g_is_initialized = true;
        return ESP_OK;
    }

    esp_err_t deinit() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        void cleanup();
        return ESP_OK;
    }

    esp_err_t change_pswd(std::string_view new_pswd) {

        return ESP_OK;
    }

    bool check_pswd(std::string_view pswd_to_cmp) {

        return false;
    }

    esp_err_t add_pnumber(std::string_view new_pnumber) {

        return ESP_OK;
    }

    esp_err_t rm_pnumber(std::string_view new_pnumber) {

        return ESP_OK;
    }

    void get_pnumbers() {
    }

} // namespace storage
