# Hi3861 Smart Dryer（智慧洗衣房烘干监测与控制）

面向 Hi3861 的湿度闭环烘干机示例：本地按键 + OLED 显示 + 三档 PWM 电机控制 + DHT11 湿温采集，支持华为云 IoTDA 物联云与 Web 远程控制。

## 软硬件组成
- **硬件**：Hi3861 开发板；DHT11 湿温度传感器；直流电机（GPIO14 软件 PWM 驱动）；按键 key1=GPIO11（启停），key2=GPIO12（档位）；状态 LED=GPIO2；OLED I2C（默认引脚）；5V/3.3V 供电及公共地。
- **固件**：`src/smart_laundry.c` 负责状态机、DHT11 采集、三档 PWM、电机启停、倒计时、按键与 OLED 刷新，以及 MQTT 上云。
- **云端/Web**：默认通过 IoTDA MQTT 上报/收命令（service_id=`dryer`）；`web_control/` 提供 Flask + Chart.js 的 Web 控制台和 REST API，可 Docker 化部署。
- **构建产物**：`src/out/hispark_pegasus/wifiiot_hispark_pegasus/Hi3861_wifiiot_app_allinone.bin`（示例目标）。

## 快速开始
### 1) 配置并编译固件
1. 修改 `src/smart_laundry.c` 顶部宏，填入 Wi-Fi 与 IoTDA 参数：`WIFI_SSID`、`WIFI_PAWD`、`DEVICE_ID`、`MQTT_CLIENT_ID`、`MQTT_USER_NAME`、`MQTT_PASS_WORD`、`SERVER_IP_ADDR`(`MQTT` 终端节点 IP)、`SERVER_IP_PORT`(默认 1883)。
2. 进入源码根目录（示例路径 `src`）：  
   ```bash
   hb set   # 选择 wifiiot_hispark_pegasus 等包含该 demo 的产品
   hb build --target //vendor/pzkj/pz_hi3861/demo/49_Exam:SmartLaundry
   ```
3. 烧录 `Hi3861_wifiiot_app_allinone.bin` 到板子，串口 115200 8N1 查看日志。

### 2) 接线与本地使用
- 电机驱动：GPIO14 → 电机；注意外部驱动/续流保护。  
- 传感器：DHT11 数据 → GPIO7；OLED 按默认 I2C 引脚连接。  
- 按键：key1=GPIO11（启停/切换运行），key2=GPIO12（切换档位 Fast→Standard→Soft）。  
- 湿度低于阈值（默认 40%）后启动 10 秒倒计时，归零自动停机；倒计时中湿度回升会重置。

### 3) Web 控制/云端
- 在 `web_control/` 中复制 `.env.example` 为 `.env`，填入 `PROJECT_ID`、`DEVICE_ID`、`IOTDA_ENDPOINT`、`IOTDA_AK`、`IOTDA_SK`、`SERVICE_ID` 等。
- Docker 运行（示例）：  
  ```bash
  cd web_control
  docker build -t smart-laundry-web:latest .
  docker run -d --name smart-laundry-web -p 5000:5000 --env-file .env smart-laundry-web:latest
  ```
- 接口：`GET /api/state` 获取影子/最新属性；`POST /api/command` 下发 `start|stop|toggle|set_mode|switch_mode`，如  
  ```bash
  curl -X POST http://localhost:5000/api/command \
    -H "Content-Type: application/json" \
    -d '{"command_name":"set_mode","paras":{"gear":1}}'
  ```
- Web UI：浏览器打开 `http://<服务器>:5000/`，可查看湿度曲线、状态与日志，发送启停/档位命令。

## 核心原理
- **湿度闭环**：周期读取 DHT11，湿度低于阈值触发倒计时（默认 10 秒）；计时结束停机，湿度回升重置计时，防止过烘或误触发。
- **三档 PWM 电机**：软件 PWM 周期 20 ms，占空比映射档位（Fast 85%，Standard 65%，Soft 45%），非运行时短睡眠降低占用。
- **状态显示与交互**：按键中断/轮询切换运行和档位，OLED 每 400 ms 刷新运行状态、档位、湿度/温度、倒计时，LED 指示运行。
- **云端协议**：IoTDA 标准 Topic  
  - 上报：`$oc/devices/{deviceId}/sys/properties/report`，payload `{services:[{service_id:"dryer",properties:{status,mode,humidity,temperature,countdown}}]}`  
  - 命令：`$oc/devices/{deviceId}/sys/commands/#`，支持 `start|stop|toggle|set_mode|switch_mode` + `gear`。执行结果回执 `.../response/request_id=...`，`result_code:0` 表示成功。
- **Web 桥接**：后端可调用 IoTDA 北向 REST（推荐）或 MQTT 桥接，统一对外暴露 `/api/state` 与 `/api/command`；前端使用 Chart.js 渲染湿度曲线并记录命令日志。

## 目录索引
- 固件源码：`src/smart_laundry.c`
- 构建脚本：`src/BUILD.gn`
- 文档：`doc/SmartLaundry.md`（固件功能与参数）、`doc/SmartLaundry_IoTDA.md`（Topic/物模型）、`doc/SmartLaundry_WebControl.md`、`doc/WebControl_Plan.md`
- Web 控制：`web_control/app.py`、静态页 `web_control/static/index.html`、Dockerfile/部署说明 `web_control/README.md`

## 常用问题
- 远程命令无回执：检查 IoTDA endpoint、设备密钥与 `service_id=dryer` 是否一致，并确认 MQTT 连接成功。
- 电机转速/力矩不合适：调整 `g_mode_duty[]` 或外接驱动与电源；确保与传感器共地。
- 仅本地演示：即便 Wi-Fi/MQTT 失败，按键+OLED+PWM 仍可离线工作。
