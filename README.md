# 山海司天 · EchoEar Demo

设备已更新为七页开发固件，新增真实账号额度、Google Calendar 列表和事件详情；固定 v6.1 编译完成。Google、Codex、Cursor 与 Claude 独立账号已支持 USB 导入，设备实板 HTTPS 获取成功（HTTP 200 解析成功）；Cursor 总览与详情统一为剩余率（残り）；主页日期与天气增加位图黑色描边提升可读性；全局圆形背景校准居中（偏差 <1px）；正文字号整体放大 +2px 并配备独立 16px CJK 日历字体。当前最新烧录固件位于 `firmware/cursor-remaining/`；整体实板验收仍暂停。设备直接通过 HTTPS 获取数据并刷新 OAuth，不依赖常驻电脑服务；电脑仅用于一次性授权和 USB 导入。同级 R1 工程保留。

新对话请先读 [HANDOFF.md](HANDOFF.md)：当前进度、后续计划、开发路径及已测／未测边界。

## 交付内容

- `SHANHAI_離線プレビュー.html`：全部资源内嵌的交互预览，浏览器直接打开。预览中的天气、时间和网络为可调整样例；二维码 `PROV_DEMO` 仅用于演示。
- `preview/overview.png`、`preview/network_states.png`：主页面及网络、配网、错误、过期状态总览。
- `main/`、`assets/`、`tools/layout.json`：固件源码与冻结素材。文字、数值、有效额度环动态绘制；背景没有写死数值。
- `firmware/`：原生构建的应用、引导程序、分区表、ELF/map、实际 sdkconfig 和带 SHA-256 的清单。
- `docs/validation.md`：检查结果、实测范围与尚未测项。
- `backups/`：本机私有原厂 Flash/NVS、串口日志与实板测试记录，不纳入 Git。该目录可能包含网络配置，应保密。

## 设备操作

左右滑动切换页面；点击天气、AI 入口和服务图标查看详情。日历每屏显示两项，上下滑动查看更多。45 秒无操作进入待机，点击唤醒。打开设置或 BLE 配网时暂停待机。

主页网络图标打开 Wi-Fi 设置。首次没有凭据时显示真实 BLE 配网二维码，请用 Espressif 配网应用扫码；每台设备独立生成 PoP。配网结束停止 BLE，同次启动可以再次配网。

「切断」保留凭据并暂停自动重连，「再接続」恢复连接；重启会自动连接。忘记网络需在屏上确认，只替换 Wi-Fi 配置，其他 NVS 数据保留。配网取消或失败会恢复此前保存的网络。

天气连接后立即更新，此后每 15 分钟刷新。请求或解析失败时保留最近完整数据并显示过期；没有数据时显示 `--`。无上限的额度仅显示余额；没有上限百分比。

## 固定构建环境

- ESP-IDF `release/v6.1`，提交 `9a97f6c54ec638111ce55cd36581b3c192f15207`。
- 本机 SDK：`/Users/kongweilu/esp/esp-idf-v6.1_release`，Xtensa GCC 15.2.0。
- ESP32-S3，16 MB Flash / 16 MB octal PSRAM，ST77916 360×360，CST816S。
- `esp_vocat` BSP 1.1.0，LVGL 9.4.0，`network_provisioning` 1.3.1；全部解析版本保存在 `dependencies.lock`。

```sh
source /Users/kongweilu/esp/esp-idf-v6.1_release/export.sh
idf.py -B /private/tmp/shanhai_echoear_v61_build build
```

使用不含空格或 `&` 的构建目录：固定 SDK 的 OTA 初始数据生成命令无法可靠处理本项目路径中的 `&`。不修改 SDK 或托管组件。

分区表依据实机原厂表保存：应用位于 `0xc0000`，容量 `0x7d0000`；NVS 位于 `0x16000`。不要套用其他工程的 `0x10000` 应用偏移。已配置 PSRAM 分配、40 行双 DMA 显示缓冲与 USB Serial/JTAG 控制台。

## 烧录与恢复

原厂 16 MB Flash 已在首次烧录前完整备份，并分别提取 NVS 与工厂配置。备份 SHA-256：`330bce629ad877c86773c22709b89cf3119d593d468ee44580d4b20f7d026c49`。清单保存在 `backups/original_manifest.json`。

