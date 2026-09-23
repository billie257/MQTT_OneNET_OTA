#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mqtt_oneiot.h"
#include "esp_ota_ops.h"
#include "onenet_ota.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "OTA"
#define BUFF_SIZE 1024

static TimerHandle_t ota_check_timer = NULL;
static TaskHandle_t ota_task_handle = NULL;

typedef struct
{
    char *buffer;
    int buffer_len;
    int output_len;
}http_response_ctx_t;

static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->user_data != NULL && evt->data_len > 0) {
                http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
                if (ctx->output_len + evt->data_len >= ctx->buffer_len - 1) 
                {
                    ESP_LOGE(TAG, "响应数据过长，缓冲区溢出！当前：%d, 追加:%d", ctx->output_len, evt->data_len);
                    return ESP_FAIL;
                }
                memcpy(ctx->buffer + ctx->output_len, evt->data, evt->data_len);
                ctx->output_len += evt->data_len;
                ctx->buffer[ctx->output_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief 上报 OTA 进度与状态
 * @param tid 任务 ID
 * @param step 进度 1-100
 * @param status 状态码 (1: 待升级、2: 下载中、3:升级中、4:升级成功、5: 升级失败、6:升级取消)
 * @param desc 描述信息
 */
static esp_err_t onenet_ota_report_status(int tid, int step)
{
    char url[128];
    snprintf(url, sizeof(url), "http://iot-api.heclouds.com/fuse-ota/%s/%s/%d/status",
                    ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, tid);
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", ONENET_OTA_AUTH);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "step", step);

    char *post_data = cJSON_PrintUnformatted(root);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK){
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "状态上报成功 [tid: %d, step: %d%%, status: %d]", tid, step, status_code);
    } else {
        ESP_LOGE(TAG, "状态上报失败：%s", esp_err_to_name(err));
    }
    free(post_data);
    cJSON_Delete(root);
    esp_http_client_cleanup(client);
    return err;
}

/**
 * @brief 上报设备当前版本
 */
