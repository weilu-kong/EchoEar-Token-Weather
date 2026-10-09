# 验证记录

日期：2026-10-08，日本时间。第一阶段画面已确认后进行固件开发。

## 构建和设备

- ESP-IDF release/v6.1：`9a97f6c54ec638111ce55cd36581b3c192f15207`，SDK 工作树干净；未使用其他 SDK 构建或修改 vendor 组件。
- ESP32-S3 rev 0.2，16 MB Flash、16 MB octal PSRAM；EchoEar v1.2，ST77916/CST816S。
- BSP 1.1.0、LVGL 9.4.0、network_provisioning 1.3.1，完整版本见 dependencies.lock。
- 当前开发构建：3,591,712 bytes；SHA-256 `ccebe0f3c77fbbc8d69f4dea9c982b0e31899ec985971e83f85e4b0b3afd6612`。已完成原生编译并更新设备（esptool 写入校验通过）；统一功能验收按用户要求暂停。
- 应用分区 8,192,000 bytes，起始 0xc0000；原厂分区布局保留；构建提示 board_test 分区小于应用是预期情况，Demo 仅使用 factory 分区，不写 board_test。五张背景在 Flash 中共 1,296,000 bytes，当前页面进度环和 LVGL 分配使用 PSRAM，DMA 缓冲使用内部 RAM。静态 .bss 27,728 bytes、.data 24,671 bytes；DIRAM 总占用 162,198 bytes（含代码）。
- 实际 sdkconfig、配置头、ELF/map、引导程序及分区表已保存至 firmware/，完整文件散列见 manifest.json。

## 此前版本已完成检查

| 检查 | 结果与证据 |
|---|---|
| 浏览器五页、六种网络/错误状态 | 通过；tests/results.json，0 JS 错误、0 外部资源请求 |
| 圆形安全区、日文字形、文字碰撞 | 浏览器原尺寸字体测量通过；设备画面由用户确认正常 |
| 额度 0%/100%/未知上限 | C 模型断言及浏览器通过；此前实板边界截图通过；新页码协议回归暂停，待统一验收 |
| 天气缺字段、截断、错误 JSON、越界、无效 WMO 代码 | host ASan/UBSan 通过；tests/weather_test.c |
| 固定 SDK 原生构建 | 通过；最终应用已烧录 |
| 手机 BLE 扫码 / 错误密码 / 重配 | 用户确认错误提示正常，正确密码重配成功；日志已恢复 saved=1、connected=1 |
| 东京真实天气 | 日志显示 HTTPS 200、744–746 bytes、解析成功；真实天气页帧已采集 |
| 日本时间 | 实机 SNTP synchronized (JST)，页面时间已更新 |
| 天气失败保留旧值 | 实测 API HTTP 503，保留原数据并标记 stale；之后 200 恢复正常 |
| 触摸、左右滑动、45 秒待机/点击唤醒 | 用户确认画面和触摸均正常 |
| 三次同一启动内 BLE 启动/取消/重连 | 此前短实板检查通过；当前开发版本待统一复测 |
| 主动断开、保留凭据、抑制自动重连 | 短实板检查通过：断开后等待 5 秒仍离线，再连接成功 |
| 重启自动恢复已保存网络 | 多次应用更新/USB 复位后恢复 Wi-Fi、天气与 SNTP |
| 迟到天气结果 | 源码审查确认 generation 检查；尚未进行可控延迟服务器故障注入 |
| 30 分钟联网/切页负载 | 诊断任务 4 KB 栈发生实测溢出，已改为 8 KB，并加入页码与栈水位校验；2026-10-08 用户要求暂停验证，先完成开发；15 帧回归未完成（采集期间页码不匹配，不计为通过）；30 分钟测试未完成 |

源码复核见 firmware-review.md；此复核与实板测试分别记录。

## 备份与保密

