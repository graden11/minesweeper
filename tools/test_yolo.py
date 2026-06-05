import urllib.request, json, base64

model = "yolov8l:1"

with open("/tmp/cat.jpg", "rb") as f:
    b64 = base64.b64encode(f.read()).decode()

body = json.dumps({"image_data": b64, "model_name": model}).encode()
req = urllib.request.Request("http://localhost:8082/predict", data=body,
    headers={"Content-Type": "application/json"})
r = urllib.request.urlopen(req, timeout=120)
j = json.loads(r.read())

print("Status:", j["status"])
print("Task:", j.get("task_type", "?"))
print("Summary:", j.get("summary", ""))
for d in j.get("detections", [])[:8]:
    b = d["bbox"]
    conf = str(d["confidence"])[:5]
    print(f"{d['label']} {conf}% "
          f"box=({int(b['x1'])},{int(b['y1'])},{int(b['x2'])},{int(b['y2'])})")
