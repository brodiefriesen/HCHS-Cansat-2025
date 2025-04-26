#include "http.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include <string.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <sys/param.h>

static const char *TAG = "http";

// Global queues for incoming/outgoing messages
static QueueHandle_t *outgoing;
static QueueHandle_t *incoming;
static QueueHandle_t *image_out;

static TaskHandle_t *tx_task_handle;

// Buffer to store the latest received LoRa message
static char latest_message[290] = "No data received";

// ------------------------- STATUS ENDPOINT -------------------------
static esp_err_t status_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Status requested");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, ":) please poll sse endpoint or latest for data", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t status = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = status_get_handler,
    .user_ctx  = NULL
};

// ------------------------- SSE ENDPOINT -------------------------
static esp_err_t sse_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    char in[280];

    while (1) {
        if (xQueueReceive(*incoming, (void *)&in, 0) == pdTRUE) {
            snprintf(latest_message, sizeof(latest_message), "%s", in); // Store latest message
            char sse_data[290];
            int len = snprintf(sse_data, sizeof(sse_data), "data: %s\n\n", in);
            if (httpd_resp_send_chunk(req, sse_data, len) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send SSE data");
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Send data every 500ms
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t sse = {
    .uri       = "/sse",
    .method    = HTTP_GET,
    .handler   = sse_handler,
    .user_ctx  = NULL
};

// ------------------------- SSE2 ENDPOINT -------------------------
static esp_err_t sse2_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    char in[110];

    while (1) {
        if (xQueueReceive(*image_out, (void *)&in, 0) == pdTRUE) {
            snprintf(latest_message, sizeof(latest_message), "%s", in); // Store latest message
            char sse_data[130];
            int len = snprintf(sse_data, sizeof(sse_data), "data: %s\n\n", in);
            if (httpd_resp_send_chunk(req, sse_data, len) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send SSE data");
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Send data every 500ms
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(50));
}

static const httpd_uri_t sse2 = {
    .uri       = "/sse2",
    .method    = HTTP_GET,
    .handler   = sse_handler,
    .user_ctx  = NULL
};

// ------------------------- LATEST MESSAGE ENDPOINT -------------------------
static esp_err_t latest_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, latest_message, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t latest = {
    .uri       = "/latest",
    .method    = HTTP_GET,
    .handler   = latest_get_handler,
    .user_ctx  = NULL
};

// ------------------------- INSTRUCTION ENDPOINT -------------------------
static esp_err_t instruction_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, remaining = req->content_len;

    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf) - 1))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }

        buf[ret] = '\0'; // Null-terminate received data
        ESP_LOGI(TAG, "Received instruction: %s", buf);

        // Prepare a safe copy
        char temp[200];
        strncpy(temp, buf, sizeof(temp));
        temp[sizeof(temp) - 1] = '\0'; // Ensure null-termination

        // Queue the message for later transmission
        if (xQueueSend(*outgoing, (void *)temp, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGE(TAG, "Outgoing queue full! Dropping message.");
        } else {
            ESP_LOGI(TAG, "Message added to outgoing queue.");

            // Wake up tx_task to immediately process the queue
            xTaskNotifyGive(*tx_task_handle); // tx_task_handle is the task handle for tx_task
        }

        remaining -= ret;
    }

    httpd_resp_send(req, "ACK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}




static const httpd_uri_t instruction = {
    .uri       = "/instruction",
    .method    = HTTP_POST,
    .handler   = instruction_post_handler,
    .user_ctx  = NULL
};

// ------------------------- WEB SERVER START/STOP -------------------------
httpd_handle_t start_webserver(QueueHandle_t *out, QueueHandle_t *in, QueueHandle_t *image, TaskHandle_t *tx_task)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    outgoing = out;
    incoming = in;
    image_out = image;
    tx_task_handle=tx_task;

    ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &status);
        httpd_register_uri_handler(server, &sse);
        httpd_register_uri_handler(server, &sse2);
        httpd_register_uri_handler(server, &latest);
        httpd_register_uri_handler(server, &instruction);
        return server;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return NULL;
}

esp_err_t stop_webserver(httpd_handle_t server)
{
    return httpd_stop(server);
}

// ------------------------- NETWORK EVENT HANDLERS -------------------------
static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop HTTP server");
        }
    }
}
