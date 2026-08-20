#include "telegram.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include <array>
#include <cstdio>


namespace telegram {

    namespace {

        constexpr const char* TAG = "Telegram";

        constexpr uint32_t HTTP_TIMEOUT_MS   = 10 * 1000;
        constexpr uint32_t HTTP_SUCCESS_CODE = 200;

    } // namespace

    esp_err_t send_message(std::string_view msg, std::string_view chat_id) {
        if (msg.empty() || msg.length() > MAX_MSG_LEN || chat_id.empty() || chat_id.length() > CHAT_ID_LEN) {
            return ESP_ERR_INVALID_SIZE;
        }

        // Build the request URL: https://api.telegram.org/bot<TOKEN>/sendMessage
        constexpr auto url = utils::concat("https://api.telegram.org/bot", config::TELEGRAM_BOT_TOKEN, "/sendMessage");

        // Build the JSON body.
        std::array<char, MAX_MSG_LEN + 128> body{};

        const int body_length =
            snprintf(body.data(), body.size(), R"({"chat_id":"%.*s","text":"%.*s"})", chat_id.size(), chat_id.data(), msg.size(), msg.data());
        if (body_length <= 0 || static_cast<size_t>(body_length) >= body.size()) {
            ESP_LOGE(TAG, "Failed to build the telegram request body");
            return ESP_ERR_INVALID_SIZE;
        }

        esp_http_client_config_t http_config = {};
        http_config.url                      = url.data();
        http_config.method                   = HTTP_METHOD_POST;
        http_config.timeout_ms               = HTTP_TIMEOUT_MS;
        http_config.crt_bundle_attach        = esp_crt_bundle_attach;

        auto* client = esp_http_client_init(&http_config);
        if (client == nullptr) {
            ESP_LOGE(TAG, "Failed to initialize the HTTP client");
            return ESP_ERR_NO_MEM;
        }

        auto cleanup = [&] {
            esp_http_client_cleanup(client);
        };

        TRY_WITH_FUNC(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup());
        TRY_WITH_FUNC(esp_http_client_set_post_field(client, body.data(), body_length), cleanup());
        TRY_WITH_FUNC(esp_http_client_perform(client), cleanup());

        const int status = esp_http_client_get_status_code(client);
        cleanup();

        if (status != HTTP_SUCCESS_CODE) {
            ESP_LOGE(TAG, "Telegram API returned HTTP status %d", status);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Message sent to chat_id %.*s", chat_id.size(), chat_id.data());
        return ESP_OK;
    }

} // namespace telegram
