# 智慧洗衣房 Web 控制方案（云端部署）

目标：在云服务器部署网页控制服务，手机/PC 通过浏览器控制 Hi3861 设备，并查看状态。设备侧保持现有 IoTDA MQTT 接入，无需改动固件。

## 推荐架构
1) 设备：现有 Hi3861 固件，通过 MQTT 连接华为云 IoTDA。  
2) 云服务器（自建）：部署 Web 前端 + 后端服务。  
3) 后端与 IoTDA 交互方式（两选一）：
   - **REST/HTTPS 调用 IoTDA**：使用华为云 IoTDA Northbound API（下发命令、查询属性/日志），后端持有 AK/SK，前端通过自建后端代理调用。
   - **MQTT 直连 IoTDA**：后端作为“云端控制客户端”连接 IoTDA 的 MQTT 终端节点，订阅设备命令/上报 Topic，与 WebSocket 进行桥接。

## 方案 A：REST/HTTPS 调用 IoTDA（推荐）
流程：前端 → 自建后端 → 华为云 IAM 认证换 Token → IoTDA REST API。  
核心 API（华为云 IoTDA 北向接口）：
- 下发命令：`POST /v5/iot/{project_id}/devices/{device_id}/commands`（包含 command_name 与 paras，例如 `start` / `stop` / `set_mode`）。
- 查询设备影子/属性：`GET /v5/iot/{project_id}/devices/{device_id}/shadow`。
- 查询消息/日志（可选）：`GET /v5/iot/{project_id}/devices/{device_id}/messages`。

后端实现步骤：
1. 在华为云创建访问密钥（AK/SK），后端使用 IAM 申请 Token（或使用签名方式调用 API 网关）。  
2. 封装命令接口：
   ```http
   POST /api/devices/{deviceId}/command
   body: { "command_name": "set_mode", "paras": {"gear": 1} }
   ```
   后端将其转发为 IoTDA 下发命令请求。  
3. 封装状态查询接口：
   ```http
   GET /api/devices/{deviceId}/shadow
   ```
   后端调用 IoTDA 影子查询返回最新属性（status/mode/humidity/temperature/countdown）。  
4. 前端（Web/Mobile H5）通过 HTTPS 调用后端接口，展示状态并发送控制指令。  
5. 可选：开启 WebSocket 通道，后端周期（或事件）推送最新影子/属性到前端。

优点：无需在服务器上维持 MQTT 长连接；靠 IoTDA 接口即可控制/查询。适合多设备管理与权限控制。

## 方案 B：后端直连 MQTT 终端节点
流程：前端 → 后端 WebSocket → 后端 MQTT 客户端 → IoTDA。  
后端与设备一样，使用 IoTDA 终端节点、设备认证信息或桥接证书，订阅：
- `$oc/devices/{device_id}/sys/properties/report`（属性上报）
- `$oc/devices/{device_id}/sys/commands/#`（可选监听回执）
发送：
- `$oc/devices/{device_id}/sys/commands/`...（命令下发，同设备侧）

前端发控制请求给后端，后端通过 MQTT 发布到 IoTDA。属性上报由后端订阅，转换为 WebSocket 推送给前端。

优点：实时性好；缺点：需自管 MQTT 连接/重连，证书/鉴权管理更复杂。

## 前端 UI 要点
- 状态展示：运行/停止、当前档位（Fast/Standard/Soft）、湿度/温度、倒计时。  
- 控制按钮：启动/停止、档位切换（gear=1/2/3）。  
- 日志/回执：显示最近命令执行结果与时间。

## 安全与部署
- 后端暴露 HTTPS，接入认证（JWT/Session）；避免直接暴露 AK/SK。  
- 若用 REST 方案，AK/SK 仅在后端；前端永远不接触云密钥。  
- 部署形态：可用 Docker（Nginx 反代前端，Node.js/Go/Python 作为后端）。  
- 配置 IoTDA 终端节点、project_id、device_id、AK/SK 等为环境变量。

## 示例后端伪代码（REST 方式，Node.js）
```js
// POST /api/devices/:id/command
app.post('/api/devices/:id/command', async (req, res) => {
  const { command_name, paras } = req.body;
  const token = await getIamToken();
  const resp = await axios.post(
    `https://iotda.cn-xxx.myhuaweicloud.com/v5/iot/${projectId}/devices/${req.params.id}/commands`,
    { service_id: 'dryer', command_name, paras },
    { headers: { 'X-Auth-Token': token } }
  );
  res.json(resp.data);
});

// GET /api/devices/:id/shadow
app.get('/api/devices/:id/shadow', async (req, res) => {
  const token = await getIamToken();
  const resp = await axios.get(
    `https://iotda.cn-xxx.myhuaweicloud.com/v5/iot/${projectId}/devices/${req.params.id}/shadow`,
    { headers: { 'X-Auth-Token': token } }
  );
  res.json(resp.data);
});
```

## 推荐路径小结
- 若想快速上线：优先选 **方案 A（REST/HTTPS 调用 IoTDA）**，前端通过自建后端调接口即可。  
- 需高实时推送：可在 REST 方案基础上增加定时轮询影子或使用 MQTT 订阅回调（方案 B）结合 WebSocket 推送。

## 设备侧是否要改动？
- 不需要。设备依旧通过 MQTT 连接 IoTDA，命令格式保持：`start/stop/toggle/set_mode` + `gear`。  
- 服务器下发命令与 IoTDA 控制台一致，直接复用现有固件逻辑。
