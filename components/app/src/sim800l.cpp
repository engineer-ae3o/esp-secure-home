#include "sim800l.hpp"


namespace gsm {

    namespace {

        imsi_t g_imsi{};
        bool   g_is_initialized = false;

        void cleanup() {
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
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

    esp_err_t get_sim_status() {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        return ESP_OK;
    }

    esp_err_t send_sms(std::string_view sms, std::string_view number, bool check_sim_status) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (sms.length() > MAX_SMS_LEN || sms.empty() || number.length() != PHONE_NUMBER_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        return ESP_OK;
    }

    std::expected<imsi_t, esp_err_t> get_imsi() {
        if (!g_is_initialized) {
            return std::unexpected(ESP_ERR_INVALID_STATE);
        }
        return g_imsi;
    }

} // namespace gsm
