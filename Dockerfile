# ============================================
# Production image — fully offline, all deps pre-downloaded
# Build: docker build -t kama-httpserver:latest .
# ============================================
FROM nvidia/cuda:12.6.0-runtime-ubuntu22.04

# Copy all pre-downloaded runtime libs (includes libhiredis.so)
COPY third_party/runtime_libs/         /usr/lib/x86_64-linux-gnu/
COPY third_party/onnxruntime/lib/      /usr/local/lib/
COPY third_party/tensorrt/lib/         /usr/lib/x86_64-linux-gnu/

RUN ldconfig

# Copy pre-built binary
COPY build/simple_server /app/simple_server

# Copy models and config (fix paths from dev to production)
COPY WebApps/InferenceServer/models/ /app/models/
COPY WebApps/InferenceServer/config.json /app/config.json
RUN sed -i 's|/project/WebApps/InferenceServer/models/|models/|g' /app/config.json

# Copy HTML resource files (handlers use relative path ../WebApps/InferenceServer/resource/ from CWD /app)
COPY WebApps/InferenceServer/resource/ /WebApps/InferenceServer/resource/

WORKDIR /app
EXPOSE 80

CMD ["./simple_server", "-c", "config.json"]