首次烧录前完整读取 16,777,216 bytes 原厂 Flash。SHA-256：`330bce629ad877c86773c22709b89cf3119d593d468ee44580d4b20f7d026c49`。NVS、工厂配置单独提取，文件位于 backups/，访问权限收紧且不纳入 Git。没有进行全片擦除。

配网 PoP 保存在独立 NVS 命名空间，Wi-Fi 清除只使用 Wi-Fi 配置接口。屏上二维码包含设备凭据，未放入公开截图；固定 SDK 的 security1 INFO 密钥输出已抑制。

## 开发调整与待验收项

用户已确认错误密码提示正常，正确密码再次配网成功；日志确认保存凭据并联网。22:14 左右私有串口采集因 USB 连接中断而中止，原因未确认，未记录到 panic；端口重新出现后开始新的持续测试窗口。尚未做断电时写凭据、分配失败、可控迟到 HTTPS 结果故障注入。当前帧图是设备 LVGL framebuffer，不是 LCD 拍照；实体显示与触摸结论来自用户反馈。

HTTPS 十秒预算在 SDK 调用/回调边界检查，DNS 和缓慢响应可能超出这个时间。独立天气线程不持有显示锁，不会因等待 HTTP 阻塞切页。串口截图期间日志发送被互斥，属于本地诊断限制；正常显示不使用截图传输。

## 最近开发调整

用户反馈 Wi-Fi 入口不显眼，已改为 26 px 图标加 40 px 深色底板；额度「デモ値」增加小块深色底板。用户反馈待机信息与水面倒影重叠，已把信息组左移并上移至深色天空：时间 (200,133)、日期 (200,173)、天气图标 (160,204)、温度 (213,204)。背景不变，预览与原生布局同步。当前调整尚未进行统一验收；以前的 tests/results.json 不作为新布局通过的证据。

## 真实账号与 Google Calendar 开发进度（2026-10-08）

已加入独立 HTTPS 云任务、NVS `sh_auth` 凭据、过期 token 刷新、USB 单次导入工具，UI 页面 5/6 显示未来 7 天最近三项日历事件和详情。云任务等待日本时间同步，每 5 分钟刷新；断网保留数据并标记过期；网络 generation 或账号 revision 变化会丢弃旧请求结果。额度四家各自保留其实际单位和窗口，不把余额强制转换为百分比。

Google Desktop OAuth 客户端 JSON 类型已确认；用户将账号加入测试用户后，PKCE 浏览器只读日历授权成功，令牌保存到私有 `.local/google-account.json`（0600）。尚未导入设备；此前设备仍为五页天气 Demo。账号协议解析样例和 USB 导入 doctest 已保留，但按用户要求没有执行测试或实板验收。

测试状态下 Google Calendar 刷新令牌可能在 7 天后失效。当前设备未启用 Flash/NVS 加密，物理读取 Flash 可获取账号凭据；不自动修改 eFuse。AI 服务的消费端额度接口可能变化，Antigravity 暂需专用 OAuth JSON，桌面 OAuth 刷新令牌复制存在轮换冲突风险。

新增账号和日历源代码已经固定 v6.1 原生编译成功：应用 6,231,040 bytes，factory 分区余量 1,960,960 bytes。新产物独立保存在 firmware/live-accounts-calendar/，旧 firmware 根目录保留；未烧录或验收。

新构建静态 DIRAM 占用 183,606 bytes，链接区域余量 158,154 bytes；这些是链接静态数据，不能当作运行时堆、TLS/BLE 并发或栈水位实测值。size-report.txt 已保留。

## 2026-10-09 账号配置推进

用户完成独立 Codex 浏览器授权，工具确认私密记录保存成功。设备仅更新应用分区 0xc0000（新应用 SHA256 见 firmware/live-accounts-calendar/manifest.json），Google 与 Codex 独立凭据通过单次 USB 导入，收到各自 `SH_ACCOUNT OK` 回执。Wi-Fi/NVS/原厂其他分区未擦除。

