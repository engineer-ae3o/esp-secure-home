#include "secure_system.hpp"
#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "file.hpp"

#include <array>
#include <utility>


namespace crypto {

    namespace {

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
        struct pswd_file_data_t {
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
        [[maybe_unused]] pswd_file_data_t     g_pswd_file_storage{};
        [[maybe_unused]] pnumbers_file_data_t g_pnumbers_file_storage{};

        bool g_is_initialized = false;

    } // namespace

    // Helpers
    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (file::is_first_boot()) [[unlikely]] {
            file::create();
        } else [[likely]] {
            file::open();
        }

        g_is_initialized = true;
        return ESP_OK;
    }

    esp_err_t deinit() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        file::close();
        return ESP_OK;
    }

} // namespace crypto
