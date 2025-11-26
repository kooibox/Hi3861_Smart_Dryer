"""
Flask + 华为云 IoTDA 官方 SDK（北向）：命令下发 & 影子查询。
Hardcoded credentials for testing.
"""

import time
from typing import Any, Dict

from flask import Flask, jsonify, request, send_from_directory, redirect, session
from flask_cors import CORS
from huaweicloudsdkcore.auth.credentials import BasicCredentials, DerivedCredentials
from huaweicloudsdkcore.region.region import Region
from huaweicloudsdkiotda.v5 import (
    IoTDAClient,
    CreateCommandRequest,
    DeviceCommandRequest,
    ShowDeviceShadowRequest,
)

# Hardcoded configuration for testing
PROJECT_ID = "9e84cbf7c7c642059df96a94aa97661a"
DEVICE_ID = "69254fd5bf22cc5a8c09cdcf_demo"
IOTDA_ENDPOINT = "386ca38bcf.st1.iotda-app.cn-north-4.myhuaweicloud.com"
IOTDA_AK = "HPUA9PTFXFEA4DNUYWZP"
IOTDA_SK = "YFhpfKN1CWrF5MYHah0qIahWJhlfBjTy5hDARXlt"
SERVICE_ID = "dryer"
REGION_ID = "cn-north-4"
LOGIN_USERNAME = "admin"  # 简单用户名（测试用）
LOGIN_PASSWORD = "admin123"  # 简单登录密码（测试用）
SECRET_KEY = "smart-laundry-secret"

print("=" * 50)
print("           IoTDA Flask Web 服务配置")
print("=" * 50)
print(f"项目ID (PROJECT_ID):    {PROJECT_ID}")
print(f"设备ID (DEVICE_ID):    {DEVICE_ID}")
print(f"服务ID (SERVICE_ID):    {SERVICE_ID}")
print(f"IoTDA端点:            {IOTDA_ENDPOINT}")
print(f"区域ID:               {REGION_ID}")
print(f"访问密钥 (AK):        {IOTDA_AK[:8]}...{IOTDA_AK[-4:]}")
print(f"秘密密钥 (SK):        {IOTDA_SK[:8]}...{IOTDA_SK[-4:]}")
print("=" * 50)
print("注意：敏感信息已部分隐藏，仅用于调试")
print("=" * 50)


def build_client() -> IoTDAClient:
    creds = BasicCredentials(IOTDA_AK, IOTDA_SK, PROJECT_ID)
    # 标准版/企业版需要使用衍生算法
    creds.with_derived_predicate(DerivedCredentials.get_default_derived_predicate())

    # 创建Region对象
    region = Region(REGION_ID, IOTDA_ENDPOINT)

    return IoTDAClient.new_builder() \
        .with_credentials(creds) \
        .with_region(region) \
        .build()


client = build_client()

app = Flask(__name__, static_folder="static", static_url_path="")
app.secret_key = SECRET_KEY
CORS(app)


def iotda_get_shadow() -> Dict[str, Any]:
    req = ShowDeviceShadowRequest(device_id=DEVICE_ID)
    resp = client.show_device_shadow(req)
    return resp.to_dict()


def iotda_send_command(command_name: str, paras: Dict[str, Any]) -> Dict[str, Any]:
    body = DeviceCommandRequest(service_id=SERVICE_ID, command_name=command_name, paras=paras)
    req = CreateCommandRequest(device_id=DEVICE_ID, body=body)
    resp = client.create_command(req)
    return resp.to_dict()


@app.route("/api/state", methods=["GET"])
def api_state():
    if not session.get("authed"):
        return jsonify({"error": "unauthorized"}), 401
    try:
        shadow = iotda_get_shadow()
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500
    # 兼容前端旧格式：payload.services[0].properties
    service_list = shadow.get("shadow", []) if isinstance(shadow, dict) else []
    service = next((s for s in service_list if s.get("service_id") == SERVICE_ID), service_list[0] if service_list else {})
    props = {}
    if isinstance(service, dict):
        props = service.get("reported", {}).get("properties", {}) or {}
    payload = {"services": [{"service_id": SERVICE_ID, "properties": props}]}
    return jsonify({"shadow": shadow, "payload": payload, "ts": time.time()})


@app.route("/api/command", methods=["POST"])
def api_command():
    if not session.get("authed"):
        return jsonify({"error": "unauthorized"}), 401
    data = request.get_json(force=True, silent=True) or {}
    command_name = data.get("command_name")
    paras = data.get("paras", {}) if isinstance(data.get("paras", {}), dict) else {}
    if command_name not in ("start", "stop", "toggle", "set_mode", "switch_mode"):
        return jsonify({"error": "invalid command_name"}), 400
    try:
        resp = iotda_send_command(command_name, paras)
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500
    return jsonify({"ok": True, "resp": resp})


@app.route("/", methods=["GET"])
def index():
    if not session.get("authed"):
        return redirect("/login.html")
    return send_from_directory(app.static_folder, "index.html")


@app.route("/login", methods=["POST"])
def login():
    data = request.get_json(force=True, silent=True) or {}
    username = data.get("username", "")
    pwd = data.get("password", "")
    if username == LOGIN_USERNAME and pwd == LOGIN_PASSWORD:
        session["authed"] = True
        return jsonify({"ok": True})
    return jsonify({"error": "invalid username or password"}), 401


def start():
    port = 5000  # Default port for Flask
    host = "0.0.0.0"  # Default host
    print(f"[Flask] starting on {host}:{port}")
    app.run(host=host, port=port, debug=False)


if __name__ == "__main__":
    start()