这是开发固件安装与账号配置记录，不是实时额度、日历响应、画面、触摸、内存或持续联网的验收结果；用户要求的验收暂停仍有效。Claude/Cursor/Antigravity 尚未配置。

Cursor 首次登录返回团队选择字段，最初个人版工具拒绝并未保存凭据。随后新增 team_id 导入/持久化与 x-cursor-team-id 请求头，按官方客户端默认读数显示所选团队中当前用户的 included usage，标注 TEAM，不请求 pooled usage。再次浏览器授权已成功保存私密会话，设备导入待后续配置回执；所有夹具和实板验收仍未执行。

Claude 未建立独立设备授权，官方支持范围见 docs/ai-account-sources.md；Antigravity 个人登录参数和 project 获取契约未闭合，见 docs/antigravity-setup.md，保持未接入。

团队支持版最终应用 6,231,200 bytes 已完成原生 v6.1 构建并写入既有应用分区，烧录工具内建写入校验成功；Cursor 独立会话和所选 team_id 已私密导入 NVS，收到 `SH_ACCOUNT OK`。这是配置完成记录；实时额度/事件响应和 UI/RAM 仍未验收。Google 与 Codex 凭据保留。最终静态 DIRAM 183,606 bytes、链接区域余量 158,154 bytes，不代表运行堆。

## 2026-10-09 日历裁切与额度请求修复

用户反馈后进行定向排障：日历14px字体实际行高27，原18px标签裁切字形。改为每屏两项，时间高度27、标题高度54；上下滑动按两项翻页，详情保留绝对事件索引，未来7天最多保存12项。临时日历快照改由PSRAM分配。预览分页检查覆盖12项、奇数、空列表、详情索引和上下手势；日历解析边界夹具通过。

设备原日志证明默认512字节HTTP发送缓冲装不下授权头。共享buffer_size_tx改为4608，固定SDK头生成回归通过。Cursor billingCycleEnd毫秒时间戳修正为秒；最小夹具和已授权真实响应隔离解析通过。

固定v6.1原生构建成功，应用6,255,472字节，factory分区余量1,936,528字节；仅更新0xc0000，烧录工具写入校验成功，保留账号及Wi-Fi凭据。静态DIRAM201,526字节、链接余量140,234字节。交付manifest的SHA对应实际已烧录的保存产物；size命令重新构建的临时产物未再次烧录。

更新后实板定向刷新：Codex、Cursor、Google Calendar均HTTP200、ESP_OK、valid=1。Antigravity/Claude未配置。短时记录internal heap63,663字节、largest17,408字节、PSRAM16,752,724字节、UI栈水位3,524字节；不据此推断持续并发或30分钟稳定性。

新日历布局实体显示与触摸尚未验收；定向截图因USB打开导致复位采到主页，不计为日历验证。用户暂停整体实板验收的要求继续有效。

## 2026-10-09 Cursor 双类使用率与详情可读性

用户截图要求 Cursor 显示 Cursor Models 与 Other Models 的百分比使用率。根因：此前仅解析总 includedSpend/limit 的金额余额；安装官方客户端 schema 与 UsageDataService 已提供 autoPercentUsed/apiPercentUsed，且 UI 标签分别对应 Cursor Models/Other Models。新解析优先读取两项，不伪造缺失项；保留旧响应兼容性。详情主圆环为使用率，AI总览圆环仍为剩余率。额度正文明确标注使用。

共享详情布局左移14、上移30；二级额度行增加0.96遮光面板，底部面板透明度升至0.96；热区同步。背景与待机保持原素材。浏览器目视检查两页截图可读；截图为合成演示样例，不能作为LCD验收。

C夹具新增后先复现旧解析器68/11断言失败，修复后通过；覆盖0/100、110超额、缺失主类、非法负数/null拒绝且旧快照不变、旧美元字段。执行主机cc + SDK cJSON，HTTP使用原有stub，没有真实账号请求。tests/ai_detail.cjs通过两类文字、边界、缺失提示、恢复普通演示、Codex窗口与新热区点击；tests/calendar_paging.cjs通过原有分页/字体高度/手势回归。

