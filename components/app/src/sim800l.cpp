#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portmacro.h"

#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_modem_api.h"
#include "esp_netif_types.h"
#include "esp_modem_config.h"
#include "esp_netif_defaults.h"
#include "esp_modem_c_api_types.h"

#include <array>
#include <cstring>
#include <utility>
#include <string_view>


namespace gsm {

    namespace {

        constexpr const char* TAG = "GSM";

        bool g_is_initialized = false;

        esp_modem_dce_t* g_dce_handle{};
        esp_netif_t*     g_esp_netif{};

        SemaphoreHandle_t g_gsm_mutex{};
        StaticSemaphore_t g_gsm_mutex_stack{};

        // RAII helper for taking and freeing the mutex
        struct scoped_mutex_t {
        public:
            scoped_mutex_t() {
                xSemaphoreTake(g_gsm_mutex, pdMS_TO_TICKS(portMAX_DELAY));
            }

            ~scoped_mutex_t() {
                xSemaphoreGive(g_gsm_mutex);
            }

            scoped_mutex_t(const scoped_mutex_t&)            = delete;
            scoped_mutex_t& operator=(const scoped_mutex_t&) = delete;
            scoped_mutex_t(scoped_mutex_t&&)                 = delete;
            scoped_mutex_t& operator=(scoped_mutex_t&&)      = delete;
        };

        void cleanup() {
            if (g_dce_handle) {
                esp_modem_destroy(g_dce_handle);
                g_dce_handle = nullptr;
            }
            if (g_esp_netif) {
                esp_netif_destroy(g_esp_netif);
                g_esp_netif = nullptr;
            }
            TRY_THEN_LOG(esp_event_loop_delete_default(), "Failed to remove the system event loop");
            // TRY_THEN_LOG(esp_netif_deinit(),""); // Not supported yet
            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        TRY(esp_event_loop_create_default());
        TRY(esp_netif_init());

        // Initialize the network interface
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
        dte_config.uart_config.baud_rate  = config::GSM_BAUD_RATE;

        // DCE configuation
        constexpr esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(static_cast<const char*>(config::GLO_APN));

        // Initialize the SIM800L
        g_dce_handle = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_config, &dce_config, g_esp_netif);
        if (g_dce_handle == nullptr) {
            ESP_LOGE(TAG, "Failed to create instance of the SIM800L");
            cleanup();
            return ESP_ERR_NO_MEM;
        }

        // Sync retry loop before reading IMSI
        constexpr uint32_t MAX_RETRIES = 5;

        bool synced = false;

        for (uint32_t i = 0; i < MAX_RETRIES; i++) {
            if (esp_modem_sync(g_dce_handle) == ESP_OK) {
                synced = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500)); // Wait 500ms between sync attempts
        }

        if (!synced) {
            ESP_LOGE(TAG, "SIM800L failed to respond to AT sync");
            cleanup();
            return ESP_ERR_TIMEOUT;
        }

        // Read the IMSI
        std::array<char, CONFIG_ESP_MODEM_C_API_STR_MAX> imsi{};
        if (auto ret = esp_modem_get_imsi(g_dce_handle, imsi.data()); ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read the IMSI of the SIM card: %s", esp_err_to_name(ret));
            cleanup();
            return ret;
        }

        // Read the IMEI
        std::array<char, CONFIG_ESP_MODEM_C_API_STR_MAX> imei{};
        if (auto ret = esp_modem_get_imei(g_dce_handle, imei.data()); ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read the IMEI of the SIM800L: %s", esp_err_to_name(ret));
            cleanup();
            return ret;
        }

        ESP_LOGI(TAG, "IMSI of the SIM Card: %.*s", imsi.size(), imsi.data());
        ESP_LOGI(TAG, "IMEI of the SIM800L: %.*s", imei.size(), imei.data());

        g_gsm_mutex = xSemaphoreCreateMutexStatic(&g_gsm_mutex_stack);

        g_is_initialized = true;
        return ESP_OK;
    }

    esp_err_t deinit() {
        // Take the mutex to ensure the cleanup is thread safe and no other thread holds the mutex as it is about to be deleted
        {
            [[maybe_unused]] scoped_mutex_t scoped_mutex;
            if (!g_is_initialized) {
                return ESP_ERR_INVALID_STATE;
            }
            cleanup();
        }

        // Delete the mutex only after cleaning other resources
        if (g_gsm_mutex) {
            vSemaphoreDelete(g_gsm_mutex);
            g_gsm_mutex_stack = {};
            g_gsm_mutex       = nullptr;
        }

        return ESP_OK;
    }

    esp_err_t get_sim_status() {
        [[maybe_unused]] scoped_mutex_t scoped_mutex;

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
            ESP_LOGW(TAG, "SIM card present but requires unlocking. State = %d", std::to_underlying(pin_state));
            return ESP_FAIL;
        }

        return ESP_OK;
    }

    esp_err_t send_sms(std::string_view sms, std::string_view pnumber) {
        [[maybe_unused]] scoped_mutex_t scoped_mutex;

        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        if (sms.length() > MAX_SMS_LEN || sms.empty() || pnumber.length() != PHONE_NUMBER_LEN) {
            return ESP_ERR_INVALID_SIZE;
        }

        // check the SIM card's status before proceeding to ensure it is still alive
        if (auto ret = get_sim_status(); ret != ESP_OK) {
            return ret;
        }

        // Temporary storage here since the sms API takes in a null terminated string and it is
        // not guaranteed that the underlying data from the string views are null terminated.
        std::array<char, MAX_SMS_LEN + 1>      sms_c_buf     = {};
        std::array<char, PHONE_NUMBER_LEN + 1> pnumber_c_buf = {};

        memcpy(sms_c_buf.data(), sms.data(), sms.length());
        memcpy(pnumber_c_buf.data(), pnumber.data(), PHONE_NUMBER_LEN);

        // Send the SMS
        TRY(esp_modem_sms_txt_mode(g_dce_handle, true));
        TRY(esp_modem_sms_character_set(g_dce_handle));
        TRY(esp_modem_send_sms(g_dce_handle, pnumber_c_buf.data(), sms_c_buf.data()));
        TRY(esp_modem_sms_txt_mode(g_dce_handle, false));

        return ESP_OK;
    }

} // namespace gsm
