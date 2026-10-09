#!/usr/bin/env python3
"""Check outline pixels in before/after native home captures, not browser images."""
import sys
from PIL import Image

before, after = (Image.open(path).convert('RGB') for path in sys.argv[1:])
assert before.size == after.size == (360, 360)
# RGB565 quantization of the native #001016 outline, decoded by device_probe.py.
for name, box in [('condition', (170, 230, 314, 250)),
                  ('highlow', (181, 253, 306, 273))]:
    x0, y0, x1, y1 = box
    counts = [sum(im.getpixel((x, y)) == (0, 16, 16)
                  for y in range(y0, y1) for x in range(x0, x1))
              for im in (before, after)]
    assert counts[1] > counts[0] + 20, f'{name}: native outline missing: {counts}'
    print(f'PASS {name}: native outline pixels {counts[0]} -> {counts[1]}')
