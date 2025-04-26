#ifndef HTTP_H_
#define HTTP_H_
#include "esp_err.h"
#include <esp_http_server.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

httpd_handle_t start_webserver(QueueHandle_t *out, QueueHandle_t *in, QueueHandle_t *image, TaskHandle_t *tx_task);
esp_err_t stop_webserver(httpd_handle_t server);

#endif