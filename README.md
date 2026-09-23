# idf-mqtt-test — ESP32-C3 OneNET 物联网接入示例

基于 **ESP-IDF 5.3** 的 ESP32-C3 应用示例，演示如何把设备接入中国移动 **OneNET** 物联网平台，实现：

- Wi-Fi STA 联网 + SNTP 校时（TLS 需要正确的系统时间）
- 通过 **MQTT over TLS（8883）** 与 OneNET 建立长连接
- 按 OneNET **物模型（Thing Model）** 格式上报属性（遥测）
- 订阅并处理云端下发的属性设置（控制）指令，并回执 ACK
- 通过 OneNET **Fuse-OTA** HTTP 接口上报版本、检查升级、下载固件、MD5 校验并切换启动分区

> 项目名 `idf-mqtt-test` 来自 ESP-IDF 模板，实际用途是 OneNET 接入与 OTA 参考实现。

## 功能概览

| 功能 | 说明 | 代码位置 |
| --- | --- | --- |
| Wi-Fi 接入 | STA 模式，断线 1 秒后自动重连，等待 IP 超时则重启 | `main/idf-mqtt-test.c` |
| 时间同步 | SNTP 轮询 `ntp.aliyun.com` / `cn.pool.ntp.org`，等待年份 > 2024 | `main/mqtt_oneiot.c` |
| MQTT 连接 | `mqtts://` TLS 连接 OneNET 8883，内置平台 CA 证书 | `main/mqtt_oneiot.c` |
| 属性上报 | 每 50 秒上报一次 `LightSwitch` / `r` / `g` / `b`（当前为模拟数据） | `main/mqtt_oneiot.c` |
| 属性下发 | 订阅 `property/set`，解析并打印，返回 `set_reply` 回执 | `main/mqtt_oneiot.c` |
| 物模型封装 | cJSON 封装上报 / 回执 / 版本 / 事件报文 | `main/onenet_dm.c` |
| OTA 升级 | 版本上报、定时检查、HTTP 下载、MD5 校验、切换分区后重启 | `main/onenet_ota.c` |

## 目录结构

```
.
├── CMakeLists.txt          # 顶层工程与版本宏定义（APP/FW 版本号）
├── partitions.csv          # 分区表：双 OTA 分区，各 1500K
├── sdkconfig               # 工程配置（target=esp32c3，4MB Flash）
├── main/
│   ├── CMakeLists.txt      # main 组件注册 + 版本宏注入
│   ├── idf-mqtt-test.c     # 应用入口：Wi-Fi 初始化与主流程编排
│   ├── mqtt_oneiot.c/.h    # SNTP 校时、MQTT 客户端、属性上报/下发、主题与密钥
│   ├── onenet_dm.c/.h      # OneNET 物模型 JSON 封装与解析辅助
│   └── onenet_ota.c/.h     # Fuse-OTA：版本上报 / 任务检查 / 下载烧录
└── build/                  # 构建产物（可由 idf.py 重新生成）
```

## 环境要求

- **芯片**：ESP32-C3
- **Flash**：4 MB（`partitions.csv` 按双 1500K OTA 槽设计）
- **ESP-IDF**：v5.3.0（测试版本；其他 5.x 版本通常兼容）
- 需要 `ESP-IDF` 环境变量 `IDF_PATH` 可用（工程 CMake 依赖它）
- OneNET 平台上的产品与设备（需要产品 ID、设备名、密钥/Token）

## 快速开始

### 1. 设置目标芯片

```bash
idf.py set-target esp32c3
```

> 注意：`set-target` 会重置 `sdkconfig`，如需保留现有配置请谨慎操作或先备份。

### 2. 修改设备与网络配置

下面这些是**必须替换**为你自己环境的占位/示例值：

