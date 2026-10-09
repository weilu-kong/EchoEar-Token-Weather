#!/bin/sh
# Offline fixtures only: no account stores, USB, login, HTTP or firmware writes.
set -eu
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Set IDF_PATH to the fixed ESP-IDF v6.1 checkout}"
if [ "$(git -C "$IDF_PATH" rev-parse HEAD)" != 9a97f6c54ec638111ce55cd36581b3c192f15207 ]; then
    echo "Wrong ESP-IDF commit; use the project pin." >&2
    exit 1
fi
cjson=managed_components/espressif__cjson/cJSON
scratch=$(mktemp -d "${TMPDIR:-/tmp}/shanhai-host.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
build_test() {
    name=$1
    shift
    "${CC:-cc}" -Wno-deprecated-declarations -fsanitize=address,undefined -I tests/host -I main -I "$cjson" \
        -I "$IDF_PATH/components/esp_common/include" \
        -I "$IDF_PATH/components/nvs_flash/include" \
        -I "$IDF_PATH/components/esp_http_client/lib/include" \
        "tests/${name}_test.c" "$@" -lm -o "$scratch/$name"
    "$scratch/$name"
    printf 'PASS %s\n' "$name"
}
build_test model main/sh_model.c
build_test weather main/sh_weather_parse.c main/sh_model.c "$cjson/cJSON.c"
build_test calendar main/sh_calendar.c "$cjson/cJSON.c"
build_test ai_account main/sh_ai.c "$cjson/cJSON.c"
build_test cloud "$cjson/cJSON.c"
build_test http_header "$IDF_PATH/components/esp_http_client/lib/http_header.c" \
    "$IDF_PATH/components/esp_http_client/lib/http_utils.c"
PYTHONDONTWRITEBYTECODE=1 "${PYTHON_BIN:-python3}" - <<'PY'
import runpy
import hashlib
import json
from pathlib import Path
for name in ('codex', 'cursor'):
    runpy.run_path(f'tools/{name}_authorize.py')['fixture_checks']()
    print(f'PASS {name} synthetic authorization fixtures')
html = Path('SHANHAI_離線プレビュー.html').read_text()
scene, _ = json.JSONDecoder().raw_decode(html.split('const SCENE = ', 1)[1])
assert scene == json.loads(Path('tools/layout.json').read_text()), 'Preview layout is stale'
assert Path('tools/preview.js').read_text() in html, 'Preview script is stale'
for artifact in (Path('firmware/cursor-models-layout'), Path('firmware/seven-page-delivery'), Path('firmware/home-spacing'), Path('firmware/home-outline'), Path('firmware/centered-background'), Path('firmware/readable-text-claude'), Path('firmware/overview-window-home'), Path('firmware/cursor-remaining')):
    manifest = json.loads((artifact / 'manifest.json').read_text())
    app = (artifact / 'shanhai_echoear_demo.bin').read_bytes()
    assert len(app) == manifest['app_bytes'] < manifest['app_partition_bytes']
    assert hashlib.sha256(app).hexdigest() == manifest['app_sha256'], 'Firmware artifact hash mismatch'
print('PASS offline preview sources and firmware artifact hash/partition size')
PY
"${NODE_BIN:-node}" tests/preview.cjs
"${NODE_BIN:-node}" tests/ai_detail.cjs
"${NODE_BIN:-node}" tests/calendar_paging.cjs
printf 'PASS host checks; hardware and live service acceptance remain separate.\n'
