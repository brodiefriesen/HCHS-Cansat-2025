#ifndef WIFI_H_
#define WIFI_H_
#include <esp_err.h>
#include <esp_http_server.h>

#define ESP_WIFI_SSID      "CEREALS Ground Station" //CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      "skibiditoilet" //CONFIG_ESP_WIFI_PASSWORD
#define MAX_STA_CONN        5 //CONFIG_ESP_MAX_STA_CONN

esp_err_t wifi_init_softap(void);

#endif