| 配置项 | 位置 | 含义 |
| --- | --- | --- |
| `TEST_SSID` / `TEST_PASSWORD` | `main/idf-mqtt-test.c` | Wi-Fi 名称与密码 |
| `ONENET_PRODUCT_ID` | `main/mqtt_oneiot.h` | OneNET 产品 ID |
| `ONENET_DEVICE_NAME` | `main/mqtt_oneiot.h` | 设备名称（同时用作 MQTT client_id） |
| `ONEIOT_DEVTOKEN` | `main/mqtt_oneiot.h` | MQTT 接入 Token（建议用工具按需生成） |
| `ONEIOT_PRODUCTKEY` / `ONEIOT_DEVICEKEY` | `main/mqtt_oneiot.h` | 产品/设备密钥 |
| `ONENET_OTA_AUTH` | `main/onenet_ota.h` | OTA HTTP 接口鉴权 Token |

> 密钥与 Token 属于敏感信息，当前直接硬编码在源码中，仅作演示。生产环境建议改从 `nvs` / Kconfig 读取，或使用无 Token 的认证方式动态生成。

### 3. 构建与烧录

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

退出串口监视：`Ctrl + ]`。

## 运行流程

```
app_main()
  ├─ NVS / netif / 事件循环初始化
  ├─ Wi-Fi STA 启动并按配置连接
  ├─ 轮询等待获取 IP（最多 15s，超时则重启）
  └─ 获取 IP 成功
       ├─ sync_system_time()   # SNTP 校时
       ├─ mqtt_start()         # 连接 OneNET 并启动属性上报任务
       └─ ota_timer_init()     # 启动版本上报、升级检查任务与 60s 定时器
```

## OneNET 主题（Topic）

主题在 `main/mqtt_oneiot.h` 中按 `$sys/{pid}/{dev}/...` 规则拼装：

| 主题 | 方向 | 用途 |
| --- | --- | --- |
| `thing/property/post` | 上行 | 上报属性 |
| `thing/property/post/reply` | 下行 | 上报应答（已订阅） |
| `thing/property/set` | 下行 | 云端下发属性设置（已订阅） |
| `thing/property/set_reply` | 上行 | 属性设置回执 |
| `ota/inform` | 上行 | 上报软件/固件版本 |

上报报文示例（`onenet_post_task` 生成）：

```json
{
  "id": "100",
  "version": "1.0",
  "params": {
    "LightSwitch": { "value": true },
    "r": { "value": 234 },
    "g": { "value": 128 },
    "b": { "value": 27 }
  }
}
```

## OTA 升级流程

平台侧通过 OneNET **Fuse-OTA** HTTP 接口交互，接口与鉴权常量位于 `main/onenet_ota.c` / `main/onenet_ota.h`：

1. `POST .../{pid}/{dev}/version` — 启动时上报 `s_version` / `f_version`
2. `GET  .../check?type=2&version=...` — 每 **60 秒** 查询一次升级任务（`type=2` 即 SOTA）
3. 检测到 `status=1` 的差分包任务时，**当前实现会跳过**（不支持差分还原），仅处理整包
4. `GET  .../{tid}/download` — 下载固件流并写入下一个 OTA 分区，每 10% 上报一次进度
5. MD5 校验通过后 `esp_ota_set_boot_partition()` 并重启生效

进度与状态通过 `POST .../{tid}/status` 上报（`step`：1~100 为进度，100 完成，-1 失败）。

## 版本号管理

版本宏在顶层 `CMakeLists.txt` 定义，并由 `main/CMakeLists.txt` 注入为编译宏：

- `APP_SW_VERSION` — 应用（SOTA）版本，OTA 检查时作为当前版本上报
- `FW_VERSION` — 固件（FOTA）版本

修改版本号时同步更新对应的 `*_MAJOR/_MINOR/_PATCH` 字段。

## 说明与后续可做的事

- 属性上报与颜色数据目前为**模拟值**（`get_current_device_status`），控制指令中的 GPIO 操作为 `TODO`，接入真实硬件时替换即可。
- HTTP 接口使用 `http://` 明文地址 + `esp_crt_bundle`，如平台侧支持请优先切换为 HTTPS。
- MQTT 使用内置 CA 证书并 `skip_cert_common_name_check = true`，生产环境应验证证书 CN。
- Wi-Fi 凭据与各类密钥建议迁移到 `nvs` 或 Kconfig，避免硬编码泄露。
