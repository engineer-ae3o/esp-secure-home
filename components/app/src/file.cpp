#include "config.hpp"
#include "utils.hpp"
#include "file.hpp"

#include "esp_log.h"
#include "esp_err.h"

#include <cerrno>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <utility>
#include <unistd.h>
#include <sys/stat.h>

namespace file {

    namespace {

        constexpr const char* TAG = "File";

        constexpr char DIR_NAME[] = "storage";

        constexpr auto STORAGE_DIR_PATH     = utils::concat(config::FILESYSTEM_BASE_PATH, "/", DIR_NAME);
        constexpr auto PSWD_FILE_PATH       = utils::concat(config::FILESYSTEM_BASE_PATH, "/", DIR_NAME, "/", "password.txt");
        constexpr auto RECIPIENTS_FILE_PATH = utils::concat(config::FILESYSTEM_BASE_PATH, "/", DIR_NAME, "/", "recipients.txt");
        constexpr auto WIFI_CREDS_FILE_PATH = utils::concat(config::FILESYSTEM_BASE_PATH, "/", DIR_NAME, "/", "wifi_creds.txt");
        constexpr auto SENTINEL_FILE_PATH   = utils::concat(config::FILESYSTEM_BASE_PATH, "/", "sentinel.txt");

        FILE* g_pswd_file{};
        FILE* g_recipients_file{};
        FILE* g_wifi_creds_file{};

        std::pair<FILE*, const char*> get_file_handle(name_t file) {
            switch (file) {
                case name_t::RECIPIENTS:
                    return {g_recipients_file, RECIPIENTS_FILE_PATH.data()};
                case name_t::PSWD:
                    return {g_pswd_file, PSWD_FILE_PATH.data()};
                case name_t::WIFI_CREDS:
                    return {g_wifi_creds_file, WIFI_CREDS_FILE_PATH.data()};
                default:
                    return {nullptr, nullptr};
            }
        }

    } // namespace

    void open() {
        // NOLINTBEGIN(cppcoreguidelines-owning-memory)

        g_pswd_file = fopen(PSWD_FILE_PATH.data(), "rb+");
        if (g_pswd_file == nullptr) {
            ESP_LOGE(TAG, "Failed to open %s: %s", PSWD_FILE_PATH.data(), strerror(errno));
            utils::fatal();
        }

        g_recipients_file = fopen(RECIPIENTS_FILE_PATH.data(), "rb+");
        if (g_recipients_file == nullptr) {
            ESP_LOGE(TAG, "Failed to open %s: %s", RECIPIENTS_FILE_PATH.data(), strerror(errno));
            fclose(g_pswd_file);
            g_pswd_file = nullptr;
            utils::fatal();
        }

        g_wifi_creds_file = fopen(WIFI_CREDS_FILE_PATH.data(), "rb+");
        if (g_wifi_creds_file == nullptr) {
            ESP_LOGE(TAG, "Failed to open %s: %s", WIFI_CREDS_FILE_PATH.data(), strerror(errno));
            fclose(g_pswd_file);
            g_pswd_file = nullptr;
            fclose(g_recipients_file);
            g_recipients_file = nullptr;
            utils::fatal();
        }

        // NOLINTEND(cppcoreguidelines-owning-memory)
    }

    void close() {
        // NOLINTBEGIN(cppcoreguidelines-owning-memory)

        if (g_pswd_file) {
            if (fclose(g_pswd_file) != 0) {
                ESP_LOGE(TAG, "Failed to close %s: %s", PSWD_FILE_PATH.data(), strerror(errno));
            }
            g_pswd_file = nullptr;
        }

        if (g_recipients_file) {
            if (fclose(g_recipients_file) != 0) {
                ESP_LOGE(TAG, "Failed to close %s: %s", RECIPIENTS_FILE_PATH.data(), strerror(errno));
            }
            g_recipients_file = nullptr;
        }

        if (g_wifi_creds_file) {
            if (fclose(g_wifi_creds_file) != 0) {
                ESP_LOGE(TAG, "Failed to close %s: %s", WIFI_CREDS_FILE_PATH.data(), strerror(errno));
            }
            g_wifi_creds_file = nullptr;
        }

        // NOLINTEND(cppcoreguidelines-owning-memory)
    }

