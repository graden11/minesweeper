import urllib.request, json, base64, sys

model = sys.argv[1] if len(sys.argv) > 1 else "resnet50_classification:1"

with open("/tmp/cat.jpg", "rb") as f:
    b64 = base64.b64encode(f.read()).decode()

body = json.dumps({"image_data": b64, "model_name": model}).encode()
req = urllib.request.Request("http://localhost:80/predict", data=body,
    headers={"Content-Type": "application/json"})
resp = urllib.request.urlopen(req, timeout=60)
result = json.loads(resp.read())

print(f"=== {model} ===")
if result["status"] == "ok":
    print(f"Summary: {result['summary']}")
    for p in result["predictions"]:
        print(f"  {p['label']}: {p['confidence']:.1f}%")
else:
    print(f"Error: {result.get('message', 'unknown')}")
