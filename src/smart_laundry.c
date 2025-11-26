/**
 * 智慧洗衣房烘干监测：湿度驱动的烘干控制，PWM三档电机，按键交互，OLED 状态，
 * 以及华为 IoTDA MQTT 上报/远程指令。
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "bsp_dc_motor.h"
#include "bsp_key.h"
#include "bsp_dht11.h"
#include "bsp_oled.h"
#include "bsp_wifi.h"
#include "bsp_mqtt.h"
#include "bsp_led.h"

#include "lwip/netifapi.h"
#include "lwip/sockets.h"
#include "lwip/api_shell.h"

#include "cJSON.h"

#define WIFI_SSID "wnb"
#define WIFI_PAWD "88888888"

#define DEVICE_ID "69254fd5bf22cc5a8c09cdcf"
#define MQTT_CLIENT_ID "69254fd5bf22cc5a8c09cdcf_demo_0_0_2025112507"
#define MQTT_USER_NAME "69254fd5bf22cc5a8c09cdcf_demo"
#define MQTT_PASS_WORD "cf40861f8eed819dd954614ac014a4e99b785ef413345dc18f062853bf289609"
#define SERVER_IP_ADDR "117.78.5.125"
#define SERVER_IP_PORT 1883

#define MQTT_TOPIC_SUB_COMMANDS "$oc/devices/%s/sys/commands/#"
#define MQTT_TOPIC_PUB_COMMANDS_REQ "$oc/devices/%s/sys/commands/response/request_id=%s"
#define MQTT_TOPIC_PUB_PROPERTIES "$oc/devices/%s/sys/properties/report"

#define HUMIDITY_THRESHOLD 40
#define COUNTDOWN_SECONDS 10
#define SENSOR_PERIOD_MS 1000
#define MQTT_SEND_INTERVAL_SEC 3
#define MOTOR_PERIOD_US 20000
#define MOTOR_IDLE_SLEEP_US 50000

typedef enum {
    DRY_MODE_FAST = 0,
    DRY_MODE_STANDARD,
    DRY_MODE_SOFT,
    DRY_MODE_MAX
} dry_mode_t;

typedef struct {
    int running;
    dry_mode_t mode;
    uint8_t humidity;
    uint8_t temperature;
    int countdown;
} dryer_state_t;

typedef struct {
    uint8_t temp;
    uint8_t hum;
    int countdown;
    int running;
    dry_mode_t mode;
} sensor_msg_t;

static dryer_state_t g_state = {0};
static osMutexId_t g_state_lock;
static osMessageQueueId_t g_sensor_queue;   // OLED 刷新用的采样消息队列
static osSemaphoreId_t g_oled_sem;          // 通知 OLED 有新数据

static osThreadId_t g_control_task_id;
static osThreadId_t g_motor_task_id;
static osThreadId_t g_key_task_id;
static osThreadId_t g_oled_task_id;
static osThreadId_t g_mqtt_send_task_id;
static osThreadId_t g_mqtt_recv_task_id;

static const uint8_t g_mode_duty[DRY_MODE_MAX] = {85, 65, 45};

/**
 * @brief 将烘干模式枚举转换为人类可读的字符串
 * @param mode 烘干模式枚举值
 * @return 对应的模式字符串
 */
static const char *mode_to_string(dry_mode_t mode)
{
    switch (mode) {
        case DRY_MODE_FAST:
            return "Fast";        // 快速烘干模式
        case DRY_MODE_STANDARD:
            return "Standard";   // 标准烘干模式
        case DRY_MODE_SOFT:
            return "Soft";        // 温柔烘干模式
        default:
            return "Standard";   // 默认标准模式
    }
}

/**
 * @brief 获取烘干状态的快照
 * @return 烘干状态的副本，包含运行状态、模式、温湿度、倒计时等信息
 */
static dryer_state_t get_state_snapshot(void)
{
    dryer_state_t snapshot;
    // 加锁保护全局状态，确保获取的状态数据一致性
    osMutexAcquire(g_state_lock, osWaitForever);
    snapshot = g_state;
    osMutexRelease(g_state_lock);
    return snapshot;
}

