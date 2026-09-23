#ifndef __ONENET_DM_H__
#define __ONENET_DM_H__

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

typedef enum
{
    ONENET_DM_POST,
    ONENET_DM_SET_ACK,
    ONENET_DM_GET_ACK,
    ONENET_DM_EVENT,
    ONENET_OTA_VERSION,
}ONENET_DM_TYPE;

typedef struct
{
    cJSON *dm_js;
    char *dm_js_str;
    int   data_len;
} ONENET_DM_DES;

// 生成一个物模型描述结构体
ONENET_DM_DES *onenet_malloc_des(ONENET_DM_TYPE dm_type);
// 往物模型描述结构体添加属性值
void onenet_set_dm_int(ONENET_DM_DES *dm, const char *name, int value);
void onenet_set_dm_bool(ONENET_DM_DES *dm, const char *name, bool value);
// 生成cJSON字符串，保存在dm_js_str
void onenet_dm_serialize(ONENET_DM_DES *dm);
// 设置OTA版本
void onenet_set_ota_version(ONENET_DM_DES *dm, const char *version);
// 释放物模型描述结构体
void onenet_dm_free(ONENET_DM_DES *dm);
// 属性回复
void onenet_set_dm_property_ack(char **payload, const char *msg_id, int code, const char *msg);

#endif /* __ONENET_DM_H__ */
