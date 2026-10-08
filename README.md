# 山海司天 · EchoEar Demo

设备已更新为七页开发固件，新增真实账号额度、Google Calendar 列表和事件详情；固定 v6.1 编译完成，Google、Codex 与 Cursor 独立账号已通过 USB 导入（Cursor 支持所选团队内当前用户额度），最近定向排障后，三者实板 HTTP 200 且解析成功；整体实板验收仍暂停。设备直接 HTTPS 获取数据并刷新 OAuth，不依赖运行中的电脑；电脑仅用于一次性授权和 USB 导入。同级 R1 工程保留。

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
  0xc0000 firmware/live-accounts-calendar/shanhai_echoear_demo.bin
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

C 模型与天气解析检查：

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/model_test.c main/sh_model.c -o /private/tmp/shanhai_model_test
/private/tmp/shanhai_model_test
cc -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations \
  -fsanitize=address,undefined -I main -I managed_components/espressif__cjson/cJSON \
  tests/weather_test.c main/sh_weather_parse.c main/sh_model.c \
  managed_components/espressif__cjson/cJSON/cJSON.c -lm -o /private/tmp/shanhai_weather_test
/private/tmp/shanhai_weather_test
```

`tools/device_soak.py` 用单个 USB 连接连续三轮采集五页以及 0%/100% 的页码与 CRC 校验帧，检查诊断任务栈余量、无异常复位、三次 BLE 启动/取消/重连、请求中主动断开及重连抑制，然后执行默认 1800 秒联网/切页负载。需要已配网的设备、PySerial 和 Pillow。打开串口可能触发 USB 硬件复位；测试中的持续窗口不重新打开串口。串口截图是设备 LVGL 帧缓冲，不能代替 LCD 实物检查。

语音、摄像头与 OTA 留待后续。日历授权配置见 `docs/google-calendar-setup.md`，四家 AI 数据来源和限制见 `docs/ai-account-sources.md`。新开发产物在 `firmware/live-accounts-calendar/`，旧设备产物仍保存在 `firmware/` 根目录。HTTPS 十秒预算受 SDK 调用和 DNS 边界限制，可能延迟天气更新；天气工作线程与 UI 分离。当前固件和详尽实测状态以 `firmware/live-accounts-calendar/manifest.json` 与 `docs/validation.md` 为准。