/**
 * @brief 设置烘干倒计时时间
 * @param seconds 倒计时秒数，-1表示无倒计时
 */
static void set_countdown(int seconds)
{
    // 加锁保护全局状态，确保设置操作的安全性
    osMutexAcquire(g_state_lock, osWaitForever);
    g_state.countdown = seconds;
    osMutexRelease(g_state_lock);
}

/**
 * @brief 设置烘干机运行状态
 * @param running 1表示运行，0表示停止
 *
 * 同时控制LED指示灯状态，停止时重置倒计时
 */
static void set_running(int running)
{
    // 加锁保护全局状态，确保设置操作的安全性
    osMutexAcquire(g_state_lock, osWaitForever);
    g_state.running = running ? 1 : 0;
    if (!running) {
        g_state.countdown = -1;  // 停止时重置倒计时
    }
    osMutexRelease(g_state_lock);
    // 同步控制LED指示灯状态
    LED(running ? 1 : 0);
}

/**
 * @brief 设置烘干模式
 * @param mode 烘干模式枚举值
 *
 * 参数有效性检查，超出范围时自动切换到快速模式
 */
static void set_mode(dry_mode_t mode)
{
    // 参数有效性检查，超出范围时自动切换到快速模式
    if (mode >= DRY_MODE_MAX) {
        mode = DRY_MODE_FAST;
    }
    // 加锁保护全局状态，确保设置操作的安全性
    osMutexAcquire(g_state_lock, osWaitForever);
    g_state.mode = mode;
    osMutexRelease(g_state_lock);
}

/**
 * @brief 更新传感器温湿度值
 * @param temp 温度值
 * @param hum 湿度值
 *
 * 从DHT11传感器读取数据后更新到全局状态
 */
static void update_sensor_values(uint8_t temp, uint8_t hum)
{
    // 加锁保护全局状态，确保设置操作的安全性
    osMutexAcquire(g_state_lock, osWaitForever);
    g_state.temperature = temp;   // 更新温度值
    g_state.humidity = hum;       // 更新湿度值
    osMutexRelease(g_state_lock);
}

/**
 * @brief 不区分大小写的字符串比较
 * @param lhs 左侧字符串
 * @param rhs 右侧字符串
 * @return 相等返回1，不等返回0
 *
 * 用于命令解析时忽略大小写的匹配
 */
static int equals_ignore_case(const char *lhs, const char *rhs)
{
    while (*lhs && *rhs) {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) {
            return 0;
        }
        lhs++;
        rhs++;
    }
    return *lhs == *rhs;
}

/**
 * @brief 从JSON参数解析烘干模式
 * @param paras JSON参数对象
 * @param mode 输出参数，解析后的烘干模式
 * @return 成功返回0，失败返回-1
 *
 * 支持的JSON格式：{"gear": 1/2/3}，分别对应快速/标准/温柔模式
 */
static int parse_mode_from_json(const cJSON *paras, dry_mode_t *mode)
{
    const cJSON *gear_node = cJSON_GetObjectItem(paras, "gear");

    // 检查gear字段是否存在且为数字类型
    if (gear_node && cJSON_IsNumber(gear_node)) {
        int value = gear_node->valueint;
        // 1/2/3 分别映射 fast/standard/soft 模式
        if (value >= 1 && value <= 3) {
            *mode = (dry_mode_t)(value - 1);
            return 0;
        }
    }

    return -1;  // 解析失败
}

/**
 * @brief 应用云端控制命令
 * @param command_name 命令名称
 * @param paras 命令参数（JSON格式）
 * @return 成功返回0，失败返回1
 *
 * 支持的命令：
 * - start: 启动烘干机
 * - stop: 停止烘干机
 * - toggle: 切换烘干机运行状态
 * - set_mode/switch_mode: 设置烘干模式，需要JSON参数
 */
