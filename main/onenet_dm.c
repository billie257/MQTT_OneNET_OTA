#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "onenet_dm.h"

static int s_onenet_id = 100;

// 生成一个物模型描述结构体
ONENET_DM_DES *onenet_malloc_des(ONENET_DM_TYPE dm_type)
{
    ONENET_DM_DES *dm = (ONENET_DM_DES *)malloc(sizeof(ONENET_DM_DES));
    if(!dm) {
        return NULL;
    }
    memset(dm, 0, sizeof(ONENET_DM_DES));

    dm->dm_js = cJSON_CreateObject(); 
    if(!dm->dm_js){
        free(dm);
        return NULL;
    }
    char id[12];
    snprintf(id, sizeof(id), "%d", s_onenet_id++); // 每次创建自增ID
    cJSON_AddStringToObject(dm->dm_js, "id", id);

    switch (dm_type)
    {
        case ONENET_DM_POST:   // 常规属性上报
            cJSON_AddStringToObject(dm->dm_js, "version", "1.0");
            cJSON_AddObjectToObject(dm->dm_js, "params");
            break;
        case ONENET_DM_SET_ACK: // 属性设置回复
            break;
        case ONENET_DM_EVENT:  // 事件上报
            cJSON_AddStringToObject(dm->dm_js, "version", "1.0");
            cJSON_AddObjectToObject(dm->dm_js, "params");
            break;
        case ONENET_OTA_VERSION:
            cJSON_AddStringToObject(dm->dm_js, "version", "1.0");
            cJSON_AddObjectToObject(dm->dm_js, "params");
            break;
        default:
            break;
    }

    return (ONENET_DM_DES *)dm;
}

// 往物模型描述结构体添加属性值
void onenet_set_dm_int(ONENET_DM_DES *dm, const char *name, int value)
{
    if (!dm || !dm->dm_js || !name)
    {
        return;
    }
    
    cJSON *params_js = cJSON_GetObjectItem(dm->dm_js, "params");
    if(params_js)
    {
        cJSON_DeleteItemFromObject(params_js, name); // 防止键名冲突

        cJSON *prop = cJSON_CreateObject();
        if(prop)
        {
            cJSON_AddNumberToObject(prop, "value", value);
            cJSON_AddItemToObject(params_js, name, prop);
        }
    } 
}

void onenet_set_dm_bool(ONENET_DM_DES *dm, const char *name, bool value)
{
    if (!dm || !dm->dm_js || !name)
    {
        return;
    }

    cJSON *params_js = cJSON_GetObjectItem(dm->dm_js, "params");
    if(params_js)
    {
        cJSON_DeleteItemFromObject(params_js, name);

        cJSON *prop = cJSON_CreateObject();
        if(prop)
        {
            cJSON_AddBoolToObject(prop, "value", value);
            cJSON_AddItemToObject(params_js, name, prop);
        }
    }
}

// 属性回复
void onenet_set_dm_property_ack(char **payload, const char *msg_id, int code, const char *msg)
{
    ONENET_DM_DES *dm = (ONENET_DM_DES *)malloc(sizeof(ONENET_DM_DES));
    if(!dm) {
        *payload = NULL;
        return;
    }
    memset(dm, 0, sizeof(ONENET_DM_DES));

    dm->dm_js = cJSON_CreateObject(); 
    if(!dm->dm_js){
        free(dm);
        *payload = NULL;
        return;
    }
    cJSON_AddStringToObject(dm->dm_js, "id", msg_id);
    cJSON_AddNumberToObject(dm->dm_js, "code", code);
    cJSON_AddStringToObject(dm->dm_js, "msg", msg);
    *payload = cJSON_PrintUnformatted(dm->dm_js); 

    cJSON_Delete(dm->dm_js);
    free(dm);
}

// 生成cJSON字符串，保存在dm_js_str
void onenet_dm_serialize(ONENET_DM_DES *dm)
{
    if(!dm || !dm->dm_js) return;

    if(dm->dm_js_str)
    {
        cJSON_free(dm->dm_js_str);
        dm->dm_js_str = NULL;
    }
    dm->dm_js_str = cJSON_PrintUnformatted(dm->dm_js); 
}

// 设置OTA版本
void onenet_set_ota_version(ONENET_DM_DES *dm, const char *version)
{
    if(!dm)  return;
    cJSON *param_js = cJSON_GetObjectItem(dm->dm_js, "params");
    if (param_js)
    {
        cJSON_AddStringToObject(param_js, "version", version);
    }
}

// 释放物模型描述结构体
void onenet_dm_free(ONENET_DM_DES *dm)
{
    if(!dm) return;

    if(dm->dm_js_str)
    {
        cJSON_free(dm->dm_js_str);
        dm->dm_js_str = NULL;
    }
    if(dm->dm_js)
    {
        cJSON_Delete(dm->dm_js);
        dm->dm_js = NULL;
    }
    free(dm);
}