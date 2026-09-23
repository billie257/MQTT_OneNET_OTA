#ifndef __ONENET_OTA_H__
#define __ONENET_OTA_H__

#define ONENET_OTA_AUTH  "version=2018-10-31&res=products%2FdIfM184wC6%2Fdevices%2Fesp32&et=2000000000&method=sha1&sign=mw5y8xW0JeY4pe3vQ1pirUdyhO8%3D"
#define fota  1
#define sota  2

typedef struct
{
    int tid;
    int size;
    char target[32];
    char md5[64];
    int status;
    int type;
} ota_task_info_t;

void ota_timer_init(void);

#endif /* __ONENET_OTA_H__ */
