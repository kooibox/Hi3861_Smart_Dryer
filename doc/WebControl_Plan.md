# 智慧洗衣房 Web 控制部署方案（REST + Python paho-mqtt 桥接）

## 目标
在云服务器部署网页控制（手机/PC 浏览器），通过华为云 IoTDA 远程控制/查看 Hi3861 烘干机设备。设备固件保持现有 MQTT 接入与命令格式（`start/stop/toggle/set_mode` + `gear`）。

## 架构
1. 设备：Hi3861 → MQTT → 华为云 IoTDA（保持现有固件）。  
2. 云服务器：前端 H5 + 后端（推荐 Python 方案）。  
3. 后端与 IoTDA 交互两种选项：
   - **REST 方案（推荐）**：后端调用 IoTDA 北向 API 下发命令、查询影子；前端仅访问自建后端。
   - **MQTT 桥接方案（Python paho-mqtt）**：后端作为 MQTT 客户端连接 IoTDA，订阅设备上报、发布命令；前端通过 HTTP/WebSocket 调用后端。

## 设备侧协议（复用现有固件）
- 上报属性 Topic：`$oc/devices/{deviceId}/sys/properties/report`  
  Payload 示例：`{"services":[{"service_id":"dryer","properties":{"status":"RUNNING/STOPPED","mode":"Fast/Standard/Soft","humidity":17,"temperature":30,"countdown":-1}}]}`
- 命令 Topic：`$oc/devices/{deviceId}/sys/commands/#`  
  Payload 示例：`{"command_name":"set_mode","paras":{"gear":2}}` 或 `{"command_name":"start","paras":{}}`

## 方案 A：REST/HTTPS（简洁、易管控）
后端使用 AK/SK 获取 IAM Token，调用 IoTDA 北向接口：
- 下发命令：`POST /v5/iot/{project_id}/devices/{device_id}/commands`
- 查询影子：`GET /v5/iot/{project_id}/devices/{device_id}/shadow`

后端对外提供：
- `POST /api/devices/{id}/command`（body: `command_name`, `paras`）
- `GET /api/devices/{id}/shadow`

前端通过 HTTPS 调用上述接口。可选轮询或 WebSocket 推送影子。

## 方案 B：Python paho-mqtt 桥接（实时订阅）
用途：后端直接连 IoTDA MQTT，前端通过后端转发。便于快速搭建 Demo。

示例参数（请替换为你的设备信息）：
```
MQTT_SERVER = "117.78.5.125"
MQTT_PORT   = 1883
CLIENT_ID   = "<deviceId>_demo_0_0_XXXX"
USERNAME    = "<deviceId>_demo"
PASSWORD    = "<device_password>"
SUB_TOPIC   = "$oc/devices/<deviceId>/sys/commands/#"
PUB_TOPIC   = "$oc/devices/<deviceId>/sys/properties/report"
```

示例代码（精简改进版，避免递归 Timer）：
```python
import json, time, threading
import paho.mqtt.client as mqtt

MQTT_SERVER = "117.78.5.125"
MQTT_PORT   = 1883
CLIENT_ID   = "your_client_id"
USERNAME    = "your_username"
PASSWORD    = "your_password"
SUB_TOPIC   = "$oc/devices/your_device/sys/commands/#"
PUB_TOPIC   = "$oc/devices/your_device/sys/properties/report"
PUBLISH_INTERVAL = 10

message_payload = {
    "services": [
        {"service_id": "dryer",
         "properties": {"status": "STOPPED", "mode": "Standard",
                        "humidity": 17, "temperature": 30, "countdown": -1}}
    ]
}

def on_connect(client, userdata, flags, rc):
    print(f"Connected rc={rc}")
    client.subscribe(SUB_TOPIC)

def on_message(client, userdata, msg):
    print(f"[CMD] {msg.topic}: {msg.payload.decode(errors='ignore')}")
    # TODO: 解析 command_name/paras，调用你的业务逻辑或转发到 HTTP 接口

def publisher_loop(client):
    while True:
        payload = json.dumps(message_payload)
        client.publish(PUB_TOPIC, payload)
        print(f"[PUB] {payload}")
        time.sleep(PUBLISH_INTERVAL)

client = mqtt.Client(client_id=CLIENT_ID, protocol=mqtt.MQTTv311)
client.username_pw_set(USERNAME, PASSWORD)
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_SERVER, MQTT_PORT, keepalive=60)

threading.Thread(target=publisher_loop, args=(client,), daemon=True).start()
client.loop_forever()
```

部署建议：
- 将设备参数改为你的烘干机设备（`69254fd5bf22cc5a8c09cdcf` 等），service_id 改为 `dryer`，命令解析匹配 `start/stop/toggle/set_mode` 和 `gear`。
- 封装一个简单的 Flask/FastAPI：提供 HTTP 接口给前端，调用时发布 MQTT 命令；同时把订阅到的属性/命令回执存内存或缓存，通过 WebSocket/HTTP 返回前端。
- 使用 systemd/Docker 部署，配置环境变量存放凭据。

## 前端功能建议
- 状态卡片：运行/停止、档位、湿度/温度、倒计时。
- 控制按钮：启动/停止、档位选择（1/2/3）。
- 命令回执/时间戳；若使用 WebSocket，实时刷新。

## 安全
- 不在前端暴露 AK/SK 或设备密码；后端通过环境变量读取。
- 全站 HTTPS；前端调用后端需鉴权（JWT/Session/API Key）。

## 选择建议
- 快速上线/易运维：优先 REST 方案（北向 API）。  
- 需要实时订阅/推送：用 Python paho-mqtt 桥接（或 REST + 定时轮询影子）。  
- 两者可并行：REST 负责控制/查询，MQTT 负责实时订阅并推送前端。  

## 设备侧无需改动
- 仍用现有固件命令/属性；云端控制与 IoTDA 控制台一致。  
