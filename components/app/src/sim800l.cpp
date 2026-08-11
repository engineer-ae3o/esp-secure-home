#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_modem_api.h"
#include "esp_netif_types.h"
#include "esp_modem_config.h"
#include "esp_netif_defaults.h"
#include "esp_modem_c_api_types.h"

#include <cstring>


namespace gsm {

    namespace {

        constexpr const char* TAG = "GSM";

        imsi_t g_imsi{};
        bool   g_is_initialized = false;

        esp_modem_dce_t* g_dce_handle{};
        esp_netif_t*     g_esp_netif{};

        void cleanup() {
            if (g_dce_handle) {
                esp_modem_destroy(g_dce_handle);
                g_dce_handle = nullptr;
            }
            if (g_esp_netif) {
                esp_netif_destroy(g_esp_netif);
                g_esp_netif = nullptr;
            }
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        // Initialize the network inrterface
        const esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();

        g_esp_netif = esp_netif_new(&netif_ppp_config);
        if (g_esp_netif == nullptr) {
            ESP_LOGE(TAG, "Failed to create esp_netif instance");
            return ESP_ERR_NO_MEM;
        }

        // DTE configuration
        esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
        dte_config.uart_config.tx_io_num  = config::GSM_GPIO_TX_PIN;
        dte_config.uart_config.rx_io_num  = config::GSM_GPIO_RX_PIN;
        dte_config.uart_config.baud_rate  = config::GSM_BAUDRATE;

        // DCE configuation
        constexpr esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(config::GLO_APN);

        // Initialize the SIM800L
        g_dce_handle = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_config, &dce_config, g_esp_netif);
        if (g_dce_handle == nullptr) {
            ESP_LOGE(TAG, "Failed to create instance of the SIM800L");
            cleanup();
            return ESP_ERR_NO_MEM;
        }

        // Get the IMSI
        if (auto ret = esp_modem_get_imei(g_dce_handle, g_imsi.data()); ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read the IMSI: %s", esp_err_to_name(ret));
            cleanup();
            return ret;
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

        esp_modem_sim_pin_state_t pin_state{};

        auto ret = esp_modem_read_pin_state(g_dce_handle, &pin_state);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SIM800L not responding: %s", esp_err_to_name(ret));
            return ret;
        } else if (pin_state == ESP_MODEM_SIM_PIN_STATE_READY) {
            ESP_LOGI(TAG, "SIM card present and ready");
        } else {
            ESP_LOGW(TAG, "SIM card present but requires unlocking. State = %d", pin_state);
            return ESP_FAIL;
        }

        return ESP_OK;
    }

    esp_err_t send_sms(const char* sms, const char* number, bool check_sim_status) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (sms == nullptr || strlen(sms) > MAX_SMS_LEN || strlen(sms) == 0 || number == nullptr || strlen(number) != PHONE_NUMBER_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        if (check_sim_status) {
            if (auto ret = get_sim_status(); ret != ESP_OK) {
                return ret;
            }
        }

        // Send the SMS
        TRY(esp_modem_sms_txt_mode(g_dce_handle, true));
        TRY(esp_modem_sms_character_set(g_dce_handle));
        TRY(esp_modem_send_sms(g_dce_handle, number, sms));
        TRY(esp_modem_sms_txt_mode(g_dce_handle, false));

        return ESP_OK;
    }

    std::expected<imsi_t, esp_err_t> get_imsi() {
        if (!g_is_initialized) {
            return std::unexpected(ESP_ERR_INVALID_STATE);
        }
        return g_imsi;
    }

} // namespace gsm