static int apply_cloud_command(const char *command_name, const cJSON *paras)
{
    if (command_name == NULL) {
        return 1;
    }

    if (strcmp(command_name, "start") == 0) {
        set_running(1);    // 启动烘干机
        return 0;
    }
    if (strcmp(command_name, "stop") == 0) {
        set_running(0);    // 停止烘干机
        return 0;
    }
    if (strcmp(command_name, "toggle") == 0) {
        dryer_state_t state = get_state_snapshot();
        set_running(!state.running);  // 切换运行状态
        return 0;
    }
    if (strcmp(command_name, "set_mode") == 0 || strcmp(command_name, "switch_mode") == 0) {
        dry_mode_t mode = DRY_MODE_STANDARD;
        if (parse_mode_from_json(paras, &mode) == 0) {
            set_mode(mode);  // 设置烘干模式
            return 0;
        }
    }

    return 1;  // 不支持的命令
}

/**
 * @brief 向云端发送命令执行结果
 * @param request_id 请求ID
 * @param ret_code 执行结果码（0=成功，1=失败）
 *
 * 响应云端指令的执行状态，用于命令响应机制
 */
static void send_cloud_request_code(const char *request_id, int ret_code)
{
    char request_topic[128] = {0};
    // 构建响应主题：$oc/devices/{DEVICE_ID}/sys/commands/response/request_id={request_id}
    if (snprintf(request_topic, sizeof(request_topic), MQTT_TOPIC_PUB_COMMANDS_REQ, DEVICE_ID, request_id) <= 0) {
        return;
    }

    // 发送JSON格式的执行结果
    if (ret_code == 0) {
        MQTTClient_pub(request_topic, (unsigned char *)"{\"result_code\":0}", strlen("{\"result_code\":0}"));
    } else {
        MQTTClient_pub(request_topic, (unsigned char *)"{\"result_code\":1}", strlen("{\"result_code\":1}"));
    }
}

/**
 * @brief 包装设备属性数据为MQTT消息格式
 * @param buffer 输出缓冲区
 * @param len 缓冲区长度
 * @return 成功返回0，失败返回-1
 *
 * 按照IoTDA规范构建属性上报消息，包含服务数组结构
 */
