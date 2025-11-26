# 智慧洗衣房衣物烘干监测系统

面向 Hi3861 的示例：湿度闭环的烘干机模拟，三档 PWM 滚筒、电机按键控制、OLED 显示，以及华为云 IoTDA 上报/远程控制。

## 功能概述
- 湿度检测与完成判断：DHT11 实时检测，湿度 ≤40% 触发 10 秒倒计时，倒计时结束自动停机。
- 三档滚筒 PWM：Fast/Standard/Soft，占空比分别 85%/65%/45%，软件 PWM 驱动直流电机。
- 按键交互：key1 启动/停止，key2 切换档位。
- OLED 实时显示：运行状态、档位、当前湿度/温度、剩余倒计时。
- 云端同步：定期上报属性到 IoTDA；云端可下发 start/stop/toggle/set_mode 等指令控制本地。

## 核心代码结构
文件：`src/vendor/pzkj/pz_hi3861/demo/49_Exam/src/smart_laundry.c`

- 状态管理（`dryer_state_t` + `g_state_lock`）  
  统一保存运行标志、档位、温湿度、倒计时；通过互斥锁在各任务间安全读写。

- 湿度与倒计时控制（`control_task`）  
  1) 周期读取 DHT11。  
  2) 湿度低于阈值（40%）即启动 10 秒倒计时；倒计时归零后停机。湿度回升时重置倒计时为未开始状态。

- 电机 PWM（三档）（`motor_task`）  
  软件 PWM，周期 20 ms；档位映射到占空比数组 `g_mode_duty`。运行时按占空比导通/关断 GPIO，未运行时进入短睡眠降低占用。

- 按键切换（`key_task`）  
  key1 翻转运行状态，key2 轮换档位（Fast→Standard→Soft）。

- OLED 显示（`oled_task`）  
  每 400 ms 刷新一次，展示运行/停机、档位、湿度/温度、剩余倒计时（湿度未达阈值时显示 “--”）。

- MQTT 上传与下行（`mqtt_send_task` / `mqtt_recv_task`）  
  1) 定期封装属性 JSON（services 结构）并发布到 `$oc/devices/{deviceId}/sys/properties/report`。  
  2) 订阅 `$oc/devices/{deviceId}/sys/commands/#`，解析 `command_name` 与参数：  
     - `start` / `stop` / `toggle`：启停烘干。  
     - `set_mode` / `switch_mode`：切换档位，支持 `mode` 字符串（fast/standard/soft）或数字 0/1/2。  
  3) 执行结果通过 `.../response/request_id=...` 返回 `result_code`（0 成功）。

## 使用方法
1. **填入账号与网络信息**  
   打开 `src/vendor/pzkj/pz_hi3861/demo/49_Exam/src/smart_laundry.c`，替换顶部宏：
   - `WIFI_SSID` / `WIFI_PAWD`
   - `DEVICE_ID` / `MQTT_CLIENT_ID` / `MQTT_USER_NAME` / `MQTT_PASS_WORD`
   - `SERVER_IP_ADDR`（IoTDA MQTT 终端节点 IP），`SERVER_IP_PORT`（默认 1883）

2. **选择产品并编译**（在仓库根目录）  
   ```bash
   hb set  # 选择 wifiiot_hispark_pegasus 等包含此 demo 的产品
   hb build --target //vendor/pzkj/pz_hi3861/demo/49_Exam:SmartLaundry
   ```
   产物：`src/out/hispark_pegasus/wifiiot_hispark_pegasus/Hi3861_wifiiot_app_allinone.bin`。

3. **烧录与连线**  
   - 直流电机接 GPIO14（`DC_MOTOR_PIN`）。  
   - DHT11 接 GPIO7；OLED I2C 保持默认引脚。  
   - key1=GPIO11，key2=GPIO12；LED 接 GPIO2。  
   - 使用烧录工具（如 `hid_download_py -p COMx OHOS_Image.bin`）写入固件，串口 115200 8N1 观察日志。

4. **本地体验**  
   - key1 按一次启动/停止电机；key2 切换档位（OLED 会显示）。  
   - 湿度降到 40% 以下后开始 10 秒倒计时，归零自动停机。

5. **云端控制/观测（IoTDA）**  
   - 订阅/查看 Topic：`$oc/devices/{deviceId}/sys/properties/report` 可见运行/档位/湿温/倒计时。  
   - 下发命令：`$oc/devices/{deviceId}/sys/commands/#`  
     示例 payload：
     ```json
     {"command_name":"set_mode","paras":{"mode":"fast"}}
     ```
     或
     ```json
     {"command_name":"start","paras":{}}
     ```

## 关键参数可调
- `HUMIDITY_THRESHOLD`：湿度阈值（默认 40%）。  
- `COUNTDOWN_SECONDS`：达标后延时停机秒数（默认 10）。  
- `g_mode_duty[]`：三档占空比（默认 85/65/45）。  
- `MOTOR_PERIOD_US`：PWM 周期（默认 20 ms）。

## 调试建议
- 如果只想离线演示，可保留 Wi-Fi/MQTT 失败日志，不影响本地按键 + OLED + PWM 功能。  
- 若云端无回执，确认 `SERVER_IP_ADDR` 与证书/鉴权信息；串口检查 `[wifi]` / `[mqtt]` 日志。  
- 如电机转速过高，可下调占空比或增大 `MOTOR_PERIOD_US`。  
- DHT11 读失败通常为供电/线序或延时问题，可检查接线和上电时序。  
