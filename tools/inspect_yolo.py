import struct, re

with open("/tmp/yolov8l.onnx", "rb") as f:
    data = f.read()

# Scan backwards from end for output tensor metadata
# Ultralytics YOLO exports typically have output named "output0"
# with shape [1, 84, 8400] for 80-class COCO

for keyword in ["output0", "output1", "images", "8400"]:
    positions = [m.start() for m in re.finditer(keyword.encode(), data)]
    print(f"{keyword}: found at {positions[:5]}")
    for pos in positions[:3]:
        ctx_start = max(0, pos - 4)
        ctx_end = min(len(data), pos + 80)
        ctx = data[ctx_start:ctx_end]
        # Show the raw bytes around the match
        printable = "".join(chr(b) if 32 <= b < 127 else "." for b in ctx)
        print(f"  pos {pos}: {printable}")
