import urllib.request
import json

# Step 1: Login
login_data = json.dumps({"username": "testuser", "password": "test123"}).encode()
req = urllib.request.Request("http://localhost:8082/login", data=login_data,
    headers={"Content-Type": "application/json"})
resp = urllib.request.urlopen(req, timeout=10)
print(f"Login: {resp.read().decode()}")

# Extract session cookie
cookie = resp.getheader("Set-Cookie").split(";")[0]
print(f"Cookie: {cookie}")

# Step 2: Load YOLO model
yolo_data = json.dumps({
    "name": "yolov8l",
    "version": "1",
    "type": "onnx",
    "path": "models/yolov8l.onnx",
    "task": "detection",
    "labels": "models/coco_80.txt",
    "input_name": "images",
    "output_name": "output0",
    "input_width": 640,
    "input_height": 640,
    "input_mean": [0.0, 0.0, 0.0],
    "input_std": [1.0, 1.0, 1.0],
    "confidence_threshold": 0.3
}).encode()

req2 = urllib.request.Request("http://localhost:8082/models/load", data=yolo_data,
    headers={"Content-Type": "application/json", "Cookie": cookie})
try:
    resp2 = urllib.request.urlopen(req2, timeout=120)
    print(f"Load: {resp2.read().decode()}")
except Exception as e:
    print(f"Load error: {e}")

# Step 3: List models
req3 = urllib.request.Request("http://localhost:8082/models")
resp3 = urllib.request.urlopen(req3, timeout=10)
print(f"Models: {resp3.read().decode()}")
