import urllib.request, json, base64

# Test with invalid image first (should return error, not crash)
body = json.dumps({"image_data": "dGVzdA==", "model_name": "yolov8l:1"}).encode()
req = urllib.request.Request("http://localhost:8082/predict", data=body,
    headers={"Content-Type": "application/json"})
try:
    r = urllib.request.urlopen(req, timeout=30)
    print("Small:", r.read().decode())
except Exception as e:
    print(f"Small Error: {e}")

# Now with real cat image
with open("/tmp/cat.jpg", "rb") as f:
    b64 = base64.b64encode(f.read()).decode()
body = json.dumps({"image_data": b64, "model_name": "yolov8l:1"}).encode()
req = urllib.request.Request("http://localhost:8082/predict", data=body,
    headers={"Content-Type": "application/json"})
try:
    r = urllib.request.urlopen(req, timeout=120)
    result = json.loads(r.read())
    print("Real:", json.dumps(result, indent=2, ensure_ascii=False)[:2000])
except Exception as e:
    print(f"Real Error: {e}")
