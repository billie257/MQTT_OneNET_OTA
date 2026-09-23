# 生成 version.h 到构建目录
idf_component_register(SRCS "onenet_ota.c" "onenet_dm.c" "idf-mqtt-test.c" "mqtt_oneiot.c"
                    INCLUDE_DIRS ".")

# 将版本号作为宏定义传递给 main 组件
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    APP_SW_VERSION="${APP_SW_VERSION}"
    APP_SW_VERSION_MAJOR=${APP_SW_VERSION_MAJOR}
    APP_SW_VERSION_MINOR=${APP_SW_VERSION_MINOR}
    APP_SW_VERSION_PATCH=${APP_SW_VERSION_PATCH}
    FW_VERSION="${FW_VERSION}"
    FW_VERSION_MAJOR=${FW_VERSION_MAJOR}
    FW_VERSION_MINOR=${FW_VERSION_MINOR}
    FW_VERSION_PATCH=${FW_VERSION_PATCH}
)