更新当前 Demo 仅写应用分区：

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem11201 --baud 921600 \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0xc0000 firmware/cursor-remaining/shanhai_echoear_demo.bin
```

首次安装还需依次写 `0x0` 的 bootloader、`0x8000` 的 partition-table、`0x1c000` 的 ota_data_initial 以及 `0xc0000` 的应用；禁止擦除整片 Flash。完整恢复原厂镜像会覆盖此后新增的 Wi-Fi 配置，应先另存当前 Flash。

## 素材重建

```sh
python3 -m venv --system-site-packages .venv
.venv/bin/python -m pip install -r requirements-dev.txt
.venv/bin/python tools/build_preview.py
.venv/bin/python tools/build_firmware_assets.py
```

固件素材生成器使用 Node.js、Sharp 和 `lv_font_conv`。可通过 `NODE_BIN`、`SHARP_MODULE`、`LV_FONT_CONV` 指向现有安装；本机默认路径已写入工具。生成内容纳入源码，因此普通固件构建无需重新安装素材工具。

五张背景共 1,296,000 bytes，以 Flash 中的 RGB565 直接作为图像源；仅当前页面的动态环使用 PSRAM。品牌标识来源见 `assets/logos/SOURCES.md`，Noto Sans JP 的字体许可见 `assets/fonts/OFL.txt`；背景生成提示词在 `docs/imagegen-prompts.json`。

## 检查复现

浏览器检查使用 Playwright 与 Chrome：

```sh
PLAYWRIGHT_MODULE=/Users/kongweilu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright \
CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
/Users/kongweilu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node tests/preview.cjs
.venv/bin/python tools/export_contact_sheet.py
```

主机定向检查（一条命令）：

```sh
IDF_PATH=/Users/kongweilu/esp/esp-idf-v6.1_release \
NODE_BIN=/Users/kongweilu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node \
PLAYWRIGHT_MODULE=/Users/kongweilu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright \
CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
tests/run_host_checks.sh
```

入口固定检查 SDK 提交，运行现有模型/天气/日历/AI/云状态/HTTP头 C 夹具（ASan/UBSan）、Codex/Cursor 合成授权夹具、预览源码与固件哈希/分区大小，以及七页/四家详情几何、网络状态、AI详情和日历分页浏览器回归。云状态检查使用真实发布逻辑和确定性桩，覆盖失败保留旧数据、刷新重试次数、网络/账号改变后的迟到响应、导入/忘记账号隔离。桩不模拟实际线程调度。测试桩在 `tests/host/`，临时二进制自动清理。需要宿主 `cc`、Python 3、Node、Playwright/Chrome 和已解析的 cJSON 组件；可用 `CC`、`PYTHON_BIN`、`NODE_BIN` 设置工具路径，新机器应调整上述本机路径。没有新测试框架或自动安装依赖。

这些检查不读取账号、打开登录、发起真实服务请求或操作USB；不代表实板验收。现有素材生成器继续使用 `.venv/bin/python tools/build_preview.py` 和 `.venv/bin/python tools/build_firmware_assets.py`，生成后用此入口检查交付一致性。

`tools/device_soak.py` 用单个 USB 连接连续三轮采集五页以及 0%/100% 的页码与 CRC 校验帧，检查诊断任务栈余量、无异常复位、三次 BLE 启动/取消/重连、请求中主动断开及重连抑制，然后执行默认 1800 秒联网/切页负载。需要已配网的设备、PySerial 和 Pillow。打开串口可能触发 USB 硬件复位；测试中的持续窗口不重新打开串口。串口截图是设备 LVGL 帧缓冲，不能代替 LCD 实物检查。

语音、摄像头与 OTA 留待后续。日历授权配置见 `docs/google-calendar-setup.md`，四家 AI 数据来源和限制见 `docs/ai-account-sources.md`。当前产物在 `firmware/cursor-remaining/`；前版构建在 `firmware/seven-page-delivery/` 和 `firmware/cursor-models-layout/`，旧设备产物仍保存在 `firmware/` 根目录。共享云HTTPS二十秒预算受 SDK 调用和 DNS 边界限制，可能延迟天气更新；天气工作线程与 UI 分离。当前固件和详尽实测状态以 `firmware/cursor-remaining/manifest.json` 与 `docs/validation.md` 为准。

主页描边定向帧检查：`.venv/bin/python tests/home_outline.py BEFORE.png AFTER.png`，仅用于360×360原生主页的修复前后CRC有效截图；不会自动操作USB，也不代表LCD可读性。

圆框素材定位回归：`.venv/bin/python tests/background_center.py`。共享背景偏移由layout.json生成固件宏并同步离线预览。Gemini/Claude授权准备步骤见[device-authorization-next.md](docs/device-authorization-next.md)；官方客户端登录不代表设备接入已完成。

正文统一+2px，大号数字不变。日历使用独立16px完整CJK字体（31px行高），普通正文用子集字体；Claude已通过隔离会话导入与设备真实读取，实际到期刷新待验证。

Cursor总览和详情的主数字、圆环及Other Models均表示剩余比例；服务返回的使用率在解析层换算，超额剩余显示0%，缺失不显示假百分比。
