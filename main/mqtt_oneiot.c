#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_oneiot.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "onenet_dm.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define TAG     "onenet"

static const char *TAG1 = "SNTP";

static esp_mqtt_client_handle_t client = NULL;
static char s_is_mqtt_connected = 0;

EventGroupHandle_t s_mqtt_event_group;
extern const char *g_oneiot_ca;

void sync_system_time(void) {
    ESP_LOGI(TAG1, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // 配置国内可用的 NTP 服务器
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_init();

    // 等待时间同步完成（年份需大于 2024）
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;

    while (timeinfo.tm_year < (2024 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG1, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (retry < retry_count) {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG1, "Current time synced: %s", strftime_buf);
    } else {
        ESP_LOGE(TAG1, "Time sync timeout! TLS validation might fail.");
    }
}

// 处理云端下发的属性控制
static void handle_property_set(esp_mqtt_client_handle_t client, const char* data, int data_len)
{
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if(!root)
    {
        ESP_LOGI(TAG, "JSON parse error");
        return;
    }
    // 提取消息ID
    cJSON *id = cJSON_GetObjectItem(root, "id");
    char msg_id[13] = {0};
    if(cJSON_IsString(id))
    {
        strncpy(msg_id, id->valuestring, sizeof(msg_id) - 1);
    }
    else if (cJSON_IsNumber(id))
    {
        snprintf(msg_id, sizeof(msg_id), "%d", id->valueint);
    }
    // 解析具体的属性参数
    cJSON *params = cJSON_GetObjectItem(root, "params");
    if(params)
    {
        cJSON *light_switch = cJSON_GetObjectItem(params, "LightSwitch");
        if (light_switch && cJSON_IsBool(light_switch))
        {
            bool is_on = cJSON_IsTrue(light_switch);
            ESP_LOGI(TAG, "Set LightSwitch: %s", is_on ? "ON" : "OFF");
            // TODO 这里控制真实硬件GPIO输出高低电平
        }
        cJSON *r = cJSON_GetObjectItem(params, "r");
        cJSON *g = cJSON_GetObjectItem(params, "g");
        cJSON *b = cJSON_GetObjectItem(params, "b");
        if (r && g && b)
        {
            ESP_LOGI(TAG, "Set Color RGB: (%d, %d, %d)", r->valueint, g->valueint, b->valueint);
        }
    }

    // 向OneNET发送应答
    char* reply_payload = NULL;
    onenet_set_dm_property_ack(&reply_payload, msg_id, 200, "success");

    if (reply_payload != NULL)
    {
        esp_mqtt_client_publish(client, TOPIC_PROP_SET_REPLY, reply_payload, 0, 1, 0);
        ESP_LOGI(TAG, "Reply sent -> %s", reply_payload);
        free(reply_payload);
    }

    cJSON_Delete(root);
}

void onenet_post_ota_version(const char *version)
{
    ONENET_DM_DES *dm = onenet_malloc_des(ONENET_OTA_VERSION);
    onenet_set_ota_version(dm, version);
    onenet_dm_serialize(dm);
    esp_mqtt_client_publish(client, TOPIC_OTA_INFORM, dm->dm_js_str, 0, 1, 0);
    ESP_LOGI(TAG, "OTA post: %s", dm->dm_js_str);
    onenet_dm_free(dm);
}

void mqtt_event_handler(void* event_handler_arg,
                        esp_event_base_t event_base,
                        int32_t event_id,void* event_data)
{

    /* =======================MQTT event=========================== */
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;   
    switch((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "OneNET MQTT_EVENT_CONNECTED");
            // 1. 订阅属性上报的响应主题 云端下发控制的主题
            esp_mqtt_client_subscribe(client, TOPIC_PROP_POST_REPLY, 1);
            esp_mqtt_client_subscribe(client, TOPIC_PROP_SET, 1);
       
            // 全部初始化完成后置位，通知遥测任务可以开始上报属性
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            s_is_mqtt_connected = 1;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "OneNET MQTT_EVENT_DISCONNECTED");
            vTaskDelay(pdMS_TO_TICKS(5000));
            xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            s_is_mqtt_connected = 0;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "OneNET MQTT_EVENT_SUBSCRIBED");
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "OneNET MQTT_EVENT_UNSUBSCRIBED");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "OneNET MQTT_EVENT_PUBLISHED");
            break;
        case MQTT_EVENT_DATA:     
            ESP_LOGI(TAG, "OneNET MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "PAYLOAD=%.*s", event->data_len, event->data);

            // 检查接收的Topic是否为属性设置主题
            if(strncmp(event->topic, TOPIC_PROP_SET, event->topic_len) == 0)
            {
                handle_property_set(client, event->data, event->data_len);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "OneNET MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", (int)event_id);
            break;
    }
}


/*
 * {
 *   "id": "123",
 *   "version": "1.0",
 *   "params": {
 *     "LightSwitch": { "value": true },
 *     "r": { "value": 255 },
 *     "g": { "value": 128 },
 *     "b": { "value": 0 }
 *   }
 * }
*/

// 模拟读取设备当前 RGB 与开关状态
static void get_current_device_status(bool *switch_state, int *r_val, int *g_val, int *b_val)
{
    static int hue_step = 0;
    *switch_state = true; // 保持开启状态
    
    // 模拟灯光颜色渐变
    *r_val = (hue_step * 10) % 256;
    *g_val = (hue_step * 20) % 256;
    *b_val = 255 - *r_val;
    
    hue_step = (hue_step + 1) % 256; // 关键：限制范围，防止无限增长
}

