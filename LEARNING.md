# C++ HTTP Server 项目深度学习方案

## 项目概览

这是一个基于 **muduo** 网络库的 C++17 HTTP 服务器框架，附带 AI 推理平台（用户认证 + ResNet-50 图像分类推理 + 监控）。

```
+-------------------------------------------+
|  应用层: InferenceServer                      |  9 个 Handler、用户认证、ML 推理、监控
+-------------------------------------------+
|  框架层: HttpServer                        |  路由、中间件、会话、SSL、DB 连接池
+-------------------------------------------+
|  网络层: muduo                             |  EventLoop、TcpServer、Buffer
+-------------------------------------------+
|  部署: Docker Compose (GPU/CPU/Dev)        |
+-------------------------------------------+
```

---

## 第一阶段：网络基础 — muduo 的 Reactor 模式（2-3天）

**目标**: 理解 muduo 的事件循环 + 非阻塞 I/O 模型

**核心文件**:
- `HttpServer/src/http/HttpServer.cpp` — `onConnection()`, `onMessage()`, `start()` 三个入口
- `HttpServer/include/http/HttpServer.h` — 看 muduo 成员的类型和用途

**学习要点**:
1. `muduo::net::EventLoop` — one loop per thread 模型
2. `muduo::net::TcpServer` — 连接接受、多线程 Reactor
3. 回调注册机制：`setConnectionCallback` / `setMessageCallback`
4. `conn->setContext()` / `conn->getMutableContext()` — 用 `boost::any` 存储连接级状态
5. `muduo::net::Buffer` — 零拷贝缓冲区设计

**实践**: 写一个最小的 muduo echo server，理解 `onConnection` / `onMessage` 的生命周期

---

## 第二阶段：HTTP 协议解析 — HttpContext 状态机（1-2天）

**目标**: 理解 HTTP/1.1 协议解析的增量状态机

**核心文件**:
- `HttpServer/src/http/HttpContext.cpp` — 完整的状态机实现
- `HttpServer/include/http/HttpContext.h` — 状态枚举和接口

**学习要点**:
1. 四个状态：`kExpectRequestLine → kExpectHeaders → kExpectBody → kGotAll`
2. 为什么需要状态机（TCP 是字节流，一个包可能只到半行）
3. `Buffer::findCRLF()` — 按行分割 HTTP 协议
4. Content-Length 处理（POST body 长度）
5. `HttpRequest` / `HttpResponse` 作为值对象的序列化方式

**实践**: 用 Wireshark 抓一个 HTTP 请求，对照代码看解析过程

---

## 第三阶段：路由与会话（2天）

**目标**: 理解 URL 分发和 Cookie-based Session

**核心文件**:
- `HttpServer/src/router/Router.cpp` — 四级路由分发
- `HttpServer/src/session/SessionManager.cpp` — Cookie 解析、Session 创建
- `HttpServer/src/session/Session.cpp` — 键值存储 + 过期机制

**学习要点**:
1. 精确匹配 vs 正则匹配（`/:userId` → `^/([^/]+)$`）
2. 路由优先级：精确 Handler → 精确 Callback → 正则 Handler → 正则 Callback → 404
3. Session ID 生成（32 位十六进制随机串）
4. Cookie 读写流程：`getSession()` 自动处理 Set-Cookie
5. `SessionStorage` 抽象 — 为何设计成可替换（内存 → Redis）

**实践**: 添加一个自定义路由，理解 Handler 的注册和调用

---

## 第四阶段：中间件链（1天）

**目标**: 理解洋葱模型和责任链

**核心文件**:
- `HttpServer/src/middleware/MiddlewareChain.cpp` — forward before / reverse after
- `HttpServer/src/middleware/cors/CorsMiddleware.cpp` — 异常短路技巧
- `HttpServer/src/middleware/MetricsMiddleware.cpp` — thread_local 传递上下文

**学习要点**:
1. 中间件顺序：`before()` 正序、`after()` 逆序（洋葱模型）
2. CORS 中间件用 `throw HttpResponse` 短路 OPTIONS 请求（非常规但有效）
3. Metrics 中间件用 `thread_local` 存储路径和起始时间，避免参数传递

**实践**: 写一个简单的计时中间件

---

## 第五阶段：数据库连接池（1天）

**目标**: 理解对象池模式 + 模板参数绑定

**核心文件**:
- `HttpServer/include/utils/db/DbConnectionPool.h` — 单例 + 条件变量
- `HttpServer/include/utils/db/DbConnection.h` — RAII 归还 + 预处理语句

**学习要点**:
1. Meyer's Singleton — `static` 局部变量线程安全初始化
2. `shared_ptr` 自定义 deleter 实现连接自动归还
3. 条件变量实现的阻塞 `getConnection()`
4. 变参模板递归绑定 SQL 参数（`std::to_string` + `string` 特化）
5. 后台健康检查线程

---

