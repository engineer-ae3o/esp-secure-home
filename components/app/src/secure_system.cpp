#include "secure_system.hpp"
#include "sim800l.hpp"
#include "config.hpp"

#include <array>
#include <cstdio>
#include <utility>

namespace crypto {

    namespace {

        // File identifiers for the different files being used
        enum class file_name_t : uint8_t {
            COUNTER,  // Stores the boot cycle counter
            PASSWORD, // Stores the hashed password and salt
            PNUMBERS, // Stores the encrypted password blob
            COUNT,    // Keep track of the number of values in this enum
        };

        // Lookup table for the files being used
        constexpr std::array<const char*, std::to_underlying(file_name_t::COUNT)> FILE_LUT = {{
            // Add a little bit of obfuscation to the file names since they get stored directly in
            // flash. Besides, they will be accessed with their more readable enum counterparts.
            // Doesn't do a whole lot in the grand scheme of things, but still, doesn't hurt.
            [std::to_underlying(file_name_t::COUNTER)]  = "/lfs/getds/fchdvqv",
            [std::to_underlying(file_name_t::PASSWORD)] = "/lfs/getds/yacnywo",
            [std::to_underlying(file_name_t::PNUMBERS)] = "/lfs/getds/cqwogto",
        }};

        // Counter in flash counting number of boot cycles
        uint32_t g_boot_cycle_counter{};

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
        using pnumbers_t = std::array<pnumber_t, config::MAX_PNUMBERS>;

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

        bool g_is_initialized = false;

        // Helpers
        bool is_first_boot() {
            return false;
        }

        void on_first_boot() {
        }

        void cleanup() {
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (is_first_boot()) [[unlikely]] {
            on_first_boot();
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

} // namespace crypto
