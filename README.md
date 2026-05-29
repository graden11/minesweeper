# Kama-HTTPServer

基于 muduo 的高性能 C++17 HTTP 服务框架，集成 ResNet-50 图像分类推理服务。

---

## 目录

- [1. 快速开始](#1-快速开始)
  - [1.4 云服务器部署（CPU-only）](#14-云服务器部署cpu-only)
- [2. 项目架构](#2-项目架构)
- [3. API 参考](#3-api-参考)
- [4. 配置参考](#4-配置参考)
- [5. 项目结构](#5-项目结构)
- [6. 开发环境搭建](#6-开发环境搭建)
- [7. 已知限制与注意事项](#7-已知限制与注意事项)
- [8. 性能基准](#8-性能基准)
- [9. 常用操作](#9-常用操作)
- [10. 许可证与致谢](#10-许可证与致谢)

---

## 1. 快速开始

### 1.1 前置条件

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| Docker | 20.10+ | 需支持 Compose v2 |
| NVIDIA Container Toolkit | — | GPU 推理必需；仅 CPU 推理可跳过 |
| 磁盘空间 | ≥ 2 GB | 镜像 + 模型文件 |
| 内存 | ≥ 4 GB | MySQL + 应用运行所需 |

### 1.2 三步启动

```bash
# Step 1 — 编译项目
git clone <repo-url> && cd httpserver
mkdir -p build && cd build
cmake .. && make -j$(nproc)
cd ..

# Step 2 — 启动服务
docker compose up -d

# Step 3 — 验证
curl http://localhost/            # 登录页面
curl http://localhost/metrics     # 监控指标
```

首次启动 MySQL 需要约 30 秒完成数据库初始化，可通过 `docker compose logs mysql` 观察进度。在 MySQL 变为 healthy 之前，httpserver 不会启动。

### 1.3 模型文件说明

模型文件位于 `WebApps/InferenceServer/models/`，**不在 Git 仓库中**（总计约 174 MB），需单独提供：

| 文件 | 大小 | 说明 |
|------|------|------|
| `resnet50_classification.onnx` | ~98 MB | ONNX 模型（CPU 推理，必选） |
| `resnet50_classification.engine` | ~50 MB | TensorRT FP16 引擎（GPU 推理，可选） |
| `resnet50_int8.engine` | ~26 MB | TensorRT INT8 引擎（GPU 推理，可选） |
| `imagenet_classes.txt` | ~10 KB | ImageNet 1000 类标签（必选） |

**无 GPU 环境**：如果运行环境没有 NVIDIA GPU，ONNX Runtime CPU 推理仍然可用。TensorRT 引擎加载失败时仅记录警告日志，不影响服务启动。

### 1.4 手动测试推理

```bash
# JSON 接口 — base64 图片
curl -X POST http://localhost/predict \
  -H "Content-Type: application/json" \
  -d '{"image_path":"/app/models/cat.jpg","model_name":"resnet50"}'

# Protobuf 接口
curl -X POST http://localhost/predict/proto \
  -H "Content-Type: application/x-protobuf" \
  --data-binary @request.bin
```

---

### 1.5 云服务器部署（CPU-only）

适用于阿里云/腾讯云轻量服务器（2核4G, Ubuntu 22.04），无 GPU 环境。

**前置准备：**
```bash
# 云服务器上安装 Docker
curl -fsSL https://get.docker.com | bash
apt install -y docker-compose-plugin

# 本地上传项目到服务器（排除 build/）
rsync -avz --exclude 'build/' ./ user@your-server-ip:~/httpserver/
```

**一键启动：**
```bash
cd ~/httpserver
./build.sh cpu
```

`build.sh` 自动完成：
1. `cmake -DENABLE_TENSORRT=OFF`（跳过 GPU 编译）
2. `make -j`（编译 CPU-only 二进制）
3. `docker compose -f docker-compose.cpu.yml up -d --build`（构建 CPU 镜像 + 启动）

**安全组配置：** 在云服务器控制台防火墙中放行 TCP 80 端口。

**验证：**
```bash
# 从本地浏览器访问
http://<服务器公网IP>/

# 测试推理（ONNX CPU）
curl -X POST http://<服务器公网IP>/predict \
  -H "Content-Type: application/json" \
  -d '{"image_path":"/app/models/cat.jpg","model_name":"resnet50"}'
```

> **注意：** 云端仅 ONNX CPU 推理可用（~76ms/请求），TensorRT 引擎需 GPU。CPU 版本的 `Dockerfile.cpu` 基于 `ubuntu:22.04`，不依赖 NVIDIA 镜像。

---

## 2. 项目架构

### 2.1 分层架构

```
+------------------------------------------------------------------+
|                       客户端 (Browser / curl)                     |
+---------------------------------+--------------------------------+
                                  |
                                  v
+------------------------------------------------------------------+
|                     HttpServer (muduo 事件驱动)                    |
|  +------------------+  +------------------+  +-----------------+  |
|  |  CorsMiddleware  |  | MetricsMiddleware|  | SessionManager  |  |
|  +------------------+  +------------------+  +-----------------+  |
|  |                         Router (11 条路由)                     |  |
|  +-------------------------------------------------------------+  |
+---------------------------------+--------------------------------+
                                  |
          +-----------------------+-----------------------+
          |                                               |
          v                                               v
+----------------------+                    +--------------------------+
|   InferenceServer       |                    |   HttpServer 框架层       |
|  (应用层 - 业务逻辑)  |                    |  (可复用基础设施)           |
|                      |                    |                          |
|  - ResNet50Engine    |                    |  - HttpRequest/Response   |
|  - ResNet50TRTEngine |                    |  - Router (精确+正则路由)   |
|  - ModelFactory      |                    |  - Session (Cookie 会话)   |
|  - 11 个 Handler     |                    |  - MiddlewareChain         |
|                      |                    |  - DbConnectionPool        |
+----------+-----------+                    |  - MetricsCollector        |
           |                                |  - CorsConfig              |
           |                                |  - ConfigLoader            |
           v                                +--------------------------+
+------------------+
|  外部依赖         |
|  - MySQL 8.0     |
|  - ONNX Runtime  |
|  - TensorRT       |
|  - protobuf      |
+------------------+
```

### 2.2 技术栈

| 层级 | 技术 | 用途 |
|------|------|------|
| 网络库 | [muduo](https://github.com/chenshuo/muduo) | 事件驱动、非阻塞 I/O、多线程 Reactor |
| HTTP 协议 | 自研 HttpRequest/HttpResponse/HttpContext | 请求解析、响应构造 |
| 数据库 | MySQL 8.0 + MySQL Connector/C++ 8.0 | 用户持久化、连接池 |
| 日志 | spdlog | 异步结构化日志 |
| 序列化 | nlohmann/json, Protocol Buffers | JSON 接口、二进制推理接口 |
| 推理 | ONNX Runtime 1.21 + TensorRT 10.16 | ResNet-50 图像分类 |
| 加密 | OpenSSL | HTTPS 支持（可选） |
| 构建 | CMake 3.10+, C++17 | 跨平台构建 |

### 2.3 请求处理流程

```
TCP 连接 → muduo 接受 → HttpContext 解析 HTTP 报文
  → MiddlewareChain::before()
      → MetricsMiddleware::before()   (记录开始时间)
      → CorsMiddleware::before()      (处理 OPTIONS 预检)
  → Router::route()                  (精确匹配 → 正则匹配 → 404)
  → Handler::handle()                (业务逻辑, DB/Session/AI/推理)
  → MiddlewareChain::after()
      → CorsMiddleware::after()      (添加 CORS 响应头)
      → MetricsMiddleware::after()   (记录延迟, 更新统计)
  → HttpResponse 序列化 → TCP 发送
```

---

## 3. API 参考

### 3.1 通用约定

- **Base URL**: `http://localhost:80`
- **认证**: 登录后需携带 `Cookie: SESSIONID=<session_id>`（登录接口自动 `Set-Cookie`）
- **Content-Type**: JSON 接口使用 `application/json`，Protobuf 接口使用 `application/x-protobuf`
- **统一错误格式**:
  ```json
  { "status": "error", "message": "描述信息" }
  ```

### 3.2 端点总览

| 方法 | 路径 | 认证 | 说明 | 响应类型 |
|------|------|------|------|----------|
| GET | `/` `/entry` | 否 | 登录/注册页面 | text/html |
| POST | `/login` | 否 | 用户登录 | application/json |
| POST | `/register` | 否 | 用户注册 | application/json |
| POST | `/user/logout` | 是 | 用户登出 | application/json |
| GET | `/menu` | 是 | 仪表盘 | text/html |
| GET | `/backend` | 否 | 管理后台 | text/html |
| GET | `/backend_data` | 否 | 在线统计 | application/json |
| POST | `/predict` | 否 | 图像推理 (JSON) | application/json |
| POST | `/predict/proto` | 否 | 图像推理 (Protobuf) | application/x-protobuf |
| GET | `/metrics` | 否 | 请求监控 | application/json |

### 3.3 端点详情

#### GET `/` `/entry`

返回登录/注册页面。

```bash
curl http://localhost/
```

---

#### POST `/login`

用户登录，成功时创建 Session 并设置 Cookie。

**请求：**
```json
{ "username": "alice", "password": "123456" }
```

**成功 (200)：**
```json
{ "success": true, "userId": 1 }
```
响应头包含 `Set-Cookie: SESSIONID=<uuid>`。

**失败 (401)：**
```json
{ "status": "error", "message": "Invalid username or password" }
```

**账号已在别处登录 (403)：**
```json
{ "success": false, "error": "账号已在其他地方登录" }
```

---

#### POST `/register`

注册新用户。

**请求：**
```json
{ "username": "alice", "password": "123456" }
```

**成功 (200)：**
```json
{ "status": "success", "message": "Register successful", "userId": 2 }
```

**用户名已存在 (409)：**
```json
{ "status": "error", "message": "username already exists" }
```

---

#### POST `/user/logout`

登出，销毁 Session。

**请求：**（需携带 Session Cookie）

**成功 (200)：**
```json
{ "message": "logout successful" }
```

---

#### GET `/menu`

返回 AI 推理平台仪表盘（需登录）。HTML 中会注入当前用户的 `userId`。

---

#### GET `/backend` `/backend_data`

`/backend` 返回管理后台 HTML 页面；`/backend_data` 返回 JSON 统计数据。

```bash
curl http://localhost/backend_data
```

**响应 (200)：**
```json
{ "curOnline": 3, "maxOnline": 12, "totalUser": 45 }
```

---

#### POST `/predict`

图像分类推理（JSON 接口）。

**请求：** 二选一
```json
{
  "image_data": "<base64 编码的图片>",
  "model_name": "resnet50"
}
```
或
```json
{
  "image_path": "/app/models/cat.jpg",
  "model_name": "resnet50_trt"
}
```

`model_name` 可选，默认为 `"resnet50"`。可用值：`resnet50`（ONNX）、`resnet50_trt`（TensorRT FP16）、`resnet50_int8`（TensorRT INT8）。

**成功 (200)：**
```json
{
  "status": "ok",
  "summary": "识别结果：tiger cat（27.0%），其他可能：tabby（12.5%）、Egyptian cat（11.8%）、lynx（0.3%）、plastic bag（0.2%）",
  "predictions": [
    { "id": 282, "label": "tiger cat", "confidence": 27.0 },
    { "id": 281, "label": "tabby", "confidence": 12.5 },
    { "id": 285, "label": "Egyptian cat", "confidence": 11.8 },
    { "id": 287, "label": "lynx", "confidence": 0.3 },
    { "id": 728, "label": "plastic bag", "confidence": 0.2 }
  ]
}
```

`confidence` 为百分比 (0–100)，已做 softmax 归一化。返回 Top-5 预测结果。

**缺少图片 (400)：**
```json
{ "status": "error", "message": "missing image_path or image_data" }
```

**未知模型 (400)：**
```json
{ "status": "error", "message": "unknown model: xxx" }
```

```bash
# 使用图片路径
curl -X POST http://localhost/predict \
  -H "Content-Type: application/json" \
  -d '{"image_path":"/app/models/cat.jpg","model_name":"resnet50"}'

# 使用 base64 图片
curl -X POST http://localhost/predict \
  -H "Content-Type: application/json" \
  -d "{\"image_data\":\"$(base64 -w0 cat.jpg)\",\"model_name\":\"resnet50_trt\"}"
```

---

#### POST `/predict/proto`

图像分类推理（Protobuf 二进制接口）。Schema 定义见 `proto/inference.proto`：

```protobuf
syntax = "proto3";
package inference;

message PredictRequest {
    bytes  image_data = 1;   // 原始图片字节（非 base64）
    string model_name = 2;   // 可选，默认 "resnet50"
}

message Prediction {
    int32  class_id   = 1;
    string label      = 2;
    float  confidence = 3;   // 0.0–1.0（注意：不是百分比）
}

message PredictResponse {
    bool                 success     = 1;
    string               message     = 2;
    repeated Prediction  predictions = 3;
}
```

**注意：** `/predict/proto` 的 `confidence` 是 0–1 的比值，而 JSON 接口 `/predict` 的 `confidence` 是 0–100 的百分比。两者不一致是因为 protobuf handler 内部做了 `/100.0` 转换。

---

#### GET `/metrics`

请求统计与延迟分布。

```bash
curl http://localhost/metrics | jq .
```

**响应 (200)：**
```json
{
  "uptime_seconds": 3600,
  "endpoints": {
    "/predict": {
      "total": 1523,
      "errors": 2,
      "avg_latency_us": 16500,
      "latency_us_min": 13200,
      "latency_us_max": 875000,
      "buckets": {
        "<10ms": 0,
        "<50ms": 1480,
        "<100ms": 35,
        "<500ms": 6,
        ">=500ms": 2
      }
    }
  }
}
```

`buckets` 为延迟分布直方图（微秒），按 `<10ms` / `<50ms` / `<100ms` / `<500ms` / `>=500ms` 分桶。

### 3.4 认证流程

```
1. POST /login (username + password)
   → 服务端查询 MySQL users 表
   → 创建 Session，存入 isLoggedIn/username/userId
   → Set-Cookie: SESSIONID=<random_uuid>
   → 返回 { "success": true, "userId": N }

2. 后续请求浏览器自动携带 Cookie: SESSIONID=<uuid>
   → SessionManager 从 MemorySessionStorage 查找 Session
   → Handler 检查 session->getValue("isLoggedIn") == "true"
   → 未登录返回 401

3. POST /user/logout
   → Session 销毁，onlineUsers 移除
```

Session 存储在内存中（`MemorySessionStorage`），服务重启后全部丢失。

---

## 4. 配置参考

### 4.1 config.json

```json
{
  "server": {
    "port": 80,
    "name": "HttpServer",
    "threads": 4,
    "log_level": "WARN"
  },
  "logging": {
    "level": "INFO",
    "file": "server.log"
  },
  "mysql": {
    "host": "tcp://mysql:3306",
    "user": "root",
    "password": "root",
    "database": "inference_platform",
    "pool_size": 10
  },
  "models": {
    "labels_path": "models/imagenet_classes.txt",
    "engines": {
      "resnet50": {
        "type": "onnx",
        "path": "models/resnet50_classification.onnx"
      },
      "resnet50_trt": {
        "type": "tensorrt",
        "path": "models/resnet50_classification.engine"
      },
      "resnet50_int8": {
        "type": "tensorrt",
        "path": "models/resnet50_int8.engine"
      }
    }
  }
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `server.port` | int | 80 | 监听端口 |
| `server.threads` | int | 4 | muduo I/O 线程数 |
| `server.log_level` | string | WARN | muduo 日志级别：TRACE/DEBUG/INFO/WARN |
| `logging.level` | string | INFO | spdlog 日志级别 |
| `logging.file` | string | server.log | 日志输出文件 |
| `mysql.host` | string | tcp://mysql:3306 | MySQL 连接地址（Docker 中使用服务名） |
| `mysql.pool_size` | int | 10 | 数据库连接池大小 |
| `models.engines.<name>.type` | string | — | 引擎类型：`"onnx"` 或 `"tensorrt"` |
| `models.engines.<name>.path` | string | — | 模型文件路径 |

### 4.2 命令行参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `-c <path>` | 配置文件路径 | `-c /app/config.json` |
| `-p <port>` | 覆盖监听端口 | `-p 8080` |
| `-t <n>` | 覆盖 I/O 线程数 | `-t 8` |
| `-l <level>` | 覆盖 muduo 日志级别 | `-l DEBUG` |

```bash
./simple_server -c config.json -p 8080 -t 8 -l DEBUG
```

---

## 5. 项目结构

```
httpserver/
├── CMakeLists.txt                     # 顶层 CMake（引入 HttpServer + WebApps 子目录，单目标 simple_server）
├── Dockerfile                         # GPU 生产镜像（cuda:12.6-runtime-ubuntu22.04，预编译二进制）
├── Dockerfile.cpu                     # CPU 生产镜像（ubuntu:22.04，仅 ONNX Runtime）
├── Dockerfile.dev                     # 开发镜像（cuda:12.6-devel-ubuntu22.04 + cmake/gcc/protobuf 等全套工具链）
├── docker-compose.yml                 # GPU 部署编排（MySQL 8.0 + httpserver，healthcheck 依赖）
├── docker-compose.cpu.yml             # CPU 部署编排（MySQL 8.0 + httpserver-cpu）
├── build.sh                           # 一键编译脚本：cmake + make + docker compose up -d
├── .dockerignore                      # 排除 build/ .git/ .idea/ models/ 等无关文件
├── init.sql                           # MySQL 初始化：CREATE DATABASE inference_platform + CREATE TABLE users
├── README.md                          # 本文件
├── pressureresult.md                  # wrk 压力测试详细结果
│
├── HttpServer/                        # ===== HTTP 框架层（可脱离 InferenceServer 复用）=====
│   ├── include/
│   │   ├── http/
│   │   │   ├── HttpServer.h           # 服务器主类，封装 muduo TcpServer + 多线程 EventLoop
│   │   │   ├── HttpRequest.h          # HTTP 请求模型（method, url, headers, body, query params）
│   │   │   ├── HttpResponse.h         # HTTP 响应构建器（状态码, Content-Type, body, Set-Cookie）
│   │   │   └── HttpContext.h          # HTTP 协议解析状态机（逐字节解析请求行→头部→正文）
│   │   ├── router/
│   │   │   ├── Router.h               # 路由注册与分发（精确匹配 → 正则匹配 → 404）
│   │   │   └── RouterHandler.h        # Handler 抽象基类，定义 handle(req, resp) 接口
│   │   ├── session/
│   │   │   ├── Session.h              # 会话数据模型（K-V 存储 + sessionId + 过期时间）
│   │   │   ├── SessionManager.h       # 会话生命周期管理（create/destroy/get/cleanExpired）
│   │   │   └── SessionStorage.h       # 内存会话存储后端（HashMap<string, Session>）
│   │   ├── middleware/
│   │   │   ├── Middleware.h           # 中间件抽象接口（before/after 钩子）
│   │   │   ├── MiddlewareChain.h      # 中间件链（按注册顺序依次执行 before → handler → after）
│   │   │   ├── MetricsMiddleware.h    # 指标中间件（记录请求延迟, 分桶统计, 错误计数）
│   │   │   └── cors/
│   │   │       ├── CorsConfig.h       # CORS 配置结构（允许的 origin/method/header）
│   │   │       └── CorsMiddleware.h   # CORS 中间件（处理 OPTIONS 预检, 添加 Access-Control-* 头）
│   │   ├── ssl/
│   │   │   ├── SslConfig.h            # SSL 配置（证书路径, 私钥路径, 加密套件）
│   │   │   ├── SslContext.h           # OpenSSL SSL_CTX 封装（初始化, 加载证书）
│   │   │   ├── SslConnection.h        # SSL 连接包装（read/write/handshake 非阻塞适配）
│   │   │   └── SslTypes.h             # SSL 相关类型别名（BIO, SSL 指针管理）
│   │   └── utils/
│   │       ├── ConfigLoader.h         # JSON 配置加载器（从文件读取 → nlohmann::json）
│   │       ├── FileUtil.h             # 文件工具（读取文本文件, 获取文件大小）
│   │       ├── JsonUtil.h             # JSON 辅助（安全提取字段, 构造错误响应 JSON）
│   │       ├── LogUtil.h              # 日志封装（spdlog 初始化, 多 sink 输出到控制台+文件）
│   │       ├── MetricsCollector.h     # 指标收集器（线程安全计数 + 延迟直方图 + snapshot 导出）
│   │       ├── MysqlUtil.h            # MySQL 查询辅助（sql::Connection + sql::PreparedStatement 包装）
│   │       └── db/
│   │           ├── DbConnection.h     # 数据库连接封装（RAII 管理 sql::Connection 生命周期）
│   │           ├── DbConnectionPool.h # 数据库连接池（固定大小, 互斥锁, borrow/return 模式）
│   │           └── DbException.h      # 数据库异常类型（连接失败, 查询失败, 参数错误）
│   ├── src/                           # 框架层实现（与 include 结构一一对应）
│   │   ├── http/
│   │   │   ├── HttpContext.cpp
│   │   │   ├── HttpRequest.cpp
│   │   │   ├── HttpResponse.cpp
│   │   │   └── HttpServer.cpp
│   │   ├── router/
│   │   │   └── Router.cpp
│   │   ├── session/
│   │   │   ├── Session.cpp
│   │   │   ├── SessionManager.cpp
│   │   │   └── SessionStorage.cpp
│   │   ├── middleware/
│   │   │   ├── MiddlewareChain.cpp
│   │   │   ├── MetricsMiddleware.cpp
│   │   │   └── cors/
│   │   │       └── CorsMiddleware.cpp
│   │   ├── ssl/
│   │   │   ├── SslConfig.cpp
│   │   │   ├── SslConnection.cpp
│   │   │   └── SslContext.cpp
│   │   └── utils/db/
│   │       ├── DbConnection.cpp
│   │       └── DbConnectionPool.cpp
│   └── examples/
│       └── test_client.cc             # 手工测试用 TCP 客户端（发送原始 HTTP 请求并打印响应）
│
├── WebApps/InferenceServer/              # ===== 应用层（ResNet-50 推理服务）=====
│   ├── include/
│   │   ├── InferenceServer.h             # 应用启动器：初始化 DB 连接池, SessionManager, 注册全部路由
│   │   ├── InferenceEngine.h          # 推理引擎抽象接口（virtual predict(image) → PredictResult）
│   │   ├── ResNet50Engine.h           # ONNX Runtime 推理：CPU 上运行 ResNet-50 图像分类
│   │   ├── ResNet50TRTEngine.h        # TensorRT 推理：GPU 上运行 FP16/INT8 引擎
│   │   ├── ModelFactory.h             # 模型工厂：按名称字符串创建/查找引擎实例
│   │   └── handlers/
│   │       ├── EntryHandler.h         # GET / /entry — 返回登录/注册页面（entry.html）
│   │       ├── LoginHandler.h         # POST /login — 校验密码, 创建 Session, Set-Cookie
│   │       ├── RegisterHandler.h      # POST /register — 检查用户名冲突, 写入 users 表
│   │       ├── LogoutHandler.h        # POST /user/logout — 销毁 Session, 更新在线统计
│   │       ├── MenuHandler.h          # GET /menu — 验证登录, 返回仪表盘页面（menu.html）
│   │       ├── GameBackendHandler.h   # GET /backend /backend_data — 管理后台 HTML + 在线人数 JSON API
│   │       ├── PredictHandler.h       # POST /predict — JSON 图像推理（image_path 或 base64 输入）
│   │       ├── ProtoPredictHandler.h  # POST /predict/proto — Protobuf 二进制推理（原始图片字节）
│   │       └── MetricsHandler.h       # GET /metrics — 导出 MetricsCollector 的 JSON 快照
│   ├── src/                           # 应用层实现（与 include 一一对应）
│   │   ├── main.cpp                   # 入口：解析命令行参数, 创建 EventLoop, 启动 HttpServer
│   │   ├── InferenceServer.cpp
│   │   ├── ModelFactory.cpp
│   │   ├── ResNet50Engine.cpp
│   │   ├── ResNet50TRTEngine.cpp
│   │   └── handlers/
│   │       ├── EntryHandler.cpp
│   │       ├── LoginHandler.cpp
│   │       ├── RegisterHandler.cpp
│   │       ├── LogoutHandler.cpp
│   │       ├── MenuHandler.cpp
│   │       ├── GameBackendHandler.cpp
│   │       ├── PredictHandler.cpp
│   │       ├── ProtoPredictHandler.cpp
│   │       └── MetricsHandler.cpp
│   ├── resource/                      # 前端静态 HTML（Handler 通过 CWD 相对路径读取返回）
│   │   ├── entry.html                 # 登录/注册页面（username + password + 切换表单）
│   │   ├── menu.html                  # AI 推理平台仪表盘（图片分类 + API 参考 + 在线统计）
│   │   ├── Backend.html               # 管理后台（在线人数, 注册统计）
│   │   └── NotFound.html              # 通用 404 页面
│   ├── models/                        # 模型文件（~174MB，不在 Git 仓库中）
│   └── config.json                    # 运行时配置（server/mysql/models 三段，详见 §4.1）
│
├── proto/
│   └── inference.proto                # Protobuf schema：PredictRequest / PredictResponse / Prediction
│
├── scripts/
│   ├── stress_test.py                 # Python 压力测试：可配置并发数/请求数, 输出 P50/P99/QPS
│   ├── convert_onnx_to_trt.cpp        # ONNX → TensorRT 引擎转换器（FP16 模式）
│   └── generate_calibration.py        # INT8 校准数据生成（ImageNet 验证集采样 → 校准缓存）
│
├── third_party/                       # 预编译第三方库（头文件 + .so/.a）
│   ├── muduo/                         # muduo 网络库（Reactor + EventLoop + TcpServer）
│   ├── onnxruntime/                   # ONNX Runtime 1.21（CPU 推理）
│   ├── tensorrt/                      # TensorRT 10.16（GPU 推理运行时）
│   ├── runtime_libs/                  # CUDA/cuDNN 等运行时 .so（容器内 LD_LIBRARY_PATH）
│   └── stb/                           # stb_image / stb_image_resize（单头文件图像解码）
│
└── images/                            # 推理测试图片（ImageNet 样本）
    ├── cat.jpg                        # 虎斑猫 → 验证 Top-1 输出 "tiger cat"
    ├── dog.jpg                        # 金毛犬 → 验证 Top-1 输出 "golden retriever"
    ├── image1.jpg
    ├── image2.jpg
    └── image3.jpg
```

---

## 6. 开发环境搭建

### 6.1 使用开发容器（推荐）

```bash
# 构建开发镜像（首次约 10 分钟）
docker build -f Dockerfile.dev -t kama-httpserver:dev .

# 启动开发容器
docker run -it --gpus all \
    -v "$(pwd):/project" \
    -p 80:80 \
    kama-httpserver:dev

# 容器内编译
cd /project && mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 运行
./simple_server -c ../WebApps/InferenceServer/config.json
```

### 6.2 裸机编译（Ubuntu 22.04）

```bash
# 安装编译依赖
apt-get update && apt-get install -y \
    cmake g++ make git libssl-dev \
    libmysqlclient-dev libmysqlcppconn-dev \
    nlohmann-json3-dev protobuf-compiler libprotobuf-dev \
    libspdlog-dev

# 编译 muduo
git clone --depth 1 https://github.com/chenshuo/muduo.git /tmp/muduo
cd /tmp/muduo && cmake -B build -DCMAKE_BUILD_TYPE=release \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build && cmake --install build

# 安装 ONNX Runtime
wget https://github.com/microsoft/onnxruntime/releases/download/v1.21.0/onnxruntime-linux-x64-1.21.0.tgz
tar xzf onnxruntime-linux-x64-1.21.0.tgz -C /usr/local

# 安装 TensorRT（需 CUDA 12.6）
apt-get install -y libnvinfer10 libnvinfer-dev \
    libnvinfer-plugin10 libnvinfer-plugin-dev \
    libnvonnxparsers10 libnvonnxparsers-dev

# 编译项目
cd httpserver && mkdir build && cd build
cmake .. && make -j$(nproc)
```

---

## 7. 已知限制与注意事项

| 限制 | 说明 |
|------|------|
| **密码安全** | 密码使用 bcrypt ($2b$ 格式) 哈希存储，通过 Linux crypt_r() 实现。 |
| **模型文件不在仓库** | 模型文件约 174 MB，须单独放入 `WebApps/InferenceServer/models/`。 |
| **HTML 资源路径依赖** | Handler 通过 CWD 相对路径 `../WebApps/InferenceServer/resource/` 读取 HTML。运行前确保 CWD 使此路径可解析。 |
| **GPU 依赖** | TensorRT 引擎需 NVIDIA GPU + CUDA 12.6。无 GPU 时仅 ONNX CPU 推理可用。 |
| **内存会话** | Session 存储在 HashMap 中，服务重启后全部丢失。 |
| **GPU 推理串行** | `gpu_mutex_` 确保同一时刻只有一个推理任务在 GPU 上执行，吞吐量上限约 157 QPS。 |
| **单可执行文件** | 所有 Handler 在编译期注册，无插件/热加载机制。 |
| **无频率限制** | `/predict`、`/login` 等接口无限流，暴露在公网时需额外保护。 |

---

## 8. 性能基准

硬件：NVIDIA GeForce RTX 5060 (8 GB)，16 核 CPU，Ubuntu 22.04。

### 单请求延迟

| 模型 | 平均延迟 | P50 | P99 | 相对 ONNX CPU |
|------|----------|-----|-----|---------------|
| **resnet50_int8** (TensorRT) | 13.8 ms | 13.8 ms | 15.2 ms | 快 5.5 倍 |
| **resnet50_trt** (TensorRT) | 16.3 ms | 16.5 ms | 20.0 ms | 快 4.6 倍 |
| resnet50 (ONNX CPU) | 76.3 ms | 75.8 ms | 83.6 ms | 基准 |

### 吞吐量（wrk -t4 -c10 -d30s）

| 模型 | QPS | 平均延迟 |
|------|-----|----------|
| **resnet50_trt** | 152 | 60.5 ms |
| **resnet50_int8** | 142 | 63.6 ms |
| resnet50 (ONNX CPU) | 44 | 188.3 ms |

详细压测报告见 [pressureresult.md](pressureresult.md)。

---

## 9. 常用操作

```bash
# 查看日志
docker compose logs -f httpserver

# 查看 MySQL 数据
docker exec -it kama-mysql mysql -uroot -proot inference_platform -e "SELECT * FROM users;"

# 重建并重启
docker compose down && docker compose up -d --build

# 测试图像分类（文件路径）
curl -X POST http://localhost/predict \
  -H "Content-Type: application/json" \
  -d '{"image_path":"/app/models/cat.jpg","model_name":"resnet50_trt"}'

# 测试图像分类（base64）
curl -X POST http://localhost/predict \
  -H "Content-Type: application/json" \
  -d "{\"image_data\":\"$(base64 -w0 cat.jpg)\",\"model_name\":\"resnet50\"}"

# 查看实时指标
curl -s http://localhost/metrics | jq .

# 压力测试
python3 scripts/stress_test.py --url http://localhost/predict --model resnet50_trt

# 用户注册
curl -X POST http://localhost/register \
  -H "Content-Type: application/json" \
  -d '{"username":"test","password":"123456"}'

# 用户登录（保存 Cookie）
curl -c cookies.txt -X POST http://localhost/login \
  -H "Content-Type: application/json" \
  -d '{"username":"test","password":"123456"}'

# 带 Cookie 访问受保护页面
curl -b cookies.txt http://localhost/menu

# 查看实时指标
curl -s http://localhost/metrics | jq .
```

---

## 10. 许可证与致谢

本项目基于以下开源项目构建：

- [muduo](https://github.com/chenshuo/muduo) — 陈硕的高性能 C++ 网络库
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) — Microsoft 的跨平台推理引擎
- [TensorRT](https://developer.nvidia.com/tensorrt) — NVIDIA 的高性能深度学习推理 SDK
- [spdlog](https://github.com/gabime/spdlog) — 快速 C++ 日志库
- [nlohmann/json](https://github.com/nlohmann/json) — Modern C++ JSON 库
- [stb](https://github.com/nothings/stb) — 单头文件图像加载库