void ota_version_report_task(void *pvParameters)
{
    int status_code = 0;
    char url[256];
    char resp_buf[512] = {0};        // 用于接收返回的响应体

    // 封装接收上下文
    http_response_ctx_t resp_ctx = {
        .buffer = resp_buf,
        .buffer_len = sizeof(resp_buf),
        .output_len = 0,
    };

    // 拼接官方接口: http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/version
    snprintf(url, sizeof(url), "http://iot-api.heclouds.com/fuse-ota/%s/%s/version", 
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = ota_http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    esp_http_client_set_header(http_client, "Authorization", ONENET_OTA_AUTH);
    esp_http_client_set_header(http_client, "Content-Type", "application/json");

    // 组装请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "s_version", APP_SW_VERSION);
    cJSON_AddStringToObject(root, "f_version", FW_VERSION);
    char *post_data = cJSON_PrintUnformatted(root);
    esp_http_client_set_post_field(http_client, post_data, strlen(post_data));

    // 执行 HTTP 请求
    esp_err_t err = esp_http_client_perform(http_client);
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(http_client);
        ESP_LOGI(TAG, "HTTP Report Version Success, Status = %d", status_code);
        ESP_LOGI(TAG, "服务器原始响应: %s", resp_ctx.buffer);

        if (status_code == 200) {
            // resp_buf 已由事件回调自动填充，直接解析 JSON
            cJSON *resp_json = cJSON_Parse(resp_ctx.buffer);
            if (resp_json != NULL) {
                cJSON *code = cJSON_GetObjectItem(resp_json, "code");
                cJSON *msg = cJSON_GetObjectItem(resp_json, "msg");
                cJSON *req_id = cJSON_GetObjectItem(resp_json, "request_id");

                if (cJSON_IsNumber(code) && code->valueint == 0) {
                    ESP_LOGI(TAG, "版本上报成功！msg: %s, request_id: %s", 
                             cJSON_GetStringValue(msg), 
                             cJSON_GetStringValue(req_id));
                } else {
                    ESP_LOGE(TAG, "业务报错！code: %d, msg: %s", 
                             cJSON_IsNumber(code) ? code->valueint : -1, 
                             cJSON_GetStringValue(msg));
                }
                cJSON_Delete(resp_json);
            } else {
                ESP_LOGE(TAG, "解析 JSON 响应失败");
            }
        } else {
            ESP_LOGE(TAG, "HTTP 请求失败，状态码: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP Report Version Failed: %s", esp_err_to_name(err));
    }

    // 释放动态分配的堆内存与句柄（注意：栈数组 resp_buf 绝不能 free）
    free(post_data);
    cJSON_Delete(root);
    esp_http_client_cleanup(http_client);

    vTaskDelete(NULL); 
}

/**
 * @brief 下载并烧录固件
 */
static esp_err_t onenet_ota_download_and_flash(const ota_task_info_t *task_info)
{
    char url[128];
    snprintf(url, sizeof(url), "http://iot-api.heclouds.com/fuse-ota/%s/%s/%d/download",
                            ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, task_info->tid);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = BUFF_SIZE,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", ONENET_OTA_AUTH);
    // 发起HTTP连接
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "无法连接下载服务器：%s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    // 接收HTTP Headers
    int content_length = esp_http_client_fetch_headers(client);
    int http_status = esp_http_client_get_status_code(client);

    // 读取并分析自定义响应头 Ota-Errno
    char *ota_errno_str = NULL;
    esp_http_client_get_header(client, "Ota-Errno", &ota_errno_str);
    int ota_errno = ota_errno_str ? atoi(ota_errno_str) : 0;

    if (http_status != 200) {
        ESP_LOGE(TAG, "下载校验未通过! HTTP状态码: %d", http_status);
        switch (ota_errno) {
            case 0: ESP_LOGI(TAG, "成功！"); break;
            case 1: ESP_LOGE(TAG, "原因: 设备不存在"); break;
            case 2: ESP_LOGE(TAG, "原因: 文件资源不存在"); break;
            case 3: ESP_LOGE(TAG, "原因: 对象存储中资源不存在"); break;
            case 4: ESP_LOGE(TAG, "原因: 获取文件失败，文件大小不一致"); break;
            case 5: ESP_LOGE(TAG, "原因: 请求参数错误 、任务已过期|完成"); break;
            case 6: ESP_LOGE(TAG, "原因: 鉴权失败"); break;
            default: ESP_LOGE(TAG, "原因: 未知错误"); break;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "下载请求成功，开始接收固件...");

    // 4. 校验 Content-Length 与 check 接口返回的大小是否匹配
    if (task_info->size > 0 && content_length > 0 && content_length != task_info->size) {
        ESP_LOGE(TAG, "文件大小不一致! 任务大小: %d, 响应大小: %d", task_info->size, content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // 5. 初始化 OTA 分区与 MD5 计算上下文
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "未找到目标 OTA 分区");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    // OTA_SIZE_UNKNOWN 会擦除整个分区
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    mbedtls_md5_context md5_ctx;
    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts(&md5_ctx);

    char *upgrade_data_buf = malloc(BUFF_SIZE);
    if (!upgrade_data_buf) {
        ESP_LOGE(TAG, "分配下载缓冲区失败！内存不足");
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    int binary_file_len = 0;
    int last_reported_step = -1;

    while (1) {
        int data_read = esp_http_client_read(client, upgrade_data_buf, BUFF_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "SSL/HTTP 数据读取失败");
            break;
        } else if (data_read > 0) {
            // 写入OTA FLASH分区
            err = esp_ota_write(ota_handle, (const void *)upgrade_data_buf, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Flash 写入失败: %s", esp_err_to_name(err));
                break;
            }
            // 更新 MD5
            mbedtls_md5_update(&md5_ctx, (const unsigned char *)upgrade_data_buf, data_read);
            binary_file_len += data_read;

            // 动态上报下载进度 (每 10% 上报一次，避免过度占用网络带宽)
            // 🌟 核心逻辑：动态上报 current_step 🌟
            if (task_info->size > 0) { // 必须防除零崩溃
                int current_step = (binary_file_len * 100) / task_info->size;
                // 每增加 10% 且小于 100% 时上报一次，避免频繁占用网络
                if (current_step - last_reported_step >= 10 && current_step < 100) {
                    last_reported_step = current_step;
                    onenet_ota_report_status(task_info->tid, current_step);
                }
            }
            
        } else {
            ESP_LOGI(TAG, "文件流接收完毕，总大小: %d 字节", binary_file_len);
            break;
        }
    }

    free(upgrade_data_buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // 7. MD5 校验
    unsigned char md5_calc[16];
    char md5_calc_str[33] = {0};
    mbedtls_md5_finish(&md5_ctx, md5_calc);
    mbedtls_md5_free(&md5_ctx);

    for (int i = 0; i < 16; i++) {
        sprintf(&md5_calc_str[i * 2], "%02x", md5_calc[i]);
    }

    if (strcasecmp(task_info->md5, md5_calc_str) != 0) {
        ESP_LOGE(TAG, "MD5 校验失败! 平台: %s, 本地计算: %s", task_info->md5, md5_calc_str);
        esp_ota_abort(ota_handle);
        mbedtls_md5_free(&md5_ctx);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MD5 校验通过!");

    // 8. 提交固件并设置新启动分区
    if (esp_ota_end(ota_handle) == ESP_OK && 
        esp_ota_set_boot_partition(update_partition) == ESP_OK) {
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

static void ota_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "定时器触发，通知OTA任务检查升级");
    xTaskNotifyGive(ota_task_handle);
}

/**
 * @brief 设备检测升级任务
 */
void ota_check_task(void *pvParamters)
{
    char url[256];
    char response_buf[1024] = {0};

    // 增加结构体初始化
    http_response_ctx_t resp_ctx = {
        .buffer = response_buf,
        .buffer_len = sizeof(response_buf),
        .output_len = 0,
    };

    while (1)
    { 
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        memset(response_buf, 0, sizeof(response_buf));
        resp_ctx.output_len = 0;
        // 拼接官方接口: http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/check
        snprintf(url, sizeof(url), "http://iot-api.heclouds.com/fuse-ota/%s/%s/check?type=%d&version=%s", 
                ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, sota, APP_SW_VERSION);
        
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = ota_http_event_handler,
            .user_data = &resp_ctx,
            .timeout_ms = 10000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Authorization", ONENET_OTA_AUTH);
        
        ESP_LOGI(TAG, "开始向 OneNET 查询是否有升级任务...");
        esp_err_t err = esp_http_client_perform(client);
        ota_task_info_t task_info = {0};
        bool need_update = false;

        if (err == ESP_OK && esp_http_client_get_status_code(client) == 200)
        {
            ESP_LOGI(TAG, "原始响应内容：[%s]，长度：%d", response_buf, strlen(response_buf));
            if (strlen(response_buf) == 0)
            {
                ESP_LOGW(TAG, "收到空响应体，可能网络拥堵，本次跳过");
                esp_http_client_cleanup(client);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            cJSON *root = cJSON_Parse(response_buf);
            if (root)
            {
                cJSON *code = cJSON_GetObjectItem(root, "code");
                // cJSON *msg = cJSON_GetObjectItem(root, "msg");
                // cJSON *req_id = cJSON_GetObjectItem(root, "request_id");
                cJSON *data = cJSON_GetObjectItem(root, "data");
                if (cJSON_IsNumber(code) && code->valueint == 0 && data && !cJSON_IsNull(data))
                {
                    cJSON *target = cJSON_GetObjectItem(data, "target");
                    cJSON *tid = cJSON_GetObjectItem(data, "tid");
                    cJSON *size = cJSON_GetObjectItem(data, "size");
                    cJSON *md5 = cJSON_GetObjectItem(data, "md5");
                    cJSON *status = cJSON_GetObjectItem(data, "status");
                    cJSON *type = cJSON_GetObjectItem(data, "type");

                    if (tid && target) {
                        task_info.tid = tid->valueint;
                        task_info.size = size ? size->valueint : 0;
                        strncpy(task_info.target, target->valuestring, sizeof(task_info.target) - 1);
                        if (md5 && md5->valuestring) {
                            strncpy(task_info.md5, md5->valuestring, sizeof(task_info.md5) - 1);
                        }

                        task_info.status = status ? status->valueint : 0;
                        task_info.type = type ? type->valueint : 0;

                        if (task_info.status == 1){
                            if (task_info.type == 1) {
                                need_update = true;
                                ESP_LOGI(TAG, "检测到新固件版本: %s, TID: %d, 大小: %d", 
                                            task_info.target, task_info.tid, task_info.size);
                            } else if (task_info.type == 2) {
                                ESP_LOGW(TAG, "检测到差分升级包(type=2)，但当前固件不支持差分还原，跳过升级。");
                                need_update = false;
                            }
                        } else {
                            ESP_LOGI(TAG, "任务存在但状态不是待升级(status=%d)，跳过。", task_info.status);
                            need_update = false;
                        }               
                    }                                   
                }
                else
                {
                    ESP_LOGI(TAG, "暂无升级任务");
                }
                cJSON_Delete(root);
            }
            else{
                ESP_LOGI(TAG, "解析JSON失败");
            }
        }
        else{
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);

        // 如果检测到升级任务，开始 OTA 过程
        if (need_update) {
            // 1.上报开始下载状态（进度 0%）
            onenet_ota_report_status(task_info.tid, 0);

            // 2.执行下载与写入
            err = onenet_ota_download_and_flash(&task_info);

            if (err == ESP_OK) {
                // 上报成功状态
                onenet_ota_report_status(task_info.tid, 100);
                ESP_LOGI(TAG, "OTA 完成，5 秒后重启设备生效...");
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_restart();
            } else {
                // 上报失败状态
                onenet_ota_report_status(task_info.tid, -1);
                ESP_LOGE(TAG, "OTA 过程出错");
            }
        }
    }
    
    vTaskDelete(NULL);
}

void ota_timer_init(void)
{
    xTaskCreate(ota_version_report_task, "ota_ver_task", 8192, NULL, 5, NULL);
    xTaskCreate(ota_check_task, "ota_check_task", 8192, NULL, 5, &ota_task_handle);

    ota_check_timer = xTimerCreate("OTACheckTimer", pdMS_TO_TICKS(60000), pdTRUE, (void *)0, ota_timer_callback);

    if(ota_check_timer != NULL){
        xTimerStart(ota_check_timer, 0);
    }

    xTaskNotifyGive(ota_task_handle);
}