固定SDK提交9a97f6c54ec638111ce55cd36581b3c192f15207完整构建通过，最终应用6,256,624字节，factory余量1,935,376字节。已重新识别/dev/cu.usbmodem11201为ESP32-S3，仅写0xc0000并通过Hash of data verified。新产物、ELF、map、sdkconfig与SHA见firmware/cursor-models-layout/manifest.json。没有修改NVS、其他分区或eFuse，没有重新授权。

新两类额度的真实值/LCD可读性、日历实体滑动与30分钟联网压力未验收；不能把上版HTTP200、主机夹具或烧录校验当作本版整体验收。

## 2026-10-09 实物反馈：详情底部重叠与圆环文字

用户实物照片确认上版三个底部标签存在重叠，并要求删除Codex重复余额行、Cursor TEAM使用率。根因包括两个底部面板区域互相覆盖，以及预览自动缩字与固件标签宽度/默认换行行为不同。此前预览通过不能代表LCD布局正确。

本轮合并为单个0.98遮光面板(y231/h68)，底部三行y243/265/287，间隔22px；当前10px字体line_height=13。原生详情标签固定一行高度，避免长值自动增加高度。删除Codex圆环内detail_balance所有内容，保留5小时窗口并移到y193；Cursor detail_unit为空，类别改为10px金色字置于圆环内。热区与面板同步。

新增回归先因仍含TEAM使用率而失败，修复后tests/ai_detail.cjs通过：指定文本移除、类别保留、三行间距、两类边界和底部点击。浏览器截图已重生成并目视检查；固定v6.1构建通过。最新应用6,256,608字节，分区余量1,935,392字节。LCD新版本可读性仍待用户反馈，整体实板验收/压力测试仍暂停。

本轮已重新识别ESP32-S3，仅写0xc0000，esptool Hash of data verified；最终保存产物SHA对应烧录镜像，NVS/其他分区保留。

2026-10-09 用户要求Codex「5時間」放大两号：仅Codex detail_unit从10px改为12px，复用现有sh_font_12_500（line_height=15）；同字号应用于标签高度。预览断言5時間实际字号12通过，固定v6.1构建通过；应用6,256,624字节，factory余量1,935,376字节。实屏新字号待反馈，整体实板验收仍暂停。
本轮已识别ESP32-S3，仅写0xc0000并通过Hash of data verified，保留NVS/其他分区。

2026-10-09 用户继续要求Codex「5時間」15px：已改用现有sh_font_15_500，实际行高19px，详情标签高度同步；预览字号15断言与原生构建通过。显示中心y193保持，主百分比和底部面板间距保留。
15px版本已仅写0xc0000并通过Hash of data verified；账号和Wi-Fi保留，整体实板验收未开展。

## 2026-10-09 七页交付与主机检查复现

新增tests/run_host_checks.sh与tests/host，固定校验SDK提交；五组C夹具使用ASan/UBSan，HTTP头使用实际固定SDK实现。授权仅运行Codex/Cursor合成fixture，不访问凭据。检查HTML内嵌布局/脚本与源码一致，以及两份应用大小/分区/SHA256；SDK错误时提前拒绝。原预览回归从五页扩至七页，并审计四家详情。

完整预览首次复现Claude演示余额/单位位置相距3px导致重叠；非Codex单位改y210，Codex保持15px/y193，余额y190。覆盖七页安全圆/文字重叠、网络/错误状态、额度边界、待机唤醒；另有Cursor双类与日历分页手势回归。浏览器无JS错误、无远程请求。重新生成并目视核对overview/network_states。

