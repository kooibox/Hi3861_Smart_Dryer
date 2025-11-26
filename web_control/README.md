# Web 控制系统 Docker 部署指南

基于 Flask + IoTDA SDK 的智能洗衣控制系统，提供完整的 Docker 容器化部署方案。

## 📋 系统要求

- Docker 20.10+
- Docker Compose 2.0+ (可选)
- 最低内存：512MB
- 推荐内存：1GB+

## 🔧 环境配置

### 必需的环境变量

创建 `.env` 文件（从 `.env.example` 复制）：

```bash
# 华为云配置
PROJECT_ID=your_project_id_here           # 华为云项目ID
DEVICE_ID=your_device_id_here             # IoTDA设备ID
IOTDA_ENDPOINT=https://iotda.cn-north-4.myhuaweicloud.com  # IoTDA接入地址
IOTDA_AK=your_access_key_here             # 华为云访问密钥AK
IOTDA_SK=your_secret_key_here             # 华为云访问密钥SK
SERVICE_ID=dryer                           # 服务ID

# Flask配置
FLASK_HOST=0.0.0.0                        # 监听地址
FLASK_PORT=5000                           # 服务端口
```

### 区域配置说明

| 华为云区域 | IoTDA Endpoint |
|-----------|---------------|
| 华北-北京四 | `https://iotda.cn-north-4.myhuaweicloud.com` |
| 华东-上海一 | `https://iotda.cn-east-3.myhuaweicloud.com` |
| 华南-广州 | `https://iotda.cn-south-1.myhuaweicloud.com` |
| 西南-贵阳一 | `https://iotda.cn-southwest-2.myhuaweicloud.com` |

## 🐳 Docker 部署方式

### 方式一：直接使用 Docker 命令

```bash
# 1. 进入项目目录
cd src/vendor/pzkj/pz_hi3861/demo/49_Exam/web_control

# 2. 复制并配置环境变量文件
cp .env.example .env
# 编辑 .env 文件，填入真实的配置信息

# 3. 构建镜像
docker build -t smart-laundry-web:latest .

# 4. 运行容器
docker run -d \
  --name smart-laundry-web \
  -p 5000:5000 \
  --env-file .env \
  --restart unless-stopped \
  smart-laundry-web:latest

# 查看运行状态
docker logs smart-laundry-web
```

### 方式二：使用 Docker Compose

```bash
# 1. 进入项目目录
cd src/vendor/pzkj/pz_hi3861/demo/49_Exam/web_control

# 2. 复制并配置环境变量文件
cp .env.example .env
# 编辑 .env 文件，填入真实的配置信息

# 3. 启动服务
docker-compose --env-file .env up -d --build

# 查看服务状态
docker-compose ps
docker-compose logs -f
```

### 方式三：生产环境推荐配置

```yaml
# docker-compose.prod.yml
version: '3.8'
services:
  smart-laundry-web:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: smart-laundry-web-prod
    restart: unless-stopped
    ports:
      - "80:5000"
    environment:
      - FLASK_ENV=production
    env_file:
      - .env
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:5000/api/state"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 40s
    networks:
      - app-network

networks:
  app-network:
    driver: bridge
```

运行生产环境：
```bash
docker-compose -f docker-compose.prod.yml --env-file .env up -d --build
```

## 📊 健康检查与监控

### 容器健康检查

容器已内置健康检查，自动检测服务状态：

```bash
# 查看健康状态
docker inspect --format='{{.State.Health.Status}}' smart-laundry-web

# 查看健康检查日志
docker inspect --format='{{.State.Health.Log}}' smart-laundry-web
```

### 服务监控

```bash
# 查看容器资源使用情况
docker stats smart-laundry-web

# 查看实时日志
docker logs -f smart-laundry-web

# 查看最近100行日志
docker logs --tail 100 smart-laundry-web
```

## 🔌 API 接口说明

### 状态查询
```bash
curl http://localhost:5000/api/state
```

### 控制命令
```bash
curl -X POST http://localhost:5000/api/command \
  -H "Content-Type: application/json" \
  -d '{
    "command_name": "start|stop|toggle|set_mode|switch_mode",
    "paras": {
      "gear": 1
    }
  }'
```

### 支持的命令
- `start`：启动设备
- `stop`：停止设备
- `toggle`：切换开关状态
- `set_mode`：设置模式
- `switch_mode`：切换模式

## 🛠️ 容器管理

### 基本操作

```bash
# 停止容器
docker stop smart-laundry-web

# 启动容器
docker start smart-laundry-web

# 重启容器
docker restart smart-laundry-web

# 删除容器
docker rm -f smart-laundry-web

# 删除镜像
docker rmi smart-laundry-web:latest
```

### 数据持久化

如需持久化日志数据，可以挂载卷：

```bash
docker run -d \
  --name smart-laundry-web \
  -p 5000:5000 \
  --env-file .env \
  -v /opt/smart-laundry/logs:/app/logs \
  smart-laundry-web:latest
```

## 🚨 故障排除

### 常见问题

1. **403/未订阅错误**
   - 确认 IoTDA 服务已开通
   - 检查 project_id 和 AK/SK 是否匹配
   - 确认设备已在 IoTDA 中注册

2. **401 签名失败**
   - 检查 AK/SK 是否正确
   - 确认无多余空格或换行符
   - 检查服务器时间同步
   - 确认 endpoint 地址正确

3. **容器启动失败**
   ```bash
   # 查看详细错误信息
   docker logs smart-laundry-web

   # 检查环境变量
   docker run --rm --env-file .env alpine env
   ```

4. **端口占用**
   ```bash
   # 查看端口占用
   netstat -tulpn | grep :5000

   # 修改映射端口
   docker run -d -p 8080:5000 smart-laundry-web:latest
   ```

5. **依赖包安装失败**
   - 检查网络连接
   - 确认 Docker 官方镜像源可访问
   - 如遇网络问题，可配置国内镜像源

### 日志分析

```bash
# 实时查看错误日志
docker logs -f smart-laundry-web 2>&1 | grep ERROR

# 导出日志文件
docker logs smart-laundry-web > app.log
```

### 性能优化

1. **内存优化**
   ```bash
   # 设置内存限制
   docker run -d --memory=512m smart-laundry-web:latest
   ```

2. **CPU 限制**
   ```bash
   # 设置CPU使用限制
   docker run -d --cpus=1.0 smart-laundry-web:latest
   ```

## 🔄 更新与维护

### 更新部署

```bash
# 拉取最新代码
git pull

# 重新构建镜像
docker build -t smart-laundry-web:latest .

# 重启容器
docker stop smart-laundry-web
docker rm smart-laundry-web
docker run -d --name smart-laundry-web ... smart-laundry-web:latest
```

### 备份配置

```bash
# 备份环境变量文件
cp .env .env.backup

# 备份重要配置
docker exec smart-laundry-web cat /app/config.json > config-backup.json
```

## 📚 相关文档

- [华为云IoTDA官方文档](https://support.huaweicloud.com/productdesc-iotda/iotda_01_0001.html)
- [Flask官方文档](https://flask.palletsprojects.com/)
- [Docker官方文档](https://docs.docker.com/)

---

如有问题，请检查日志或联系技术支持。
