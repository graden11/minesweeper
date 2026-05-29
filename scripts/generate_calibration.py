#!/usr/bin/env python3
"""Generate ~100 calibration images by augmenting a source image."""

import os
import sys
import random
from PIL import Image, ImageEnhance, ImageFilter, ImageOps


def augment_image(img, out_dir, base_name, count_start):
    """Apply random augmentations and save results. Returns next count index."""
    variants = []

    # Flips
    variants.append(("flip_h", ImageOps.mirror(img)))
    variants.append(("flip_v", ImageOps.flip(img)))
    variants.append(("flip_both", ImageOps.mirror(ImageOps.flip(img))))

    # Rotations: every 30 degrees
    for angle in range(0, 360, 30):
        variants.append((f"rot{angle}", img.rotate(angle)))

    # Brightness jitter
    for factor in [0.5, 0.7, 0.85, 1.15, 1.3, 1.5]:
        enhanced = ImageEnhance.Brightness(img).enhance(factor)
        variants.append((f"bright{factor:.2f}", enhanced))

    # Contrast jitter
    for factor in [0.6, 0.8, 1.2, 1.4]:
        enhanced = ImageEnhance.Contrast(img).enhance(factor)
        variants.append((f"contrast{factor:.2f}", enhanced))

    # Color jitter
    for factor in [0.5, 0.7, 1.3, 1.5]:
        enhanced = ImageEnhance.Color(img).enhance(factor)
        variants.append((f"color{factor:.2f}", enhanced))

    # Slight blur / sharpen
    variants.append(("blur", img.filter(ImageFilter.GaussianBlur(radius=1))))
    variants.append(("sharpen", img.filter(ImageFilter.SHARPEN)))

    # Random crops + resize back
    w, h = img.size
    for crop_scale in [0.75, 0.85, 0.9, 0.95]:
        cw, ch = int(w * crop_scale), int(h * crop_scale)
        x = random.randint(0, w - cw)
        y = random.randint(0, h - ch)
        cropped = img.crop((x, y, x + cw, y + ch)).resize((w, h), Image.BILINEAR)
        variants.append((f"crop{crop_scale:.2f}", cropped))

    # Add slight noise
    for _ in range(5):
        noisy = img.copy()
        pixels = noisy.load()
        for _ in range(5000):
            x = random.randint(0, img.width - 1)
            y = random.randint(0, img.height - 1)
            r, g, b = pixels[x, y][:3]
            noise = random.randint(-20, 20)
            pixels[x, y] = (max(0, min(255, r + noise)),
                            max(0, min(255, g + noise)),
                            max(0, min(255, b + noise)))
        variants.append((f"noise_{_}", noisy))

    idx = count_start
    for label, variant in variants:
        variant.save(os.path.join(out_dir, f"calib_{idx:04d}_{label}.jpg"), quality=95)
        idx += 1

    return idx


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "WebApps/GomokuServer/models/cat.jpg"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "WebApps/GomokuServer/models/calibration"

    os.makedirs(out_dir, exist_ok=True)

    img = Image.open(src).convert("RGB")

    # Also include the original + resized variants
    idx = 0
    img.save(os.path.join(out_dir, f"calib_{idx:04d}_original.jpg"), quality=95)
    idx += 1

    idx = augment_image(img, out_dir, "base", idx)

    print(f"Generated {idx} calibration images in {out_dir}")


if __name__ == "__main__":
    main()