固定v6.1构建通过，应用6,256,656字节，factory余量1,935,344字节；保存firmware/seven-page-delivery。保留之前已烧录产物，没有USB、NVS、账号或eFuse操作。最新构建未烧录，整体实板验收/压力测试继续暂停。构建器仍提示原厂board_test分区不足以容纳此应用；本项目仅使用factory 0xc0000，不覆盖board_test。

## 2026-10-09 云状态生命周期与令牌上限

tests/cloud_test.c在主机直接包含现有sh_cloud.c，使用确定性互斥/任务通知、网络、授权与获取桩。覆盖五个账号成功/未配置、认证/限流/超时/不支持保留旧数据、一次提前刷新/一次认证失败刷新、generation/revision/epoch拒绝迟到响应、成功与失败导入/忘记账号隔离。夹具不是线程调度或真实NVS/HTTP测试。临时副本删除epoch保护时断言失败；正常代码通过ASan/UBSan。

边界不一致：sh_auth_import和日历支持4096字节access_token，sh_ai_fetch原先使用>=4096拒绝。新夹具在正常HTTP桩入口断言前失败，修复为有界strnlen(...,4097)和>4096后通过。四家AI均覆盖最大允许令牌的完整Bearer头；超长、上限无终止符、换行和空令牌拒绝，快照保留。HTTP桩直接返回不支持，没有真实请求。

整套主机入口通过（六组C启用ASan/UBSan，合成授权、产物一致性、七页/四家详情、Cursor双类、日历分页）。固定v6.1构建通过，应用仍6,256,656字节，factory余量1,935,344字节；更新firmware/seven-page-delivery。未烧录，未操作USB、NVS或eFuse；真实线程交错、LCD、触摸与长时运行仍待统一验收。

## 2026-10-09 主页日期与天气描述行距

用户要求调整主页日期和「晴れ」的行距以避开背景。仅修改共享布局：date 108→115，clock 142→147，城市/定位185→180，温度/天气图标220→211，home_condition 251→239，highlow 273→263。字号、背景和热区保留；温度/状态都仍在天气点击区域。原生生成布局与离线HTML同步，预览截图和总览已更新。

七页与四家详情几何检查通过，无JS错误/远程请求；内嵌布局/脚本一致性、三份应用哈希/容量通过。固定v6.1构建通过，应用6,256,656字节；重新检查ESP32-S3后只写0xc0000，Hash of data verified。保存firmware/home-spacing的bin/ELF/map/sdkconfig/manifest，没有NVS/其他分区/eFuse操作。

本次浏览器目视检查不能代表LCD可读性；等待用户反馈，整体实板/长时验收仍暂停。

## 2026-10-09 实物反馈：主页位图描边

照片显示浏览器黑边/阴影与LCD不同。查看固定LVGL9.4软件draw_letter实现，outline样式在FreeType/矢量字形路径处理，位图字形没有该描边。原sh_ui text()虽设置text_outline_stroke属性，但当前位图字体不能靠它获得预览效果。

仅home_condition/highlow创建8个±1px深色字形层作为1px描边，正文位于最前；refresh同步文本，页面清理复用原有root子对象释放。增加16个label对象和绑定的outline指针，未推广到其他标签；当前内存/长时成本未量化。预览两项移除shadowBlur，保留stroke。位置、字号、素材不变。

固定SDK构建通过，应用6,256,848字节/factory余量1,935,152字节；七页/四家详情浏览器回归通过，源码/产物哈希一致。只写0xc0000并Hash of data verified，无账号/NVS/eFuse改动。

修复前后主页原生帧page0、360x360/259200字节、CRC均通过。前图在启动获取天气状态，后图已有实天气晴れ及高低温；描边色RGB565解码像素在天气区域4→71、高低温9→125。tests/home_outline.py通过，目视确认黑边已出现在原生字形。它验证描边存在，不是同一天气字符串的逐像素差分，也不能代表LCD/触摸/长时验收。整体实板验收继续暂停。

