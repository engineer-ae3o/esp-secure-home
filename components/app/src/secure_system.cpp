#include "secure_system.hpp"
#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <utility>


namespace crypto {

    namespace {

        // Directory to store the relevant files
        constexpr const char*          DIRECTORY     = "crypto";
        constexpr std::array<char, 20> PSWD_FILE     = {};
        constexpr std::array<char, 20> PNUMBERS_FILE = {};

        constexpr const char* TAG = "Secure_system";

        // Constants representing length of the digest, the salt, authentication tag and nonce lengths in bytes.
        constexpr uint32_t HASH_DIGEST_LEN = 32;
        constexpr uint32_t SALT_LEN        = 32;
        constexpr uint32_t AUTH_TAG_LEN    = 16;
        constexpr uint32_t NONCE_LEN       = 16;

        // Type aliases so I don't go crazy from typing
        using salt_t     = std::array<uint8_t, SALT_LEN>;
        using digest_t   = std::array<uint8_t, HASH_DIGEST_LEN>;
        using nonce_t    = std::array<uint8_t, NONCE_LEN>;
        using auth_tag_t = std::array<uint8_t, AUTH_TAG_LEN>;
        using pnumber_t  = std::array<char, gsm::PHONE_NUMBER_LEN>;
        using pnumbers_t = std::array<pnumber_t, MAX_PNUMBERS>;

        // The salt will be randomly generated at first boot
        // since only one password is being used at a time.
        struct password_file_data_t {
            salt_t   salt{};
            digest_t password_digest{};
        };

        // All the phone numbers are encrypted as a single blob. The nonce is generated, used for
        // encryption, written to flash, on next boot, is fetched, used to decrypt the data, and
        // is discarded after which a new nonce will be generated to prevent reuse.
        struct pnumbers_file_data_t {
            nonce_t    nonce{};
            auth_tag_t auth_tag{};
            // Store a max of config::MAX_PNUMBERS. Track how many are available so far with num_of_pnumbers
            uint32_t   num_of_pnumbers{};
            pnumbers_t pnumbers{};
        };

        // Storage for the data being stored in the files at runtime
        [[maybe_unused]] password_file_data_t g_pswd_file_storage{};
        [[maybe_unused]] pnumbers_file_data_t g_pnumbers_file_storage{};

        // File handles
        FILE* g_pswd_file{};
        FILE* g_pnumbers_file{};

        bool g_is_initialized = false;

        // Helpers
        bool is_first_boot() {
            // Try to open the files in read write in binary mode
            return false;
        }

        void delete_files_and_dir() {
            remove(PSWD_FILE.data());
            remove(PNUMBERS_FILE.data());
        }

        void create_files_and_dir() {
            // Create the directory

            // Create the files
            // NOLINTBEGIN(cppcoreguidelines-owning-memory)
            g_pswd_file = fopen(PSWD_FILE.data(), "w");
            if (g_pswd_file == nullptr) {
                ESP_LOGE(TAG, "Failed to create first file: %s", strerror(errno));
                delete_files_and_dir();
                utils::fatal();
            }

            g_pnumbers_file = fopen(PNUMBERS_FILE.data(), "w");
            if (g_pswd_file == nullptr) {
                ESP_LOGE(TAG, "Failed to create second file: %s", strerror(errno));
                delete_files_and_dir();
                utils::fatal();
            }
            // NOLINTEND(cppcoreguidelines-owning-memory)
        }

        void open_files() {
            // Create the files with the read write access specifies and in binary modes
            // NOLINTBEGIN(cppcoreguidelines-owning-memory)
            g_pswd_file = fopen(PSWD_FILE.data(), "rb+");
            if (g_pswd_file == nullptr) {
                ESP_LOGE(TAG, "Failed to create first file: %s", strerror(errno));
                delete_files_and_dir();
                utils::fatal();
            }

            g_pnumbers_file = fopen(PNUMBERS_FILE.data(), "rb+");
            if (g_pswd_file == nullptr) {
                ESP_LOGE(TAG, "Failed to create second file: %s", strerror(errno));
                delete_files_and_dir();
                utils::fatal();
            }
            // NOLINTEND(cppcoreguidelines-owning-memory)
        }

        void close_files() {
            // NOLINTBEGIN(cppcoreguidelines-owning-memory)
            if (g_pswd_file) {
                if (fclose(g_pswd_file) != 0) {
                    ESP_LOGE(TAG, "Failed to close first file. Flushing instead");
                    fflush(g_pswd_file);
                }
                g_pswd_file = nullptr;
            }
            if (g_pnumbers_file) {
                if (fclose(g_pnumbers_file) != 0) {
                    ESP_LOGE(TAG, "Failed to close second file. Flushing instead");
                    fflush(g_pnumbers_file);
                }
                g_pnumbers_file = nullptr;
            }
            // NOLINTEND(cppcoreguidelines-owning-memory)
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (is_first_boot()) [[unlikely]] {
            create_files_and_dir();
        } else [[likely]] {
            open_files();
        }

        g_is_initialized = true;
        return ESP_OK;
    }

    esp_err_t deinit() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        close_files();
        return ESP_OK;
    }

} // namespace crypto
