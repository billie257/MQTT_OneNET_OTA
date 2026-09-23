#include <stdio.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "mqtt_oneiot.h"
#include "onenet_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

#define TAG "mqtt"

#define  TEST_SSID        "USER_B2867A"
#define  TEST_PASSWORD    "43656597"

//static SemaphoreHandle_t     s_wifi_connect_sem = NULL;
static volatile bool s_wifi_got_ip = false; // 全局标志位

void app_event_handler(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data)
{
    /* =======================WiFi event=========================== */
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "连接断开，1秒后尝试重连...");
                vTaskDelay(pdMS_TO_TICKS(1000)); // 加 1 秒延时，避免疯狂重试
                esp_wifi_connect();
                break;
            default:
                break;
        }
        return;
    }
    /* =======================IP event=========================== */
    if (event_base == IP_EVENT) 
    {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP:
                ESP_LOGI(TAG, "got ip");
                //xSemaphoreGive(s_wifi_connect_sem);
                s_wifi_got_ip = true; // 修改标志位
                break;
            default:
                break;
        }
        return;
    }
}

void app_main(void)
{
    //s_wifi_connect_sem = xSemaphoreCreateBinary();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, app_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, app_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,
        .sta.pmf_cfg.capable = true,
        .sta.pmf_cfg.required = false,
    };
    memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.ssid, TEST_SSID, strlen(TEST_SSID));
    memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    memcpy(wifi_config.sta.password, TEST_PASSWORD, strlen(TEST_PASSWORD));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));  
    ESP_ERROR_CHECK(esp_wifi_start());

    //xSemaphoreTake(s_wifi_connect_sem,portMAX_DELAY);

    // // 等待 Wi-Fi 获取 IP，设置 15 秒超时
    // if (xSemaphoreTake(s_wifi_connect_sem, pdMS_TO_TICKS(15000)) == pdTRUE) {
    //     ESP_LOGI(TAG, "WiFi 获取 IP 成功！");
        
    //     sync_system_time();
    //     mqtt_start();
    //     //ota_timer_init();
    // } else {
    //     ESP_LOGE(TAG, "WiFi 获取 IP 超时！可能是路由器 DHCP 问题，重启设备...");
    //     vTaskDelay(pdMS_TO_TICKS(3000));
    //     esp_restart(); // 超时直接重启，避免死机
    // }
    // 获取当前运行的分区
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI("BOOT", "==============================");
        ESP_LOGI("BOOT", "当前运行分区: %s", running->label); // 会打印 ota_0 或 ota_1
        ESP_LOGI("BOOT", "当前固件版本: %s", running_app_info.version);
        ESP_LOGI("BOOT", "==============================");
    }

    // 轮询等待 IP，最多等待 15 秒 (150 * 100ms)
    ESP_LOGI(TAG, "等待获取 IP 地址...");
    int wait_count = 0;
    while (!s_wifi_got_ip && wait_count < 150) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }

    if (s_wifi_got_ip) {
        ESP_LOGI(TAG, "WiFi 获取 IP 成功，启动后续任务！");
        sync_system_time();
        mqtt_start();
        ota_timer_init();
    } else {
        ESP_LOGE(TAG, "WiFi 获取 IP 超时！重启设备...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
}
