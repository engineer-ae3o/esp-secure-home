#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "wifi.hpp"
#include "utils.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include <cstring>
#include <algorithm>


namespace wifi {

    namespace {

        constexpr const char* TAG = "WiFi";

        constexpr EventBits_t CONNECTED_BIT = BIT0;
        constexpr EventBits_t FAIL_BIT      = BIT1;

        constexpr uint32_t CONNECT_TIMEOUT_MS = 15 * 1000;

        bool g_is_initialized = false;

        esp_netif_t* g_netif{};

        EventGroupHandle_t g_event_group{};
        StaticEventGroup_t g_event_group_buf{};

        // Remembered so a disconnect (AP reboot, temporary signal loss, etc.)
        // can be auto-retried without the user re-entering anything.
        std::array<char, SSID_LEN + 1>         g_last_ssid{};
        std::array<char, PASSWORD_MAX_LEN + 1> g_last_password{};
        bool                                   g_have_creds = false;

        void do_connect() {
            wifi_config_t sta_config{};
            std::strncpy(reinterpret_cast<char*>(sta_config.sta.ssid), g_last_ssid.data(), sizeof(sta_config.sta.ssid) - 1);
            std::strncpy(reinterpret_cast<char*>(sta_config.sta.password), g_last_password.data(), sizeof(sta_config.sta.password) - 1);
            sta_config.sta.threshold.authmode = g_last_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

            TRY_THEN_LOG(esp_wifi_set_config(WIFI_IF_STA, &sta_config), "Failed to set WiFi station config");
            TRY_THEN_LOG(esp_wifi_connect(), "Failed to start WiFi connect");
        }

        void event_handler(void* /*arg*/, esp_event_base_t event_base, int32_t event_id, void* /*event_data*/) {
            if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
                xEventGroupClearBits(g_event_group, CONNECTED_BIT);
                if (g_have_creds) {
                    ESP_LOGW(TAG, "Disconnected from AP. Reconnecting with last known credentials");
                    esp_wifi_connect();
                } else {
                    ESP_LOGW(TAG, "Disconnected and no credentials on file. Not auto-reconnecting");
                    xEventGroupSetBits(g_event_group, FAIL_BIT);
                }
            } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
                ESP_LOGI(TAG, "Connected. Got IP address");
                xEventGroupClearBits(g_event_group, FAIL_BIT);
                xEventGroupSetBits(g_event_group, CONNECTED_BIT);
            }
        }

        void cleanup() {
            esp_wifi_stop();
            esp_wifi_deinit();

            TRY_THEN_LOG(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler), "Failed to unregister WIFI_EVENT handler");
            TRY_THEN_LOG(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler), "Failed to unregister IP_EVENT handler");

            if (g_netif) {
                esp_netif_destroy_default_wifi(g_netif);
                g_netif = nullptr;
            }

            g_is_initialized = false;
        }

    } // namespace

    esp_err_t init() {
        if (g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        g_event_group = xEventGroupCreateStatic(&g_event_group_buf);

        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            TRY_WITH_FUNC(nvs_flash_erase(), cleanup());
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize the nvs flash: %s", esp_err_to_name(ret));
            cleanup();
            return ret;
        }

        TRY_WITH_FUNC(esp_netif_init(), cleanup());
        TRY_WITH_FUNC(esp_event_loop_create_default(), cleanup());

        g_netif = esp_netif_create_default_wifi_sta();
        if (g_netif == nullptr) {
            ESP_LOGE(TAG, "Failed to create the default WiFi station netif");
            return ESP_ERR_NO_MEM;
        }

        const wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        TRY_WITH_FUNC(esp_wifi_init(&wifi_init_cfg), cleanup());

        TRY_WITH_FUNC(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr), cleanup());
        TRY_WITH_FUNC(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr), cleanup());

        TRY_WITH_FUNC(esp_wifi_set_mode(WIFI_MODE_STA), cleanup());
        TRY_WITH_FUNC(esp_wifi_start(), cleanup());

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

    bool is_connected() {
        if (!g_is_initialized || !g_event_group) {
            return false;
        }
        return (xEventGroupGetBits(g_event_group) & CONNECTED_BIT) != 0;
    }

    esp_err_t scan(std::array<ap_info_t, MAX_SCAN_RESULTS>& out, size_t& count) {
        if (!g_is_initialized) {
            return ESP_ERR_INVALID_STATE;
        }

        constexpr wifi_scan_config_t scan_config = {};
        TRY(esp_wifi_scan_start(&scan_config, true)); // Blocking scan

        uint16_t ap_num = 0;
        TRY(esp_wifi_scan_get_ap_num(&ap_num));

        std::array<wifi_ap_record_t, MAX_SCAN_RESULTS> raw_records{};
        uint16_t                                       to_fetch = std::min<uint16_t>(ap_num, MAX_SCAN_RESULTS);
        TRY(esp_wifi_scan_get_ap_records(&to_fetch, raw_records.data()));

        count = to_fetch;
        for (size_t i = 0; i < count; i++) {
            std::strncpy(out[i].ssid.data(), reinterpret_cast<const char*>(raw_records[i].ssid), out[i].ssid.size() - 1);
            out[i].rssi     = raw_records[i].rssi;
            out[i].authmode = raw_records[i].authmode;
        }

        ESP_LOGI(TAG, "Scan found %zu network(s)", count);
        return ESP_OK;
    }

    esp_err_t connect(std::string_view ssid, std::string_view password) {
        if (!g_is_initialized || ssid.empty() || ssid.length() > SSID_LEN || password.length() > PASSWORD_MAX_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        g_last_ssid.fill('\0');
        g_last_password.fill('\0');
        std::ranges::copy(ssid, g_last_ssid.begin());
        std::ranges::copy(password, g_last_password.begin());
        g_have_creds = true;

        xEventGroupClearBits(g_event_group, CONNECTED_BIT | FAIL_BIT);
        do_connect();

        const EventBits_t bits = xEventGroupWaitBits(g_event_group, CONNECTED_BIT | FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

        if (!(bits & CONNECTED_BIT)) {
            ESP_LOGE(TAG, "Failed to connect to \"%.*s\" within %lu ms", static_cast<int>(ssid.size()), ssid.data(), CONNECT_TIMEOUT_MS);
            g_have_creds = false; // Don't auto-retry bad credentials forever
            return ESP_ERR_TIMEOUT;
        }

        return ESP_OK;
    }

} // namespace wifi
