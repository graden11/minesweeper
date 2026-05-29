# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
# One-shot: cmake + make + docker compose up (gpu or cpu)
./build.sh cpu      # cloud deployment
./build.sh gpu      # local with NVIDIA GPU

# Manual build (inside dev container or bare metal)
mkdir -p build && cd build
cmake .. -DENABLE_TENSORRT=OFF   # CPU-only
cmake .. -DENABLE_TENSORRT=ON    # with TensorRT GPU inference
make -j$(nproc)

# Run directly (not in Docker)
cd build && ./simple_server -c ../WebApps/InferenceServer/config.json -p 80 -t 4

# Docker management
docker compose up -d                          # GPU
docker compose -f docker-compose.cpu.yml up -d # CPU
docker compose logs -f httpserver
docker compose down && docker compose up -d --build
```

The project builds to a **single binary** `simple_server` that contains both the HTTP framework and the InferenceServer application. There are no separate libraries or plugins.

## Architecture

### Two-layer design

1. **HttpServer/** — Reusable C++ HTTP framework built on muduo. Provides Router, MiddlewareChain, SessionManager, CorsMiddleware, MetricsMiddleware, SSL support, DB connection pool, and config loading. Has no dependency on InferenceServer.

2. **WebApps/InferenceServer/** — Application layer. Registers 11 route handlers, manages online users, session tracking, and initializes the ModelFactory for inference.

### Request flow

```
TCP → muduo TcpServer → HttpContext (parse) → MiddlewareChain::before()
→ Router::route() → Handler::handle() → MiddlewareChain::after() → TCP response
```

Middleware runs in order: MetricsMiddleware (records latency), then CorsMiddleware (handles OPTIONS preflight).

### Handler registration

All 11 handlers are registered in `InferenceServer::initializeRouter()` (`WebApps/InferenceServer/src/InferenceServer.cpp`). Handlers use the **friend class** pattern — each handler class is declared `friend` in `InferenceServer.h` so it can access private state (`onlineUsers_`, `loginSessions_`, `mysqlUtil_`, `modelFactory_`).

### Routing

Two registration styles coexist:
- `httpServer_.Get("/path", handlerPtr)` — exact match
- `httpServer_.addRoute(HttpRequest::kGet, "/path/(\\d+)", callback)` — regex match with capture groups

### Session & auth

- Sessions stored in Redis (`RedisSessionStorage`) with JSON serialization and TTL-based expiry
- Falls back to in-memory (`MemorySessionStorage`) when no Redis config is provided
- Login sets `Set-Cookie: SESSIONID=<uuid>`, subsequent requests carry it
- Handlers check `session->getValue("isLoggedIn") == "true"`
- `loginSessions_` map (userId → sessionId) enables forced logout from the backend panel
- Session data: `userId`, `username`, `isLoggedIn` (3 keys stored as JSON in Redis)

### Inference engines

`ModelFactory` (`WebApps/InferenceServer/src/ModelFactory.cpp`) reads `config.json` → `models.engines` and creates engines by name:
- `"onnx"` type → `ResNet50Engine` (CPU, ONNX Runtime)
- `"tensorrt"` type → `ResNet50TRTEngine` (GPU, TensorRT)

GPU inference is serialized with `gpu_mutex_` — one request at a time.

### Config flow

`main.cpp` parses CLI args in **two passes**: first to find `-c <configPath>`, then to override port/threads/log_level from CLI. So `-p 8080` always wins over what's in config.json.

### HTML resources

Handlers read HTML files at runtime via relative path `../WebApps/InferenceServer/resource/<name>.html` from CWD. In Docker, the resource files are copied to `/WebApps/InferenceServer/resource/` and CWD is `/app`, so the relative path resolves correctly. This path dependency means the binary must be run from the right directory.

## Key constraints

- **Linux only**: muduo uses epoll, no Windows/macOS support
- **Models not in git**: `WebApps/InferenceServer/models/` must be populated before first run (~174 MB)
- **Passwords are bcrypt-hashed** using Linux crypt_r() with $2b$ format
- **Single binary**: all routes compiled in, no hot-reload or plugin system
- **When `ENABLE_TENSORRT=OFF`**, `ResNet50TRTEngine.cpp` is excluded from the build via `list(FILTER ... EXCLUDE REGEX)`
- **Dockerfile needs pre-built binary**: the production Dockerfile copies `build/simple_server`, so you must run `cmake` + `make` before `docker compose build`
