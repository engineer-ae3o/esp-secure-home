#include "storage.hpp"
#include "esp_err.h"
#include "telegram.hpp"
#include "utils.hpp"
#include "file.hpp"

#include <span>
#include <cstring>
#include <optional>
#include <algorithm>
#include <type_traits>
#include <string_view>


namespace storage {

    namespace {

        constexpr const char* TAG = "Storage";

        constexpr pswd_t DEFAULT_PASSWORD = {'1', '2', '3', '4', '5', '6', '7', '8'};

        // The kind of data that will be stored in the three files
        struct pswd_file_data_t {
            pswd_t pswd{};
        };

        struct recipients_file_data_t {
            uint32_t     num_of_recipients{};
            recipients_t recipients{};
        };

        struct wifi_file_data_t {
            bool          has_creds{};
            wifi_creds_t  creds{};
        };

        bool g_is_initialized = false;

        // Storage for the data being stored in the files at runtime
        pswd_file_data_t       g_pswd_storage{};
        recipients_file_data_t g_recipients_storage{};
        wifi_file_data_t       g_wifi_storage{};

        // Helpers
        void cleanup() {
            memset(&g_pswd_storage, 0, sizeof(g_pswd_storage));
            memset(&g_recipients_storage, 0, sizeof(g_recipients_storage));
            memset(&g_wifi_storage, 0, sizeof(g_wifi_storage));
            file::close();
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (file::is_first_boot()) [[unlikely]] {
            ESP_LOGI(TAG, "First boot. Creating and zeroing out the required directory and files");
            file::create();

            constexpr pswd_file_data_t pswd_data = {
                .pswd = DEFAULT_PASSWORD,
            };

            constexpr recipients_file_data_t recipients_data = {
                .num_of_recipients = 0,
                .recipients        = {},
            };

            constexpr wifi_file_data_t wifi_data = {
                .has_creds = false,
                .creds     = {},
            };

            TRY_WITH_FUNC(file::write(file::name_t::PSWD, {reinterpret_cast<const uint8_t*>(&pswd_data), sizeof(pswd_data)}), cleanup());
            TRY_WITH_FUNC(
                file::write(file::name_t::RECIPIENTS, {reinterpret_cast<const uint8_t*>(&recipients_data), sizeof(recipients_data)}), cleanup());
            TRY_WITH_FUNC(file::write(file::name_t::WIFI_CREDS, {reinterpret_cast<const uint8_t*>(&wifi_data), sizeof(wifi_data)}), cleanup());

            cleanup();
            utils::reboot();
        }

        file::open();
        TRY_WITH_FUNC(file::read(file::name_t::PSWD, {reinterpret_cast<uint8_t*>(&g_pswd_storage), sizeof(g_pswd_storage)}), cleanup());
        TRY_WITH_FUNC(
            file::read(file::name_t::RECIPIENTS, {reinterpret_cast<uint8_t*>(&g_recipients_storage), sizeof(g_recipients_storage)}), cleanup());
        TRY_WITH_FUNC(file::read(file::name_t::WIFI_CREDS, {reinterpret_cast<uint8_t*>(&g_wifi_storage), sizeof(g_wifi_storage)}), cleanup());

        if (g_recipients_storage.num_of_recipients > MAX_RECIPIENTS) {
            ESP_LOGE(TAG, "Corrupted recipient count in storage (%u) when it should be %u", g_recipients_storage.num_of_recipients, MAX_RECIPIENTS);
            cleanup();
            return ESP_ERR_INVALID_SIZE;
        }

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

    bool check_pswd(std::string_view pswd_to_cmp) {
        if (!g_is_initialized || pswd_to_cmp.length() != PASSWORD_LEN) {
            return false;
        }

        uint8_t diff = 0;
        for (size_t i = 0; i < PASSWORD_LEN; i++) {
            diff |= static_cast<uint8_t>(pswd_to_cmp[i]) ^ static_cast<uint8_t>(g_pswd_storage.pswd[i]);
        }

        return diff == 0;
    }

    esp_err_t change_pswd(std::string_view new_pswd) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (new_pswd.length() != PASSWORD_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        if (check_pswd(new_pswd)) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        const auto old_pswd = g_pswd_storage.pswd;

        for (size_t i = 0; i < PASSWORD_LEN; i++) {
            g_pswd_storage.pswd[i] = new_pswd[i];
        }

        if (auto ret = file::write(file::name_t::PSWD, {reinterpret_cast<const uint8_t*>(&g_pswd_storage), sizeof(g_pswd_storage)}); ret != ESP_OK) {
            g_pswd_storage.pswd = old_pswd;
            return ret;
        }

        return ESP_OK;
    }

    esp_err_t add_recipient(std::string_view chat_id_to_add) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (chat_id_to_add.length() != telegram::CHAT_ID_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        if (g_recipients_storage.num_of_recipients >= MAX_RECIPIENTS) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        auto& [num_of_recipients, recipients] = g_recipients_storage;

        for (size_t i = 0; i < num_of_recipients; i++) {
            if (std::string_view{recipients[i].data(), recipients[i].size()} == chat_id_to_add) {
                return ESP_ERR_INVALID_STATE;
            }
        }

        for (size_t i = 0; i < telegram::CHAT_ID_LEN; i++) {
            recipients[num_of_recipients][i] = chat_id_to_add[i];
        }
        num_of_recipients++;

        if (auto ret = file::write(file::name_t::RECIPIENTS, {reinterpret_cast<const uint8_t*>(&g_recipients_storage), sizeof(g_recipients_storage)});
            ret != ESP_OK) {
            num_of_recipients--;
            recipients[num_of_recipients] = {};
            return ret;
        }

        return ESP_OK;
    }

    esp_err_t rm_recipient(std::string_view chat_id_to_rm) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (chat_id_to_rm.length() != telegram::CHAT_ID_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        if (g_recipients_storage.num_of_recipients == 0) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        auto& [num_of_recipients, recipients] = g_recipients_storage;

        size_t target_idx = num_of_recipients;
        for (size_t i = 0; i < num_of_recipients; i++) {
            if (std::string_view{recipients[i].data(), recipients[i].size()} == chat_id_to_rm) {
                target_idx = i;
                break;
            }
        }
        if (target_idx == num_of_recipients) {
            return ESP_ERR_NOT_FOUND;
        }

        const auto backup_storage = g_recipients_storage;

        for (size_t i = target_idx; i < num_of_recipients - 1; ++i) {
            recipients[i] = recipients[i + 1];
        }

        recipients[num_of_recipients - 1] = {};
        num_of_recipients--;

        if (auto ret = file::write(file::name_t::RECIPIENTS, {reinterpret_cast<const uint8_t*>(&g_recipients_storage), sizeof(g_recipients_storage)});
            ret != ESP_OK) {
            g_recipients_storage = backup_storage;
            return ret;
        }

        return ESP_OK;
    }

    std::optional<std::span<recipient_t>> get_recipients() {
        if (!g_is_initialized || g_recipients_storage.num_of_recipients == 0) {
            return std::nullopt;
        }
        return std::span{g_recipients_storage.recipients.data(), g_recipients_storage.num_of_recipients};
    }

    esp_err_t set_wifi_creds(std::string_view ssid, std::string_view password) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (ssid.empty() || ssid.length() > wifi::SSID_LEN || password.length() > wifi::PASSWORD_MAX_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        const auto backup = g_wifi_storage;

        g_wifi_storage.creds.ssid.fill('\0');
        g_wifi_storage.creds.password.fill('\0');
        std::copy(ssid.begin(), ssid.end(), g_wifi_storage.creds.ssid.begin());
        std::copy(password.begin(), password.end(), g_wifi_storage.creds.password.begin());
        g_wifi_storage.has_creds = true;

        if (auto ret = file::write(file::name_t::WIFI_CREDS, {reinterpret_cast<const uint8_t*>(&g_wifi_storage), sizeof(g_wifi_storage)});
            ret != ESP_OK) {
            g_wifi_storage = backup;
            return ret;
        }

        return ESP_OK;
    }

    std::optional<wifi_creds_t> get_wifi_creds() {
        if (!g_is_initialized || !g_wifi_storage.has_creds) {
            return std::nullopt;
        }
        return g_wifi_storage.creds;
    }

} // namespace storage