用户随后明确反馈“效果很棒”：主页当前描边效果获实物确认；不扩大为其他页面或整体/长时验收结论。

## 2026-10-09 所有页面金色外框居中

用户确认问题为外框相对物理圆屏偏心。BSP固定360x360、无mirror/swap/gap；根对象padding/border为0。初始素材annulus混合内部装饰点测量得到约(178,176)，临时+2/+4版虽满足该错误度量，原生外缘复核(179.70,176.97)未过，不作为成功结论。

改测最外侧金色边缘：每度角采集最外黄金像素，排除轴向±12度罗盘装饰，再做圆拟合。源图外框约(177,173)。统一背景位移+3/+7，保留前景及热区坐标；共享layout生成原生宏并用于浏览器普通页/设置弹层。五张中心(180.59,179.94)/(180.11,179.54)/(180.56,179.85)/(180.66,180.18)/(180.43,179.67)，误差均<1。 tests/background_center.py修复前失败、后通过。

七页/四家详情浏览器几何检查通过；固定v6.1构建通过，6,256,848字节/factory余量1,935,152。最终只写0xc0000并Hash of data verified；原生page0 360x360/259200字节CRC通过，实帧外缘中心(180.84,180.03)，<1px断言通过。NVS/账号/Wi-Fi/其他分区保留。该测量验证软件画框位置，不是LCD机械圆边计量；用户实物确认及整体/长时验收尚未进行。

Claude授权准备：私有隔离目录.local/claude-device 0700；官方CLI help确认auth login --claudeai，官方文档支持CLAUDE_CONFIG_DIR独立账户。只准备目录、命令与说明，没有发起登录、读取Keychain、导出或导入新凭据。Antigravity独立OAuth契约仍未闭合。步骤见docs/device-authorization-next.md。

## 2026-10-09 Claude 接入与正文统一放大

用户已完成官方隔离登录，并在自动审批拒绝初次导出后明确允许新会话导出/设备NVS导入。经再次批准，仅导出隔离Keychain授权到.local私有0600文件。主机GET usage HTTP200，真实响应由C parser通过五小时/七天字段断言；响应不公开。首次USB导入未收到ACK；同一账号记录改用DTR/RTS false、控制台ready检查后SH_ACCOUNT OK，设备直接刷新ESP_OK、valid=1。工具据此设置打开前信号false并增加启动等待；tests/import_accounts_test.py用合成端口断言信号和ACK流程、不输出token，不接触真实USB。

正文/说明/小标题原字号<25统一+2px，>=25大数字不变；Codex窗口15→17。预览小字不自动缩回，普通正文采用JP子集，与16px完整CJK日历字体分离。日历实际line_height31/base_line9，时间标签31、双行标题62；列表仍两项，热区/事件面板同步。详情圆环r60/y174、面板y239/h68、底部251/273/295；天气更新时间移到(190,311)，避免错误提示超出安全圆。

新增字号要求先复现15!=17断言失败，再实现。六组C启用ASan/UBSan、合成授权、产物哈希、七页/四家详情、正文+2大字不变、日历31/62分页手势回归通过。原生定向page0/3/5 360x360 CRC通过，目视Claude真实额度及日历大字无裁切；记录internal57691/largest18432/PSRAM16752724/console_stack4212，仅短时数据。

原生主页发现旧“更新失敗・前回データ”在新字号下拥挤；新增更新失敗・晴れ夹具先失败，再缩为更新失敗＋实际天气，保留失败及旧天气语义。整套回归再次通过，固定SDK最终build通过；应用7170480字节，factory余量1021520字节，只写0xc0000并通过Hash of data verified。账号/Wi-Fi/NVS保留。最新产物firmware/readable-text-claude。整体LCD/触摸/长时验收继续暂停，实际到期的Claude token续期尚未验证。

