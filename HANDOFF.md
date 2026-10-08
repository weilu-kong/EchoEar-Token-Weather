# 新对话接续：EchoEar「山海司天」

更新时间：2026-10-09（Asia/Tokyo）。这是当前状态入口；历史记录见 [docs/validation.md](docs/validation.md)，构建产物见 [firmware/live-accounts-calendar/manifest.json](firmware/live-accounts-calendar/manifest.json)。

## 用户目标与边界

EchoEar v1.2 独立运行的日文圆屏显示器，显示东京天气、AI 服务额度、Google Calendar。运行时不能依赖电脑或常驻桥接服务；电脑只用于初次浏览器授权、USB 导入和开发。玄青鎏金、凤凰／神兽／山水贴图风格已获用户认可，背景与动态数值分离。

用户要求：**先完成开发，再统一验收**。目前整体实板验收暂停，不自动开展30分钟压力测试、配网全流程或全页面验收。最近用户明确授权的日历裁切和额度失败定向排障已完成。正式改动前先读取涉及源码，保留现有账号和Wi-Fi，不全片擦除，不修改eFuse。

## 当前进度

| 功能 | 当前结果 | 证据／限制 |
| --- | --- | --- |
| 页面 | 主页、天气、AI总览、服务详情、待机、日历列表、事件详情七页 | 已部署；原页面画面和触摸用户此前确认正常 |
| 风格／待机 | 贴图、动态文字和额度环；待机信息已移至深色天空 | 用户反馈效果不错；后续保持布局 |
| Wi-Fi | BLE扫码、独立PoP、断开／重连、确认后忘记、重启恢复 | 用户确认错误密码提示正常、正确密码再次配网成功 |
| 东京天气 | Open-Meteo HTTPS，联网立即／15分钟刷新，SNTP日本时间 | 实板成功；失败保留旧值并标记过期，无数据显示-- |
| Google Calendar | 独立只读OAuth已导入；设备直接获取未来7天日程 | 更新后实板HTTP200、ESP_OK、valid=1 |
| 日历列表修复 | 每屏2项，标题2行，上下滑动每次翻2项，最多12项；点击进入对应详情 | 预览分页回归通过；新布局实体显示和滑动尚未验收 |
| Codex | 独立OAuth已导入，设备直接读取窗口额度／余额 | 更新后实板HTTP200、ESP_OK、valid=1 |
| Cursor | 独立OAuth与team_id已导入；所选团队内当前用户included usage | 更新后实板HTTP200、ESP_OK、valid=1；不是团队pooled总额 |
| Claude Code | 保留适配器和手工导入能力，未配置 | 尚无可交付的独立设备授权；官方支持边界见来源文档 |
| Antigravity | 保留适配器，未配置 | 个人OAuth与project获取契约未闭合，不能声称已接入 |

AI额度保留服务原单位／窗口；无上限时不计算百分比，模拟值明确标注デモ値。接口属于消费端接口，可能随服务更新而变化。

## 最近故障与修复

1. 日历字体14px的实际行高27px，旧标签18px造成上下裁切。时间标签改27px、标题54px；列表固定两个槽位，容量扩至12项，绝对事件索引与奇数／空列表／缩减后页码钳制同步修复。临时日历快照从PSRAM分配。
2. Codex和Cursor的长Authorization头超过ESP-IDF默认512字节TX缓冲。共享HTTP buffer_size_tx改为4608；RX缓冲不能解决此问题。无需重新授权。
3. Cursor billingCycleEnd为Unix毫秒字符串，旧代码当秒解析并拒绝；现专门转换为秒。

定向检查：固定SDK请求头合成回归、AI毫秒fixture与真实响应隔离解析、日历容量／解析边界、预览两项分页和手势通过。设备新固件三个已配置服务均200且解析成功。截图工具重新打开USB可能复位设备；最近只采到主页，不能作为日历实物验证。没有完成统一验收。

## 构建与硬件

