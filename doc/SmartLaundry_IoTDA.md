# 华为云 IoTDA 参数设计手册（智慧洗衣房烘干监测）

本手册描述 SmartLaundry 模块与 IoTDA 的属性、命令及 Topic 约定，便于在物模型/规则引擎中配置和联调。

## 设备信息与服务
- `service_id`: `dryer`
- 上报周期：默认 3 秒（可在代码中调整 `MQTT_SEND_INTERVAL_SEC`）。
- 主题前缀使用华为 IoTDA 标准：`$oc/devices/{deviceId}/sys/...`

## Topic 约定
| 方向 | Topic | 说明 |
| ---- | ----- | ---- |
| 上行 | `$oc/devices/{deviceId}/sys/properties/report` | 属性上报 |
| 下行 | `$oc/devices/{deviceId}/sys/commands/#` | 命令下发 |
| 回执 | `$oc/devices/{deviceId}/sys/commands/response/request_id={reqId}` | 命令结果回执 (`result_code`) |

## 属性定义（properties）
上报 JSON 采用 `services` 数组包装。
```json
{
  "services": [
    {
      "service_id": "dryer",
      "properties": {
        "status": "RUNNING",      // RUNNING / STOPPED
        "mode": "Fast",           // Fast / Standard / Soft
        "humidity": 38,           // %RH, uint8
        "temperature": 26,        // °C, uint8
        "countdown": 7            // s，-1 表示未进入倒计时
      }
    }
  ]
}
```

字段说明：
- `status`：运行状态。
- `mode`：当前档位，字符串；与命令侧一致。
- `humidity` / `temperature`：DHT11 采样值。
- `countdown`：当湿度 ≤ 阈值（默认 40%）时从 10 秒倒计时，归零后自动停机；否则为 -1。

## 命令定义（commands）
命令通过 `$oc/devices/{deviceId}/sys/commands/#` 下发，设备执行后在回执 Topic 返回：
```json
{"result_code":0}  // 0 成功，1 失败
```

支持的 `command_name` 与 `paras`：

1) 启动 / 停止 / 翻转
- `start`，`paras`: `{}` — 置为运行
- `stop`，`paras`: `{}` — 置为停止
- `toggle`，`paras`: `{}` — 运行/停止状态取反

2) 切换档位
- `set_mode` 或 `switch_mode`
- `paras` 采用数字 `gear`: `1`/`2`/`3`（1=fast, 2=standard, 3=soft）

示例 payload：
```json
{"command_name":"start","paras":{}}
{"command_name":"stop","paras":{}}
{"command_name":"toggle","paras":{}}
{"command_name":"set_mode","paras":{"gear":1}}
```

## 映射关系与约束
- 档位与占空比：`fast=85%`，`standard=65%`，`soft=45%`（可在 `g_mode_duty[]` 中调整）。数字档位映射：`gear 1→fast`，`gear 2→standard`，`gear 3→soft`。
- 倒计时：湿度 ≤ 阈值（`HUMIDITY_THRESHOLD`，默认 40%）后才开始计时，过程中湿度回升会重置为未开始状态。
- 失败回执：当命令名未知或参数不合法时，返回 `result_code:1`。

## 联调建议
1. 在 IoTDA 创建设备，物模型可按上表字段配置（service_id=`dryer`）。  
2. 先通过属性上报确认连云（观察 `properties/report` 主题），再测试命令下发与回执。  
3. 若无回执或未执行，检查设备侧 Wi-Fi/MQTT 配置（`smart_laundry.c` 顶部宏）及 request_id 填写。  