最终收尾镜像重新启动后的page0/3/5 CRC均通过，Claude帧为接続済み且更新时刻为本次启动，确认授权保留和重新获取；主页现为正常天气短标签。最后status internal36011/largest18432/PSRAM16710368/console_stack4340字节，为服务活动中的短时快照，不能与此前空闲时数字直接比较或视为长时余量。私有截图保留.local/readable-native，不放入演示预览或公开仓库。用户已确认上一版居中效果，本版字体LCD可读性仍待反馈。

## 2026-10-09 总览剩余率、Claude窗口及主页日期

用户要求Codex/ Cursor总览改为残り；根因共享balance先选has_balance/ Cursor使用率，再选has_percent。现unlimited先保留无限，随后优先服务返回的剩余比例，再回退实际单位，不伪造缺失比例。Cursor详情和Other Models仍使用使用率，数据解析不变。

Claude详情detail_balance与Codex一起空，主窗口共用有效17px/y193及窗口格式；不删底部7天窗口。主页date有效16px并增加8个±1px黑色位图层；condition有效17px、高低温16px与城市字号一致；时钟/城市/温度y+2，weather243、highlow267。日期宽180、天气162、高低温130，保留安全圆与正常/过期提示空间。

新增夹具先复现0credits!=残り75%，修复后Codex百分比+零credits、Cursor残り32%、Claude重复行空/窗口17px/y193、主页16/17/16字号断言通过。完整六组C（ASan/UBSan）与七页/详情/日历分页回归通过，居中拟合保持<1，固定v6.1构建通过。没有新增字库，复用已有16/17字号。最新产物firmware/overview-window-home；定向烧录/原生帧以manifest和后续本节记录为准，整体LCD/触摸/长时验收仍暂停。

本轮已仅写0xc0000并Hash of data verified；应用7170512字节/factory余量1021488字节。原生page2/3/0均360x360且CRC通过，Claude与Cursor总览残り、Claude单一大百分比＋17px窗口、主页日期/底部大字已目视确认；日期描边区域深色像素70→646通过。

初次native Codex读取ESP_ERR_HTTP_FETCH_HEADER，受限重试ESP_ERR_HTTP_CONNECT，其余三项ESP_OK/valid1；只读TLS错误观察随后Codex ESP_OK/valid1恢复。本机不带凭据GET相同端点返回401，说明对照时TLS端点可达。不据此断言失败的具体根因；未改网络驱动/凭据/重授权。保留消费端接口的失败状态，不将模拟百分比当真实值。

恢复后的第二轮bounded native记录Codex/Claude/Cursor/Google均ESP_OK、valid1；page2 CRC有效，目视Codex/Claude/Cursor的残り标签真实显示。截图保存.local/overview-window-native/overview-recovered.png，未放入公开演示。最后status internal55503/largest12800/PSRAM16752724/console_stack4344为短时活动快照，不推断长时内存保证。日期、Claude详情及总览的最终实现已完成，LCD实物效果仍待用户确认。

## 2026-10-09 Cursor 详情统一剩余

用户指出详情使用80%与总览不一致。删除ring_percent详情反转层，主环直接quota_percent；detail_main删除Cursor反转100-remain，模型标签变为残り，Other Models使用内部secondary_percent并限制0–100。解析/HTTP/凭据不改。预览同步，无需新增字库。

新32%剩余/89%Other标签与环文案回归先复现旧68%使用显示失败；0/100、超额归零及缺失字段--/未提供通过。移除反转时发现预览缺失主额度误回退余额28，新增断言先失败后加缺失保护；原生缺失本就--。六组C（ASan/UBSan）、完整七页/AI详情/日历分页回归通过，固定SDK构建成功。应用7170208字节/factory余量1021792；识别ESP32-S3后只写0xc0000并Hash of data verified，NVS/Wi-Fi保留。

本机串口只能导航页，不支持服务provider选择；没有新增诊断接口，也没有把默认Claude帧误标Cursor。Cursor实际LCD详情未自动采帧，用户效果反馈待确认。整体实板/长时验收继续暂停。