## 第六阶段：应用层 — InferenceServer 组装（3天）

**目标**: 理解框架如何被使用、所有 Handler 的协作关系

**核心文件**:
- `WebApps/InferenceServer/src/InferenceServer.cpp` — `initialize()` 组装全流程
- `WebApps/InferenceServer/include/InferenceServer.h` — 应用状态管理

### 6.1 认证流程（登录/注册/登出）

| Handler | 路由 | 方法 | 职责 |
|---------|------|------|------|
| `EntryHandler` | `/`, `/entry` | GET | 静态页面服务 |
| `RegisterHandler` | `/register` | POST | SQL 插入 + 防重复 |
| `LoginHandler` | `/login` | POST | 密码验证 + 单点登录（踢旧会话） |
| `LogoutHandler` | `/user/logout` | POST | 资源清理（session/在线状态） |
| `MenuHandler` | `/menu` | GET | 认证网关模式 |

**核心文件**: `src/handlers/{Entry,Register,Login,Logout,Menu}Handler.cpp`

### 6.2 推理 API

| Handler | 路由 | 方法 | 职责 |
|---------|------|------|------|
| `PredictHandler` | `/predict` | POST | JSON 图像推理（base64 或路径） |
| `ProtoPredictHandler` | `/predict/proto` | POST | Protobuf 二进制推理 |
| `MetricsHandler` | `/metrics` | GET | 导出推理延迟统计 |

**学习要点**:
- `friend class` 的用法 — Handler 直接访问 InferenceServer 私有成员
- `ModelFactory` 注册表模式按名称获取引擎
- mutex 粒度：`mutexForOnlineUsers_` / `mutexForLoginSessions_`

---

## 第七阶段：ML 推理引擎（2天）

**目标**: 理解 ONNX Runtime 和 TensorRT 的集成方式

**核心文件**:
- `WebApps/InferenceServer/include/InferenceEngine.h` — 策略接口
- `WebApps/InferenceServer/src/ResNet50Engine.cpp` — CPU 推理完整流水线
- `WebApps/InferenceServer/src/ResNet50TRTEngine.cpp` — GPU 双缓冲异步推理
- `WebApps/InferenceServer/src/ModelFactory.cpp` — 注册表模式

**学习要点**:
1. 图像预处理：stb_image 解码 → 224×224 resize → HWC→CHW → 标准化
2. ONNX Runtime API：`Ort::Session` → `CreateTensor` → `Run` → softmax
3. TensorRT 双缓冲：pinned memory + device memory + CUDA stream 异步
4. `thread_local` 缓冲区避免重复分配
5. Base64 解码（PredictHandler 内联实现）

**实践**: 用自己的照片测试识别效果

---

## 第八阶段：安全审计（1天）

**目标**: 理解安全漏洞和改进方向

**需要关注的点**:
1. 密码明文存储（`init.sql` → `password VARCHAR(50)`）
2. `RegisterHandler` SQL 拼接（应改用预处理语句）
3. `LOG_INFO` 可能泄露敏感信息（用户名等）
4. Session 存储在内存中（服务重启全部丢失）

---

## 第九阶段：部署与 Docker（1天）

**目标**: 理解多阶段构建和 CPU/GPU 分离

**核心文件**:
- `Dockerfile` — 生产 GPU 镜像（离线依赖）
- `Dockerfile.cpu` — CPU-only 多阶段构建
- `Dockerfile.dev` — 轻量开发镜像（继承生产镜像 + 编译器）
- `docker-compose.yml` / `docker-compose.dev.yml`
- `build.sh`

**学习要点**:
1. 生产镜像"全离线"策略（预下载 libs → COPY 而非 apt-get）
2. `Dockerfile.dev` 设计思路：FROM 生产镜像 + 只装编译工具
3. Volume mount 开发模式：源码挂载 `/project` → cmake + make → 运行
4. MySQL healthcheck + `depends_on condition: service_healthy`

---

## 核心设计模式速查

| 模式 | 出现位置 |
|------|---------|
| Reactor | muduo EventLoop |
| 状态机 | HttpContext 解析 |
| 责任链 | MiddlewareChain |
| 策略模式 | InferenceEngine / SessionStorage |
| 单例 | DbConnectionPool, MetricsCollector |
| 对象池 | DbConnectionPool |
| 适配器 | HttpServer 包装 muduo TcpServer |
| 装饰器 | SslConnection 透明加解密 |
| RAII | SSL 资源管理, DbConnection 归还 |
| 模板方法 | RouterHandler::handle() |

---

## 学习建议

- **每阶段学习后，在对应源文件中添加中文注释**，加深理解
- 遇到不懂的 muduo 头文件，去 `third_party/muduo/` 查看源码
- 启动 dev 容器后，可以修改代码 + `docker compose -f docker-compose.dev.yml restart` 快速验证
- 先跑通全流程（注册 → 登录 → 下棋 → 图片识别），再深入每个模块
