#!/usr/bin/env python3
"""Arrange actual browser-exported 360px screens for visual review."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'preview'
FONT = ROOT / 'assets/fonts/NotoSansJP.ttf'


def font(size, weight=500):
    f = ImageFont.truetype(FONT, size)
    f.set_variation_by_axes([weight])
    return f


def sheet(name, title, screens):
    height=142 + ((len(screens)+2)//3)*407
    image = Image.new('RGB', (1152, height), '#041319')
    draw = ImageDraw.Draw(image)
    draw.text((27, 15), title, font=font(30, 700), fill='#e8cb87')
    draw.text((29, 65), '360 × 360 · 日文 UI · オフライン表示サンプル · 実機と共通の素材', font=font(13), fill='#91aaa5')
    draw.line((28, 92, 1124, 92), fill='#82663e', width=1)
    for i, (filename, label) in enumerate(screens):
        x, y = 18 + (i % 3) * 378, 112 + (i // 3) * 407
        screen = Image.open(OUT / filename)
        assert screen.size == (360, 360), (filename, screen.size)
        draw.text((x + 9, y), label, font=font(16, 600), fill='#e7d09a')
        image.paste(screen.convert('RGB'), (x, y + 30))
    draw.text((29, height-25), 'EchoEar / SHANHAI · UI REVIEW 01 · 2026-10-08', font=font(10), fill='#91aaa5')
    image.save(OUT / name)


sheet('overview.png', '山海司天 · EchoEar', [
    ('home.png', '01 ホーム'), ('weather.png', '02 天気詳細'), ('quota.png', '03 AI利用状況'),
    ('detail.png', '04 AI詳細 · Claude Code'), ('standby.png', '05 待機画面 · 夜の表示例'), ('calendar.png', '06 カレンダー · サンプル'),
    ('event.png', '07 予定詳細 · サンプル'), ('06_network.png', 'Wi-Fi 設定'),
])
sheet('network_states.png', '接続設定とデータの状態', [
    ('06_network.png', '接続済み'), ('07_forget_confirm.png', 'ネットワークを忘れる'), ('08_pairing.png', 'BLE設定 · QRプレビュー'),
    ('09_pairing_error.png', '接続エラー'), ('10_weather_stale.png', '天気更新の失敗'), ('11_weather_empty.png', '天気データなし'),
])
print('Exported overview.png and network_states.png from actual browser captures.')