static int package_properties_payload(char *buffer, size_t len)
{
    dryer_state_t state = get_state_snapshot();

    // IoTDA 属性上报采用 services 数组格式
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON *services = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "services", services);

    cJSON *service = cJSON_CreateObject();
    cJSON_AddItemToArray(services, service);
    cJSON_AddStringToObject(service, "service_id", "dryer");  // 设备服务ID

    cJSON *properties = cJSON_CreateObject();
    cJSON_AddItemToObject(service, "properties", properties);

    // 添加设备属性信息
    cJSON_AddStringToObject(properties, "status", state.running ? "RUNNING" : "STOPPED");
    cJSON_AddStringToObject(properties, "mode", mode_to_string(state.mode));
    cJSON_AddNumberToObject(properties, "humidity", state.humidity);
    cJSON_AddNumberToObject(properties, "temperature", state.temperature);
    cJSON_AddNumberToObject(properties, "countdown", state.countdown);

    char *printed = cJSON_PrintUnformatted(root);
    int ret = -1;
    if (printed != NULL) {
        if ((int)snprintf(buffer, len, "%s", printed) >= 0) {
            ret = 0;
        }
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief MQTT客户端订阅消息回调函数
 * @param topic 接收到的消息主题
 * @param payload 消息载荷
 * @return 0表示成功
 *
 * 处理云端下发的控制指令，解析并执行相应的命令
 */
static int8_t mqtt_client_sub_callback(unsigned char *topic, unsigned char *payload)
{
    if (topic == NULL || payload == NULL) {
        return -1;
    }

    printf("MQTT recv topic: %s\r\n", topic);
    printf("MQTT recv payload: %s\r\n", payload);

    // 解析JSON消息
    cJSON *root = cJSON_Parse((const char *)payload);
    int ret_code = 1;
    if (root != NULL) {
        cJSON *command_name = cJSON_GetObjectItem(root, "command_name");
        cJSON *paras = cJSON_GetObjectItem(root, "paras");
        if (command_name != NULL && cJSON_IsString(command_name)) {
            ret_code = apply_cloud_command(command_name->valuestring, paras);  // 执行命令
        }
        cJSON_Delete(root);
    }

    // 检查是否需要响应（包含request_id的消息需要回复执行结果）
    const char *request_key = "request_id=";
    char *pos = strstr((const char *)topic, request_key);
    if (pos != NULL) {
        char request_id[64] = {0};
        pos += strlen(request_key);
        snprintf(request_id, sizeof(request_id), "%s", pos);
        send_cloud_request_code(request_id, ret_code);  // 发送执行结果
    }

    return 0;
}

/**
 * @brief MQTT消息发送任务
 * @param arg 任务参数（未使用）
 *
 * 定期向云端上报设备属性数据，包含状态、模式、温湿度等信息
 */
static void mqtt_send_task(void *arg)
{
    (void)arg;
    char publish_topic[128] = {0};
    char payload[256] = {0};

    while (1) {
        memset(publish_topic, 0, sizeof(publish_topic));
        memset(payload, 0, sizeof(payload));

        // 构建属性上报主题：$oc/devices/{DEVICE_ID}/sys/properties/report
        if (snprintf(publish_topic, sizeof(publish_topic), MQTT_TOPIC_PUB_PROPERTIES, DEVICE_ID) > 0 &&
            package_properties_payload(payload, sizeof(payload)) == 0) {
            MQTTClient_pub(publish_topic, (unsigned char *)payload, strlen(payload));
        }
        sleep(MQTT_SEND_INTERVAL_SEC);  // 3秒间隔上报
    }
}

/**
 * @brief MQTT消息接收任务
 * @param arg 任务参数（未使用）
 *
 * 循环监听MQTT消息，接收云端下发的控制指令
 */
static void mqtt_recv_task(void *arg)
{
    (void)arg;
    while (1) {
        MQTTClient_sub();  // 阻塞式订阅消息，等待云端指令
        sleep(1);
    }
}

/**
 * @brief Wi-Fi和MQTT初始化函数
 * @return 0表示成功，-1表示失败
 *
 * 完成Wi-Fi连接、MQTT服务器连接、客户端初始化和指令订阅
 */
static int wifi_mqtt_init(void)
{
    char sub_topic[128] = {0};

    // 1. Wi-Fi 连接到指定网络
    if (WiFi_connectHotspots(WIFI_SSID, WIFI_PAWD) != WIFI_SUCCESS) {
        printf("[wifi] connect failed\r\n");
        return -1;
    }
    sleep(2);  // 等待连接稳定

    // 2. 连接 MQTT 服务器
    if (MQTTClient_connectServer(SERVER_IP_ADDR, SERVER_IP_PORT) != WIFI_SUCCESS) {
        printf("[mqtt] connect server failed\r\n");
        return -1;
    }
    sleep(1);

    // 3. 初始化MQTT客户端身份（设备ID、用户名、密码）
    if (MQTTClient_init(MQTT_CLIENT_ID, MQTT_USER_NAME, MQTT_PASS_WORD) != WIFI_SUCCESS) {
        printf("[mqtt] client init failed\r\n");
        return -1;
    }
    sleep(1);

    // 4. 订阅云端下行指令主题
    if (snprintf(sub_topic, sizeof(sub_topic), MQTT_TOPIC_SUB_COMMANDS, DEVICE_ID) <= 0) {
        printf("[mqtt] sub topic build failed\r\n");
        return -1;
    }
    // 设置消息回调函数
    p_MQTTClient_sub_callback = &mqtt_client_sub_callback;
    if (MQTTClient_subscribe(sub_topic) != WIFI_SUCCESS) {
        printf("[mqtt] subscribe failed\r\n");
        return -1;
    }
    return 0;
}

/**
 * @brief 主控制任务
 * @param arg 任务参数（未使用）
 *
 * 核心控制逻辑：DHT11传感器数据采样、湿度阈值判断、倒计时控制、状态更新
 */
static void control_task(void *arg)
{
    (void)arg;
    uint8_t temp = 0;
    uint8_t hum = 0;

    // DHT11 初始化重试，确保传感器可用
    while (dht11_init() != 0) {
        printf("DHT11 init failed, retry...\r\n");
        usleep(500 * 1000);
    }
    printf("DHT11 init success\r\n");

    // 周期采样 + 湿度判定 + 倒计时关机逻辑
    while (1) {
        // 读取DHT11传感器数据
        if (dht11_read_data(&temp, &hum) == 0) {
            update_sensor_values(temp, hum);
            printf("Temp=%uC Humidity=%u%%\r\n", temp, hum);

            // 智能烘干控制逻辑
            int should_stop = 0;
            osMutexAcquire(g_state_lock, osWaitForever);
            if (g_state.running) {
                if (hum <= HUMIDITY_THRESHOLD) {  // 湿度达到阈值，开始倒计时
                    if (g_state.countdown < 0) {
                        g_state.countdown = COUNTDOWN_SECONDS;  // 开始10秒倒计时
                    } else if (g_state.countdown > 0) {
                        g_state.countdown -= 1;  // 倒计时递减
                    } else {
                        should_stop = 1;  // 倒计时结束，停止烘干
                    }
                } else {
                    g_state.countdown = -1;  // 湿度未达标，重置倒计时
                }
            } else {
                g_state.countdown = -1;  // 未运行时保持倒计时重置状态
            }
            osMutexRelease(g_state_lock);

            // 执行停止烘干逻辑
            if (should_stop) {
                printf("Humidity below threshold, stopping dryer\r\n");
                set_running(0);
            }
        } else {
            printf("DHT11 read failed\r\n");
        }

        // 投递最新数据到 OLED 显示队列并发信号通知刷新
        if (g_sensor_queue != NULL) {
            dryer_state_t snap = get_state_snapshot();
            sensor_msg_t msg = {
                .temp = temp,
                .hum = hum,
                .countdown = snap.countdown,
                .running = snap.running,
                .mode = snap.mode
            };
            (void)osMessageQueueTryPut(g_sensor_queue, &msg, 0, 0);
        }
        if (g_oled_sem != NULL) {
            (void)osSemaphoreRelease(g_oled_sem);
        }

        usleep(SENSOR_PERIOD_MS * 1000);  // 1秒采样周期
    }
}

/**
 * @brief 电机PWM控制任务
 * @param arg 任务参数（未使用）
 *
 * 软件PWM实现电机速度控制，根据不同模式调整占空比
 * 快速模式85%占空比，标准模式65%，温柔模式45%
 */
static void motor_task(void *arg)
{
    (void)arg;
    dc_motor_init();

    // 软件 PWM 实现电机速度控制，占空比按档位切换
    while (1) {
        dryer_state_t state = get_state_snapshot();
        if (state.running) {
            // 根据当前模式计算PWM占空比
            uint32_t on_time = (MOTOR_PERIOD_US * g_mode_duty[state.mode]) / 100;
            if (on_time > MOTOR_PERIOD_US) {
                on_time = MOTOR_PERIOD_US;
            }
            uint32_t off_time = MOTOR_PERIOD_US - on_time;

            // 执行PWM控制：高电平时间 + 低电平时间 = 完整周期
            if (on_time > 0) {
                DC_MOTOR(1);  // 电机开启
                usleep(on_time);
            }
            DC_MOTOR(0);  // 电机关闭
            if (off_time > 0) {
                usleep(off_time);
            }
        } else {
            DC_MOTOR(0);  // 未运行时关闭电机
            usleep(MOTOR_IDLE_SLEEP_US);  // 延长空闲休眠时间
        }
    }
}

/**
 * @brief 按键处理任务
 * @param arg 任务参数（未使用）
 *
 * 按键功能定义：
 * KEY1：启停烘干机，切换运行状态
 * KEY2：切换烘干模式（循环切换）
 */
static void key_task(void *arg)
{
    (void)arg;
    key_init();

    // KEY1 控制启停，KEY2 控制模式切换
    while (1) {
        uint8_t key = key_scan(0);
        if (key == KEY1_PRESS) {
            // KEY1：启停烘干机
            dryer_state_t state = get_state_snapshot();
            set_running(!state.running);  // 切换运行状态
            printf("Key1 pressed, dryer %s\r\n", state.running ? "stop" : "start");
            if (g_oled_sem != NULL) {
                (void)osSemaphoreRelease(g_oled_sem);  // 通知OLED刷新显示
            }
            usleep(300 * 1000);  // 按键去抖延时
        } else if (key == KEY2_PRESS) {
            // KEY2：循环切换烘干模式
            dryer_state_t state = get_state_snapshot();
            dry_mode_t next = (state.mode + 1) % DRY_MODE_MAX;  // 循环切换
            set_mode(next);
            printf("Key2 pressed, switch mode to %s\r\n", mode_to_string(next));
            if (g_oled_sem != NULL) {
                (void)osSemaphoreRelease(g_oled_sem);  // 通知OLED刷新显示
            }
            usleep(300 * 1000);  // 按键去抖延时
        } else {
            usleep(30 * 1000);  // 正常扫描间隔
        }
    }
}

/**
 * @brief OLED显示任务
 * @param arg 任务参数（未使用）
 *
 * OLED显示格式：
 * 第1行：设备运行状态 (RUN/STOP)
 * 第2行：烘干模式 (Fast/Standard/Soft)
 * 第3行：温湿度显示 (H:xx% T:xx°C)
 * 第4行：剩余时间显示 (运行时显示倒计时)
 */
static void oled_task(void *arg)
{
    (void)arg;
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];
    sensor_msg_t latest = {0};

    // OLED硬件初始化
    oled_init();
    oled_display_on();
    oled_clear();
    oled_refresh_gram();
    printf("OLED task started\r\n");

    // 上电自检提示，避免屏幕未刷新的空白
    oled_showstring(0, 0, (const uint8_t *)"Laundry Dryer", 12);
    oled_showstring(0, 15, (const uint8_t *)"OLED Ready", 12);
    oled_refresh_gram();
    usleep(500 * 1000);

    // OLED实时显示状态循环
    while (1) {
        // 等待新数据信号，超时则使用当前状态
        if (g_oled_sem != NULL && osSemaphoreAcquire(g_oled_sem, 400) == osOK) {
            if (g_sensor_queue != NULL) {
                // 从队列中获取最新数据，旧数据会被覆盖
                while (osMessageQueueGet(g_sensor_queue, &latest, NULL, 0) == osOK) {
                    // 取最新一条
                }
            }
        }

        // 如果队列或信号量不可用，从全局状态获取数据
        if (g_sensor_queue == NULL || g_oled_sem == NULL) {
            dryer_state_t state = get_state_snapshot();
            latest.temp = state.temperature;
            latest.hum = state.humidity;
            latest.countdown = state.countdown;
            latest.running = state.running;
            latest.mode = state.mode;
        }

        // 格式化显示内容
        snprintf(line1, sizeof(line1), "Dryer: %s", latest.running ? "RUN" : "STOP");
        snprintf(line2, sizeof(line2), "Mode: %s", mode_to_string(latest.mode));
        snprintf(line3, sizeof(line3), "H:%u%%  T:%uC", latest.hum, latest.temp);
        if (latest.running && latest.countdown >= 0) {
            snprintf(line4, sizeof(line4), "Remain: %ds", latest.countdown);
        } else {
            snprintf(line4, sizeof(line4), "Remain: --");
        }

        // OLED显示更新
        oled_clear();
        oled_showstring(0, 0, (const uint8_t *)line1, 12);
        oled_showstring(0, 15, (const uint8_t *)line2, 12);
        oled_showstring(0, 30, (const uint8_t *)line3, 12);
        oled_showstring(0, 45, (const uint8_t *)line4, 12);
        oled_refresh_gram();

        usleep(400 * 1000);  // 400ms刷新周期
    }
}

