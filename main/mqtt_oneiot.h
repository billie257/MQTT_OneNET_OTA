#ifndef __MQTT_ONEIOT_H__
#define __MQTT_ONEIOT_H__


#define ONENET_PRODUCT_ID      "dIfM184wC6"
#define ONENET_DEVICE_NAME     "esp32"
#define TOPIC_PROP_POST        "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/thing/property/post"
#define TOPIC_PROP_POST_REPLY  "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/thing/property/post/reply"

#define TOPIC_PROP_SET       "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/thing/property/set"
#define TOPIC_PROP_SET_REPLY "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/thing/property/set_reply"

#define TOPIC_OTA_INFORM       "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/ota/inform"
#define TOPIC_OTA_INFORM_REPLY "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/ota/inform_reply"

#define TOPIC_OTA_NOTIFY       "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/ota/notify"
#define TOPIC_OTA_NOTIFY_REPLY "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/ota/notify_reply"

#define TOPIC_OTA_PROGRESS   "$sys/"ONENET_PRODUCT_ID"/"ONENET_DEVICE_NAME"/ota/progress"


#define ONEIOT_PRODUCTKEY   "JafLWd+nibHP24+TBcF5vTVng3cFO/XrsyExh+0rj2E="
#define ONEIOT_DEVICEKEY   "ZUxvSGJ5TjNPaEJhTFBoOE9NUWhtaXJOdHpKU3kzeTM="
#define ONEIOT_DEVTOKEN    "version=2018-10-31&res=products%2FdIfM184wC6%2Fdevices%2Fesp32&et=2000000000&method=md5&sign=HUCsaAcyPomFDoGyIaLuDA%3D%3D" // 生成的token字符

#define MQTT_BROKER        "mqtts://"ONENET_PRODUCT_ID".mqttstls.acc.cmcconenet.cn:8883"  // 服务器地址
#define MQTT_PORT          8883

#define MQTT_CONNECTED_BIT   BIT0
extern EventGroupHandle_t s_mqtt_event_group;

void sync_system_time(void);
// 启动OneNET IOT连接
void mqtt_start(void);
// 上报版本号
void onenet_post_ota_version(const char *version);
// 返回当前应用版本
const char *get_app_version(void);

char is_mqtt_connected(void);

#endif /* __MQTT_ONEIOT_H__ */
