import json

data = {
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
    "input_mean": [0, 0, 0],
    "input_std": [1, 1, 1],
    "confidence_threshold": 0.3
}
print(json.dumps(data))