/**
 * @brief 创建FreeRTOS任务
 * @param func 任务函数
 * @param task_id 任务ID输出参数
 * @param name 任务名称
 * @param stack 栈大小（字节）
 * @param 任务优先级
 *
 * 统一的任务创建接口，便于错误处理和调试
 */
static void create_task(osThreadFunc_t func, osThreadId_t *task_id, const char *name, uint32_t stack, osPriority_t priority)
{
    osThreadAttr_t attr = {
        .name = name,
        .attr_bits = 0U,
        .cb_mem = NULL,
        .cb_size = 0U,
        .stack_mem = NULL,
        .stack_size = stack,
        .priority = priority
    };
    *task_id = osThreadNew(func, NULL, &attr);
    if (*task_id == NULL) {
        printf("Create task %s failed\r\n", name);
    }
}

/**
 * @brief 智慧洗衣房系统初始化函数
 *
 * 系统初始化流程：
 * 1. 创建全局状态互斥锁
 * 2. 初始化设备状态（默认标准模式、停止状态）
 * 3. 初始化LED指示灯
 * 4. 创建消息队列和信号量用于任务间通信
 * 5. 创建各个任务：控制、电机、按键、OLED
 * 6. 初始化Wi-Fi和MQTT连接（可选，失败时进入离线模式）
 */
