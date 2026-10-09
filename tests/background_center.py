#!/usr/bin/env python3
"""Measure the gold artwork annulus, excluding the illustration inside it."""
import json
import math
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
scene = json.loads((root / 'tools/layout.json').read_text())
dx, dy = scene.get('background_offset', [0, 0])
for name in dict.fromkeys(page['background'] for page in scene['pages']):
    image = Image.open(root / 'assets/backgrounds' / f'{name}.png').convert('RGB')
    points = []
    # Outermost gold edge per ray; omit compass jewels near the cardinal axes.
    for angle in range(360):
        if min(angle % 90, 90-angle % 90) < 12:
            continue
        theta = math.radians(angle)
        hits = []
        for radius in range(148, 180):
            x = round(180+radius*math.cos(theta))
            y = round(180+radius*math.sin(theta))
            if not (0 <= x < 360 and 0 <= y < 360):
                continue
            r, g, b = image.getpixel((x, y))
            if r > 180 and g > 120 and b < g*.9 and r > g*1.04:
                hits.append((x, y))
        if hits:
            points.append(hits[-1])
    rows = [(2*x, 2*y, 1, x*x+y*y) for x, y in points]
    assert len(rows) > 100, f'{name}: insufficient annulus samples'
    matrix = [[sum(row[i]*row[j] for row in rows) for j in range(3)] +
              [sum(row[i]*row[3] for row in rows)] for i in range(3)]
    for k in range(3):
        pivot = matrix[k][k]
        assert abs(pivot) > 1e-8
        matrix[k] = [v/pivot for v in matrix[k]]
        for i in range(3):
            if i != k:
                factor = matrix[i][k]
                matrix[i] = [a-factor*b for a, b in zip(matrix[i], matrix[k])]
    cx, cy = matrix[0][3]+dx, matrix[1][3]+dy
    print(f'{name}: artwork center ({cx:.2f}, {cy:.2f})')
    assert math.hypot(cx-180, cy-180) < 1, f'{name}: artwork is off center'