- EchoEar v1.2：ESP32-S3，16MB Flash／16MB octal PSRAM，360×360 ST77916，CST816S。
- **ESP-IDF release/v6.1固定提交 9a97f6c54ec638111ce55cd36581b3c192f15207**；用户允许master作为备选，不降至旧SDK。
- BSP esp_vocat 1.1.0、LVGL9.4.0、network_provisioning1.3.1；精确组件锁见dependencies.lock。
- 最新已烧录应用6,255,472字节，factory容量8,192,000字节，余量1,936,528字节。SHA256见最新manifest。静态DIRAM201,526字节、链接余量140,234字节；不是运行时堆保证。
- 短时观测internal heap63,663、largest17,408、PSRAM16,752,724、UI栈水位3,524字节；没有长时并发结论。
- 只更新应用 **0xc0000**，不擦除Wi-Fi／账号NVS／其他原厂分区。旧五页产物仍在firmware根目录，当前七页产物在firmware/live-accounts-calendar。

本机开发路径（新机器需调整）：

```sh
cd '/Users/kongweilu/Development/EchoEar Token&Weather/SHANHAI_EchoEar_Demo'
source /Users/kongweilu/esp/esp-idf-v6.1_release/export.sh
idf.py -B /private/tmp/shanhai_echoear_v61_build build
```

构建目录避开空格和&。实际设备端口 `/dev/cu.usbmodem11201`（ESP32-S3）；另一端口 `/dev/cu.usbmodem11401` 是ESP32-S31，不要操作。端口可能变化，烧录前重新确认芯片。

## 代码地图

- main/sh_ui.c：七页动态文字、触摸／手势、两项日历分页；main/generated：由素材脚本生成的布局和图像。
- main/sh_cloud.*：云任务、服务状态、刷新节奏、快照；main/sh_auth.*：NVS凭据、OAuth刷新和revision。
- main/sh_http.*：共享HTTPS、证书、发送缓冲、响应大小／时限；main/sh_ai.*：各服务解析。
- main/sh_calendar.*：Google日程获取、严格日期和UTF-8解析；网络／天气相关源码在main。
- tools/layout.json、tools/preview.js：预览布局与交互；build_preview.py、build_firmware_assets.py同步生成。
- tools/*_authorize.py、import_accounts.py：一次性独立授权和USB导入；不要复制电脑会话替代已保存的独立授权。
- tests/calendar_paging.cjs、calendar_test.c、ai_account_test.c、http_header_test.c：最近定向回归。部分C检查需SDK头或隔离HTTP／heap桩；历史执行范围见validation，未统一包装成CI。

## 之后的计划（按顺序）

1. 保持Google／Codex／Cursor已可用的独立运行链路；先查看新日历实体文字和上下滑动的用户反馈，仅处理实际问题，不要求重新授权。
2. 继续完成剩余真实额度接入：研究Antigravity完整个人登录、刷新与project契约；核实Claude可支持的独立设备授权或用户提供凭据途径。遇到确实需要用户操作的授权步骤才说明具体条件，不把未配置显示成真实余额。
3. 完善交付与可复现性：核对当前七页预览、配置、字体／素材来源、组件锁和构建记录。账号状态有明确失败／过期／未配置区分；需要改动时先检查调用方与边界。
4. 用户恢复验收后统一执行：新日历布局与滑动、四家详情、0%／100%／未知上限、错误密码／重配、主动断开抑制重连、忘记账号／网络隔离、重启恢复、45秒待机唤醒、迟到结果、至少30分钟联网切页／内存监测。分别记录已测和未测；不将host解析／framebuffer当作LCD与触摸验收。
5. 日历扩展、语音、摄像头、OTA暂不增加；没有常驻电脑桥接服务。

## 私有资料与公开仓库

公开仓库不含 `.local/`、`backups/`、Google client_secret JSON、账号令牌、原厂Flash/NVS或真实额度响应。已有Google／Codex／Cursor凭据保留在设备NVS和本机私有目录；不重新输出、提交或导出它们。NVS尚未加密，物理Flash读取可获得凭据；不自动启用不可逆eFuse配置。Google测试模式刷新令牌可能7天失效，到时按实际错误处理。

本机父目录还保留SHANHAI_EchoEar_R1参考工程和Google客户端配置；它们不在此仓库。历史docs有阶段性“未导入／未烧录”等记录，以本文和最新manifest为当前状态。

## 新对话可直接使用的请求

> 请先读HANDOFF.md、README.md和docs/validation.md确认进度，再继续EchoEar山海司天项目。必须使用固定ESP-IDF v6.1，设备独立运行，保留已导入账号。Google Calendar、Codex、Cursor已在设备HTTP200；日历已改每屏两项和上下翻页。继续剩余Claude／Antigravity接入和开发整理；整体实板验收仍暂停，只有我恢复验收才统一测试。不要提交私有账号或Flash备份。