    bool is_first_boot() {
        // NOLINTBEGIN(cppcoreguidelines-owning-memory)

        FILE* sentinel = fopen(SENTINEL_FILE_PATH.data(), "r");
        if (sentinel) [[likely]] {
            fclose(sentinel);
            return false;
        }

        if (errno != ENOENT) {
            ESP_LOGE(TAG, "Failed to check the sentinel file %s: %s", SENTINEL_FILE_PATH.data(), strerror(errno));
            utils::fatal();
        }

        sentinel = fopen(SENTINEL_FILE_PATH.data(), "w");
        if (sentinel == nullptr) {
            ESP_LOGE(TAG, "Failed to create the sentinel file %s: %s", SENTINEL_FILE_PATH.data(), strerror(errno));
            utils::fatal();
        }

        if (fflush(sentinel) != 0 || fsync(fileno(sentinel)) != 0) {
            ESP_LOGE(TAG, "Failed to flush the sentinel file");
        }

        if (fclose(sentinel) != 0) {
            ESP_LOGE(TAG, "Failed to close the sentinel file %s: %s", SENTINEL_FILE_PATH.data(), strerror(errno));
        }

        // NOLINTEND(cppcoreguidelines-owning-memory)
        return true;
    }

    void create() {
        // NOLINTBEGIN(cppcoreguidelines-owning-memory)

        if (mkdir(STORAGE_DIR_PATH.data(), 0755) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "Failed to create directory %s: %s", STORAGE_DIR_PATH.data(), strerror(errno));
            utils::fatal();
        }

        g_pswd_file = fopen(PSWD_FILE_PATH.data(), "wb+");
        if (g_pswd_file == nullptr) {
            ESP_LOGE(TAG, "Failed to create %s: %s", PSWD_FILE_PATH.data(), strerror(errno));
            rmdir(STORAGE_DIR_PATH.data());
            utils::fatal();
        }

        g_recipients_file = fopen(RECIPIENTS_FILE_PATH.data(), "wb+");
        if (g_recipients_file == nullptr) {
            ESP_LOGE(TAG, "Failed to create %s: %s", RECIPIENTS_FILE_PATH.data(), strerror(errno));
            fclose(g_pswd_file);
            g_pswd_file = nullptr;
            remove(PSWD_FILE_PATH.data());
            rmdir(STORAGE_DIR_PATH.data());
            utils::fatal();
        }

        g_wifi_creds_file = fopen(WIFI_CREDS_FILE_PATH.data(), "wb+");
        if (g_wifi_creds_file == nullptr) {
            ESP_LOGE(TAG, "Failed to create %s: %s", WIFI_CREDS_FILE_PATH.data(), strerror(errno));
            fclose(g_pswd_file);
            g_pswd_file = nullptr;
            fclose(g_recipients_file);
            g_recipients_file = nullptr;
            remove(PSWD_FILE_PATH.data());
            remove(RECIPIENTS_FILE_PATH.data());
            rmdir(STORAGE_DIR_PATH.data());
            utils::fatal();
        }

        // NOLINTEND(cppcoreguidelines-owning-memory)
    }

    esp_err_t write(name_t file, std::span<const uint8_t> buf) {
        if (buf.empty() || buf.data() == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }

        auto [handle, path] = get_file_handle(file);
        if (handle == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }

        if (fseek(handle, 0, SEEK_SET) != 0) {
            ESP_LOGE(TAG, "Failed to seek to beginning of %s: %s", path, strerror(errno));
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (fwrite(buf.data(), 1, buf.size_bytes(), handle) != buf.size_bytes()) {
            ESP_LOGE(TAG, "Failed to write buffer to %s: %s", path, strerror(errno));
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (fflush(handle) != 0) {
            ESP_LOGE(TAG, "Failed to flush %s: %s", path, strerror(errno));
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (fsync(fileno(handle)) != 0) {
            ESP_LOGE(TAG, "Failed to sync to flash %s: %s", path, strerror(errno));
            return ESP_ERR_INVALID_RESPONSE;
        }

        ESP_LOGI(TAG, "Wrote %zu bytes to %s", buf.size_bytes(), path);
        return ESP_OK;
    }

    esp_err_t read(name_t file, std::span<uint8_t> buf) {
        if (buf.empty() || buf.data() == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }

        auto [handle, path] = get_file_handle(file);
        if (handle == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }

        if (fseek(handle, 0, SEEK_SET) != 0) {
            ESP_LOGE(TAG, "Failed to seek to beginning of %s: %s", path, strerror(errno));
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (const size_t bytes_read = fread(buf.data(), 1, buf.size(), handle); bytes_read != buf.size()) {
            if (ferror(handle)) {
                ESP_LOGE(TAG, "Failed to read %s: %s", path, strerror(errno));
                clearerr(handle);
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (feof(handle)) {
                ESP_LOGE(TAG, "EOF reached prematurely on %s (read %zu of %zu bytes)", path, bytes_read, buf.size());
                clearerr(handle);
                return ESP_ERR_INVALID_SIZE;
            }
            return ESP_ERR_INVALID_RESPONSE;
        }

        return ESP_OK;
    }

} // namespace file
