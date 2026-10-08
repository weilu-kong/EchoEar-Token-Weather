#!/usr/bin/env python3
"""Build the offline UI from shared artwork/layout; no firmware or network services."""
import base64
import hashlib
import json
from pathlib import Path

import qrcode
from fontTools import subset
from fontTools.ttLib import TTFont
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'
ASSETS = ROOT / 'assets'
layout = json.loads((TOOLS / 'layout.json').read_text())
assert layout['width'] == layout['height'] == 360
assets = {}
manifest = {'stage': 'visual-review', 'size': [360, 360], 'backgrounds': {}, 'logos': {}, 'sdk': layout['sdk']}


def data_url(path, mime):
    return f'data:{mime};base64,' + base64.b64encode(path.read_bytes()).decode()


for name in dict.fromkeys(pg['background'] for pg in layout['pages']):
    image = Image.open(ASSETS / 'source' / f'{name}.png').convert('RGB').resize((360, 360), Image.Resampling.LANCZOS)
    # Quantize the browser background to the same RGB565 samples as the later LCD.
    pixels = image.tobytes()
    raw, rgb = bytearray(), bytearray()
    for i in range(0, len(pixels), 3):
        r, g, b = pixels[i:i + 3]
        r5, g6, b5 = r >> 3, g >> 2, b >> 3
        value = (r5 << 11) | (g6 << 5) | b5
        raw.extend((value & 255, value >> 8))
        rgb.extend((round(r5 * 255 / 31), round(g6 * 255 / 63), round(b5 * 255 / 31)))
    destination = ASSETS / 'backgrounds' / f'{name}.png'
    Image.frombytes('RGB', (360, 360), bytes(rgb)).save(destination)
    (ASSETS / 'backgrounds' / f'{name}.rgb565').write_bytes(raw)
    assert len(raw) == 360 * 360 * 2
    assets[f'bg:{name}'] = data_url(destination, 'image/png')
    manifest['backgrounds'][name] = {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(), 'format': 'RGB565 little-endian'}

for provider in layout['providers']:
    path = ASSETS / 'logos' / f'{provider["id"]}.svg'
    svg = path.read_text()
    assert '<script' not in svg.lower() and 'href="http' not in svg.lower(), path
    assets[f'logo:{provider["id"]}'] = data_url(path, 'image/svg+xml')
    manifest['logos'][provider['id']] = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}

script = (TOOLS / 'preview.js').read_text()
template = (TOOLS / 'preview.html').read_text()
text = json.dumps(layout, ensure_ascii=False) + script + template
font = TTFont(ASSETS / 'fonts' / 'NotoSansCJKjp.ttf')
lcd_text = ''.join(n.get('text', '') for p in layout['pages'] for n in p['nodes'])
lcd_text += '年月日月火水木金土天気取得中晴一部曇霧雨雪雷不明最終更新設定確認失敗前回上限未提供ネットワーク接続済保存切断再忘削除配網開始キャンセル完了専用試行ごください残り'
options = subset.Options()
options.layout_features = ['*']
subsetter = subset.Subsetter(options=options)
subsetter.populate(text=text + lcd_text, unicodes=list(range(0x20,0x250))+list(range(0x2000,0x2070))+list(range(0x3000,0x3100))+list(range(0x4E00,0xA000))+list(range(0xFF00,0xFFF0)))
subsetter.subset(font)
missing = sorted({c for c in lcd_text if ord(c) not in font.getBestCmap()})
assert not missing, f'Missing generated LCD glyphs: {missing}'
font.save(ASSETS / 'fonts' / 'Shanhai.ttf')
font.flavor = 'woff2'
font.save(ASSETS / 'fonts' / 'Shanhai.woff2')
manifest['font'] = {'family': 'Noto Sans JP', 'woff2_bytes': (ASSETS / 'fonts' / 'Shanhai.woff2').stat().st_size, 'lcd_missing_glyphs': missing}

qr_payload = {'ver': 'v1', 'name': 'PROV_DEMO', 'pop': 'preview-only', 'transport': 'ble', 'network': 'wifi'}
qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=4, border=4)
qr.add_data(json.dumps(qr_payload, separators=(',', ':')))
qr.make(fit=True)
qr_path = ASSETS / 'pairing_preview.png'
qr.make_image(fill_color='black', back_color='white').save(qr_path)
assets['qr'] = data_url(qr_path, 'image/png')
(ASSETS / 'pairing_preview.json').write_text(json.dumps(qr_payload, indent=2) + '\n')
manifest['qr'] = {'preview_only': True, 'version': qr.version, 'quiet_zone_modules': 4}

html = template.replace('__FONT__', base64.b64encode((ASSETS / 'fonts' / 'Shanhai.woff2').read_bytes()).decode())
html = html.replace('__SCENE__', json.dumps(layout, ensure_ascii=False)).replace('__ASSETS__', json.dumps(assets)).replace('__SCRIPT__', script)
assert all(token not in html for token in ['__FONT__', '__SCENE__', '__ASSETS__', '__SCRIPT__'])
(ROOT / 'SHANHAI_離線プレビュー.html').write_text(html)
(ASSETS / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
print(f'Built offline preview: {len(html.encode()):,} bytes; {len(manifest['backgrounds'])} RGB565 backgrounds: {len(manifest['backgrounds']) * 360 * 360 * 2:,} bytes; missing LCD glyphs: 0')
