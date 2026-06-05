import urllib.request, json, base64

# Login
login = json.dumps({"username":"testuser","password":"test123"}).encode()
req = urllib.request.Request("http://localhost:8082/login", data=login,
    headers={"Content-Type":"application/json"})
r = urllib.request.urlopen(req, timeout=10)
cookie = r.getheader("Set-Cookie").split(";")[0]
print(f"Login: {r.read().decode()} cookie={cookie}")

# Unload old yolo (may not exist — ignore 404)
try:
    req = urllib.request.Request("http://localhost:8082/models/yolov8l/1",
        headers={"Cookie": cookie}, method="DELETE")
    r = urllib.request.urlopen(req, timeout=10)
    print(f"Unload: {r.read().decode()}")
except urllib.error.HTTPError as e:
    print(f"Unload: {e.code} {e.reason} (ok)")

# Reload YOLO with correct input_name
data = json.dumps({
    "name":"yolov8l","version":"1","type":"onnx",
    "path":"models/yolov8l.onnx","task":"detection",
    "labels":"models/coco_80.txt",
    "input_name":"images","output_name":"output0",
    "input_width":640,"input_height":640,
    "input_mean":[0.0,0.0,0.0],"input_std":[1.0,1.0,1.0],
    "confidence_threshold":0.3
}).encode()
req2 = urllib.request.Request("http://localhost:8082/models/load", data=data,
    headers={"Content-Type":"application/json","Cookie":cookie})
r2 = urllib.request.urlopen(req2, timeout=120)
print(f"Load: {r2.read().decode()}")

# Test predict
with open("/tmp/cat.jpg","rb") as f:
    b64 = base64.b64encode(f.read()).decode()
body = json.dumps({"image_data":b64,"model_name":"yolov8l:1"}).encode()
req3 = urllib.request.Request("http://localhost:8082/predict", data=body,
    headers={"Content-Type":"application/json"})
r3 = urllib.request.urlopen(req3, timeout=120)
result = json.loads(r3.read())
print(f"Predict: {json.dumps(result, indent=2, ensure_ascii=False)[:3000]}")
