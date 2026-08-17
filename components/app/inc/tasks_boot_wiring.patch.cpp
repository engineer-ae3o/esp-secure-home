// ============================================================================
// INTEGRATION NOTES for tasks.cpp - boot sequence + connectivity task
// Same deal as the other patch file: insertion points into your existing
// tasks.cpp, not a full file.
// ============================================================================

// --- 1. In init_all(), after display::bootup_screen() and before the
//         shutdown handler registration, bring WiFi up and try any saved
//         credentials. This runs once at boot, before any task starts. ---

TRY_WITH_FUNC_VOID(wifi::init(), utils::fatal());

if (auto creds = storage::get_wifi_creds(); creds.has_value()) {
    ESP_LOGI("Init", "Found saved WiFi credentials for \"%s\". Attempting to connect", creds->ssid.data());

    const size_t ssid_len = std::strlen(creds->ssid.data());
    const size_t pw_len   = std::strlen(creds->password.data());

    if (auto ret = wifi::connect({creds->ssid.data(), ssid_len}, {creds->password.data(), pw_len}); ret != ESP_OK) {
        // Deliberately not fatal - a device with no network is still useful
        // locally (keypad, LCD, switches all still work), and "WiFi setup"
        // in the admin menu lets the user fix this without a re-flash.
        ESP_LOGW("Init", "Failed to connect to saved WiFi on boot: %s", esp_err_to_name(ret));
    }
} else {
    ESP_LOGW("Init", "No saved WiFi credentials. Use the admin menu's \"WiFi setup\" to configure one");
}

// --- 2. Replace gsm_task with wifi_task (drop-in shape swap, same
//         consecutive-error-then-reboot pattern you already had) ---

[[noreturn]] void wifi_task(void* arg) {
    constexpr const char* TAG = "WiFi_task";
    ESP_LOGI(TAG, "WiFi_task started");

    // wifi::init() and the initial connect attempt already happened in
    // init_all() before any task was created. This task just periodically
    // checks the connection is still alive - actual reconnect-on-drop is
    // handled inside wifi.cpp's own event handler, this is a backstop in
    // case that reconnect loop itself gets stuck.
    uint32_t consc_err_counter = 0;

    while (true) {
        if (!wifi::is_connected()) {
            ESP_LOGW(TAG, "WiFi not connected");
            consc_err_counter++;
            if (consc_err_counter >= config::MAX_WIFI_CONSC_STATUS_ERRORS) {
                ESP_LOGE(TAG, "WiFi has been down too long. Rebooting to force a clean reconnect attempt");
                utils::reboot();
            }
        } else {
            consc_err_counter = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(config::WIFI_TASK_PERIOD_MS));
    }
}

// --- 3. In run(), replace the gsm_task xTaskCreate call with: ---

ret = xTaskCreate(wifi_task, "wifi_task", config::WIFI_TASK_STACK, nullptr, config::WIFI_TASK_PRIORITY, nullptr);
if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create the wifi task");
    utils::fatal();
}

// --- 4. deinit_all() - swap the gsm::deinit() line for: ---

TRY_THEN_LOG(wifi::deinit(), "Failed to deinitialize WiFi");

// --- 5. Add near the top with your other includes: ---
#include <cstring> // for std::strlen above, if not already included
