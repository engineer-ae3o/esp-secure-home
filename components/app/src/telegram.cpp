#include "telegram.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_err.h"

#include <array>
#include <cstdio>


namespace telegram {

    namespace {

        constexpr const char* TAG = "Telegram";

        constexpr uint32_t HTTP_TIMEOUT_MS = 10 * 1000;

    } // namespace

    esp_err_t send_message(std::string_view msg, std::string_view chat_id) {
        if (msg.empty() || msg.length() > MAX_MSG_LEN || chat_id.empty() || chat_id.length() > CHAT_ID_LEN) {
            return ESP_ERR_INVALID_SIZE;
        }

        // Build the request URL: https://api.telegram.org/bot<TOKEN>/sendMessage
        std::array<char, 256> url{};
        const int             url_len =
            snprintf(url.data(), url.size(), "https://api.telegram.org/bot%s/sendMessage", static_cast<const char*>(config::TELEGRAM_BOT_TOKEN));
        if (url_len <= 0 || static_cast<size_t>(url_len) >= url.size()) {
            ESP_LOGE(TAG, "Failed to build the Telegram request URL");
            return ESP_ERR_INVALID_SIZE;
        }

        // Build the JSON body. NOTE: this assumes msg never contains " or \ (true for all
        // the canned alert strings this project sends). If you ever pass free-form/user-typed
        // text through here, escape it first or this will produce invalid JSON.
        std::array<char, MAX_MSG_LEN + 128> body{};
        const int                           body_len = snprintf(body.data(),
                                                                body.size(),
                                                                R"({"chat_id":"%.*s","text":"%.*s"})",
                                                                static_cast<int>(chat_id.size()),
                                                                chat_id.data(),
                                                                static_cast<int>(msg.size()),
                                                                msg.data());
        if (body_len <= 0 || static_cast<size_t>(body_len) >= body.size()) {
            ESP_LOGE(TAG, "Failed to build the Telegram request body");
            return ESP_ERR_INVALID_SIZE;
        }

        esp_http_client_config_t http_config = {};
        http_config.url                      = url.data();
        http_config.method                   = HTTP_METHOD_POST;
        http_config.timeout_ms               = HTTP_TIMEOUT_MS;
        http_config.crt_bundle_attach        = esp_crt_bundle_attach; // Built-in CA bundle. No manual cert management needed.

        esp_http_client_handle_t client = esp_http_client_init(&http_config);
        if (client == nullptr) {
            ESP_LOGE(TAG, "Failed to initialize the HTTP client");
            return ESP_ERR_NO_MEM;
        }

        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body.data(), body_len);

        esp_err_t ret = esp_http_client_perform(client);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "HTTP request to Telegram failed: %s", esp_err_to_name(ret));
            esp_http_client_cleanup(client);
            return ret;
        }

        const int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (status != 200) {
            ESP_LOGE(TAG, "Telegram API returned HTTP status %d", status);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Message sent to chat_id %.*s", static_cast<int>(chat_id.size()), chat_id.data());
        return ESP_OK;
    }

} // namespace telegram