static void smart_laundry_demo(void)
{
    printf("Smart laundry dryer demo start\r\n");

    // 1. 创建全局状态互斥锁，保护共享数据
    g_state_lock = osMutexNew(NULL);
    if (g_state_lock == NULL) {
        printf("state mutex create failed\r\n");
        return;
    }

    // 2. 初始化设备默认状态
    set_mode(DRY_MODE_STANDARD);    // 默认标准烘干模式
    set_running(0);                  // 初始状态为停止
    set_countdown(-1);              // 倒计时重置

    // 3. 初始化LED指示灯
    led_init();

    // 4. 创建任务间通信机制
    g_sensor_queue = osMessageQueueNew(8, sizeof(sensor_msg_t), NULL);  // 传感器数据队列
    g_oled_sem = osSemaphoreNew(8, 0, NULL);                            // OLED刷新信号量
    if (g_sensor_queue == NULL || g_oled_sem == NULL) {
        printf("queue or semaphore create failed\r\n");
    }
    // 投递初始状态，确保 OLED 有数据可读
    if (g_sensor_queue != NULL) {
        sensor_msg_t init_msg = {0};
        (void)osMessageQueuePut(g_sensor_queue, &init_msg, 0, 0);
    }

    // 5. 创建各个功能任务
    create_task((osThreadFunc_t)control_task, &g_control_task_id, "dryer_ctrl", 4096, osPriorityNormal1);  // 主控制任务（最高优先级）
    create_task((osThreadFunc_t)motor_task, &g_motor_task_id, "motor_pwm", 2048, osPriorityNormal);        // 电机PWM控制
    create_task((osThreadFunc_t)key_task, &g_key_task_id, "keys", 2048, osPriorityNormal);                  // 按键处理
    create_task((osThreadFunc_t)oled_task, &g_oled_task_id, "oled", 4096, osPriorityNormal);              // OLED显示

    // 6. Wi-Fi/MQTT 初始化（放在任务创建之后，避免阻塞 OLED 刷新）
    if (wifi_mqtt_init() == 0) {
        // 云端连接成功，创建MQTT相关任务
        create_task((osThreadFunc_t)mqtt_send_task, &g_mqtt_send_task_id, "mqtt_send", 8192, osPriorityNormal);
        create_task((osThreadFunc_t)mqtt_recv_task, &g_mqtt_recv_task_id, "mqtt_recv", 4096, osPriorityNormal);
    } else {
        printf("Cloud connection skipped, running offline\r\n");  // 离线模式运行
    }
}

SYS_RUN(smart_laundry_demo);