void onenet_post_task(void *pvParameters)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFreq = pdMS_TO_TICKS(50000);

    ESP_LOGI(TAG, "Telemetry task started");

    for(;;)
    {
        // 阻塞等待mqtt连通事件标志，网络断开时不盲目采集与发布
        xEventGroupWaitBits(s_mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        // 获取硬件或传感器状态
        bool is_on = true;
        int r = 234, g = 128, b = 27;
        get_current_device_status(&is_on, &r, &g, &b);
        // 创建物模型描述结构体
        ONENET_DM_DES *dm = onenet_malloc_des(ONENET_DM_POST);
        if(dm!= NULL)
        {
            // 填充在OneNET定义的4个物模型功能点
            onenet_set_dm_bool(dm, "LightSwitch", is_on);
            onenet_set_dm_int(dm, "r", r);
            onenet_set_dm_int(dm, "g", g);
            onenet_set_dm_int(dm, "b", b);

            // 序列化为紧凑JSON字符串
            onenet_dm_serialize(dm);

            // 发布到OneNET属性上报主题
            if(dm->dm_js_str != NULL)
            {
                int msg_id = esp_mqtt_client_publish(client, TOPIC_PROP_POST, dm->dm_js_str, 0, 1, 0);
                ESP_LOGI(TAG, "Post properties -> [msg_id=%d] Payload: %s", msg_id, dm->dm_js_str);  
            }

            // 释放结构体，cJSON树及序列化字符串
            onenet_dm_free(dm);
        }
        vTaskDelayUntil(&xLastWakeTime, xFreq);
    }
}

char is_mqtt_connected(void)
{
    return s_is_mqtt_connected;
}


void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
            .address.uri = MQTT_BROKER,
            .address.port = MQTT_PORT,
            .verification.certificate = g_oneiot_ca,
            .verification.skip_cert_common_name_check = true,
        },
    .credentials = {
            .client_id = ONENET_DEVICE_NAME,
            .username = ONENET_PRODUCT_ID,
            .authentication.password = ONEIOT_DEVTOKEN,
        },
    };

    // build event group sync connect status
    s_mqtt_event_group = xEventGroupCreate();

    // start mqtt connect
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    // 启动数据采集与上报任务
    xTaskCreate(onenet_post_task,
                "OneNET_pub_task",
                4096, (void *)client, 5, NULL);
}




const char *g_oneiot_ca = "-----BEGIN CERTIFICATE-----\n"
"MIIDOzCCAiOgAwIBAgIJAPCCNfxANtVEMA0GCSqGSIb3DQEBCwUAMDQxCzAJBgNV"
"BAYTAkNOMQ4wDAYDVQQKDAVDTUlPVDEVMBMGA1UEAwwMT25lTkVUIE1RVFRTMB4X"
"DTE5MDUyOTAxMDkyOFoXDTQ5MDUyMTAxMDkyOFowNDELMAkGA1UEBhMCQ04xDjAM"
"BgNVBAoMBUNNSU9UMRUwEwYDVQQDDAxPbmVORVQgTVFUVFMwggEiMA0GCSqGSIb3"
"DQEBAQUAA4IBDwAwggEKAoIBAQC/VvJ6lGWfy9PKdXKBdzY83OERB35AJhu+9jkx"
"5d4SOtZScTe93Xw9TSVRKrFwu5muGgPusyAlbQnFlZoTJBZY/745MG6aeli6plpR"
"r93G6qVN5VLoXAkvqKslLZlj6wXy70/e0GC0oMFzqSP0AY74icANk8dUFB2Q8usS"
"UseRafNBcYfqACzF/Wa+Fu/upBGwtl7wDLYZdCm3KNjZZZstvVB5DWGnqNX9HkTl"
"U9NBMS/7yph3XYU3mJqUZxryb8pHLVHazarNRppx1aoNroi+5/t3Fx/gEa6a5PoP"
"ouH35DbykmzvVE67GUGpAfZZtEFE1e0E/6IB84PE00llvy3pAgMBAAGjUDBOMB0G"
"A1UdDgQWBBTTi/q1F2iabqlS7yEoX1rbOsz5GDAfBgNVHSMEGDAWgBTTi/q1F2ia"
"bqlS7yEoX1rbOsz5GDAMBgNVHRMEBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQAL"
"aqJ2FgcKLBBHJ8VeNSuGV2cxVYH1JIaHnzL6SlE5q7MYVg+Ofbs2PRlTiWGMazC7"
"q5RKVj9zj0z/8i3ScWrWXFmyp85ZHfuo/DeK6HcbEXJEOfPDvyMPuhVBTzuBIRJb"
"41M27NdIVCdxP6562n6Vp0gbE8kN10q+ksw8YBoLFP0D1da7D5WnSV+nwEIP+F4a"
"3ZX80bNt6tRj9XY0gM68mI60WXrF/qYL+NUz+D3Lw9bgDSXxpSN8JGYBR85BxBvR"
"NNAhsJJ3yoAvbPUQ4m8J/CoVKKgcWymS1pvEHmF47pgzbbjm5bdthlIx+swdiGFa"
"WzdhzTYwVkxBaU+xf/2w\n"
"-----END CERTIFICATE-----";