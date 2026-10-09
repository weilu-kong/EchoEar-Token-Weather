# 新对话接续：EchoEar「山海司天」

更新时间：2026-10-09（Asia/Tokyo）。这是当前状态入口；历史记录见 [docs/validation.md](docs/validation.md)，当前产物见 [firmware/cursor-remaining/manifest.json](firmware/cursor-remaining/manifest.json)。

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
| Cursor | 独立OAuth与team_id已导入；所选团队内当前用户Cursor Models／Other Models使用率 | 更新后实板HTTP200、ESP_OK、valid=1；不是团队pooled总额 |
| Claude Code | 用户批准官方隔离会话导出/导入，设备直接读取5小时/7天订阅额度 | 主机HTTP200、真实C解析通过；设备ESP_OK、valid=1；到期续期尚未验证 |
| Antigravity | 保留适配器，未配置 | 个人OAuth与project获取契约未闭合，不能声称已接入 |

AI额度保留服务原单位／窗口；无上限时不计算百分比，模拟值明确标注デモ値。接口属于消费端接口，可能随服务更新而变化。

## 最近故障与修复

1. 日历字体14px的实际行高27px，旧标签18px造成上下裁切。时间标签改27px、标题54px；列表固定两个槽位，容量扩至12项，绝对事件索引与奇数／空列表／缩减后页码钳制同步修复。临时日历快照从PSRAM分配。
2. Codex和Cursor的长Authorization头超过ESP-IDF默认512字节TX缓冲。共享HTTP buffer_size_tx改为4608；RX缓冲不能解决此问题。无需重新授权。
3. Cursor billingCycleEnd为Unix毫秒字符串，旧代码当秒解析并拒绝；现专门转换为秒。

定向检查：固定SDK请求头合成回归、AI毫秒fixture与真实响应隔离解析、日历容量／解析边界、预览两项分页和手势通过。设备新固件三个已配置服务均200且解析成功。截图工具重新打开USB可能复位设备；最近只采到主页，不能作为日历实物验证。没有完成统一验收。

## 本轮 Cursor 与 Codex 显示修正

- Cursor 直接采用官方客户端 `planUsage.autoPercentUsed`（Cursor Models）与 `apiPercentUsed`（Other Models），详情显示使用率；主圆环也显示使用率，AI总览圆环继续表示剩余比例。复用现有两个窗口字段，不修改账号/NVS格式。缺失单项显示未提供，非法值拒绝且保留旧快照；没有两类字段时兼容旧金额响应。
- 详情共享布局左移14px、上移30px；实物反馈后将两个互相覆盖的底部面板合为一个，二级额度/重置/更新中心y=243/265/287（间距22px），详情标签限制为真实字体的一行高度。删除Codex圆环内重复余额行（含credits）、Cursor的TEAM使用率副标题；Cursor类别使用10px金色字。Codex的主窗口标签按用户要求最终调整为15px（真实行高19px；12px仍被用户反馈偏小），5小时／7天剩余额度语义保留；触摸热区同步，待机布局保持原样。
- 已通过主机真实C解析夹具（包含68/11、0/100、超额、单项缺失、非法值与旧格式），浏览器详情/触摸回归、原有日历分页回归、固定SDK构建。已识别ESP32-S3，仅写0xc0000并通过写入哈希校验。
- `preview/cursor_buckets.png`与`preview/codex_repositioned.png`是合成演示截图；不是实板LCD验收，也不是当前账号用量。新布局的实板可读性仍待用户反馈；整体实板验收继续暂停。
- 剩余接入研究：复查Antigravity官方CLI安装/远程SSH OAuth与`/usage`文档，确认官方客户端入口，但这些文档仍未给出设备所需的完整OAuth刷新/project/额度REST契约。Claude支持边界复查无变化。详见来源文档；两者继续未配置。

## 本轮交付整理

- 新增仓库内主机检查入口与隔离桩，移除对历史/tmp测试桩的依赖；SDK提交不符会提前拒绝。README提供本机命令和依赖要求。
- 原预览回归漏掉日历/事件详情，现覆盖七页与四家详情。完整检查复现Claude演示余额与单位重叠；单位恢复为单独一行y210，Codex独立保持15px/y193，Cursor仍不显示该副标题。预览与原生布局已同步。
- 最新构建保存在firmware/seven-page-delivery（仅构建、未烧录）；设备仍为firmware/cursor-models-layout中的15px版。两份应用哈希和分区容量均纳入主机检查。七页总览preview/overview.png已更新，均为合成演示。
- 字体许可、CJK来源和组件锁已核对，修正旧文档中未运行/未构建记录。Antigravity官方FAQ/CLI与Claude支持边界复查仍未建立可交付的独立设备额度授权，未发起新登录。

## 本轮云状态与凭据边界

- 新增tests/cloud_test.c，直接执行sh_cloud.c中的发布逻辑，使用确定性RTOS/网络/授权/获取桩；覆盖五项账号的成功、未配置、认证/限流/超时失败保留旧值，提前刷新和401刷新最多一次，网络generation、credential revision和导入epoch变化后的迟到响应丢弃，以及导入/忘记账号隔离和失败不改变快照。临时副本移除epoch保护后夹具失败，证明确实检测该边界；不是实际线程/实板验收。
- 修复sh_ai_fetch令牌上限与导入、日历不一致：4096字节现在允许，超过4096拒绝；长度读取改为有界strnlen。新增四家请求的最大长度、超长、无终止符上限与非法字符夹具，先失败后通过；没有真实HTTP或凭据。
- 六组C检查（ASan/UBSan）及现有浏览器检查通过，固定SDK构建通过。更新firmware/seven-page-delivery的bin/ELF/map和manifest，仍未烧录。设备保持已烧录15px版本；整体实板验收暂停。

## 最新主页可读性调整（已烧录）

用户反馈主页上方日期和「晴れ」与背景重叠。保留字号与素材，日期中心从108下移至115，时钟147、城市180、温度211、天气描述从251上移至239，高低温263；天气图标同步上移。按实际原生行高，主要文字之间保留3/3/6/4/9px间隔，日期与图标面板之间5.5px；交互热区保持覆盖天气区域。

七页/四家详情预览几何检查、内嵌布局源码一致性、产物哈希和固定SDK构建通过，预览已更新。重新识别ESP32-S3后只写0xc0000，Hash of data verified；最新产物在firmware/home-spacing，应用6,256,656字节，factory余量1,935,344字节。包含前两轮已构建的其他服务详情行距及令牌边界修正；账号、Wi-Fi和其他分区保留。主页新版本LCD可读性待用户反馈，整体实板验收继续暂停。

## 最新主页位图文字描边（已烧录）

用户实物照片确认「晴れ」和黄色高低温在云层处难辨，浏览器预览有stroke/shadow而实屏没有同等效果。LVGL软件渲染的outline属性走矢量字体路径；现有sh_font_*是位图字形，所以原text()中已有的描边样式不产生位图描边。仅主页home_condition和highlow增加8个±1px黑色字形层，正文覆盖在最前，动态更新同步所有层。位置/字号/背景保持；重建页面时由原有root清理机制释放层。预览这两项去掉模糊阴影，保留1px边缘描边。

固定SDK构建、七页浏览器回归和四份产物哈希/源码一致性检查通过；识别ESP32-S3后仅写0xc0000，Hash of data verified。最新产物firmware/home-outline：6,256,848字节，factory余量1,935,152字节。保留NVS/账号/Wi-Fi和其他分区。

修复前后只采集主页page=0，360x360、CRC通过。真实RGB565帧中描边色像素在天气描述区域4→71，高低温9→125；tests/home_outline.py断言通过，已目视检查晴れ与高低温字形。此检查证明原生帧已绘制描边；随后用户明确反馈“效果很棒”，主页当前描边可读性已确认。长时与整体验收仍暂停。截图在本机/private/tmp，未作为合成预览或账号样例提交。

## 本轮圆框居中与授权准备

用户确认偏心指整圈背景/金色外框偏离LCD圆心。源码BSP与ST77916配置为360x360、无mirror/swap及额外gap；LVGL root无padding/border，背景之前直接放(0,0)。最外圈金色边缘拟合显示原素材圆框中心约(177,173)，不是文字列或驱动坐标偏移。

共享layout新增background_offset=[3,7]，固件通过生成宏SH_BG_OFFSET_X/Y定位背景，浏览器包括设置弹层同步。文字/图标/热区不移动。tests/background_center.py测量每条非轴向射线的最外侧金色边缘，排除罗盘装饰；修复后五张中心(180.59,179.94)/(180.11,179.54)/(180.56,179.85)/(180.66,180.18)/(180.43,179.67)，误差<1px。最初混合内环/装饰的annulus拟合低估偏差，+2/+4的临时版原生中心(179.70,176.97)未通过，已改用外缘校准，不以该临时结果声称成功。

七页浏览器回归、离线源码/产物校验和固定SDK构建通过。最终仅写0xc0000并Hash of data verified，NVS/账号/Wi-Fi保留。新产物firmware/centered-background，应用6,256,848字节/factory余量1,935,152字节。CRC有效主页原生帧中心(180.84,180.03)，通过<1px断言；没有拍摄LCD机械中心实物，待用户确认，整体实板验收仍暂停。

授权步骤见docs/device-authorization-next.md。本机已创建Git忽略的.local/claude-device（0700），核对官方CLI auth login --claudeai及CLAUDE_CONFIG_DIR隔离登录文档。用户需要在官方浏览器完成该新会话授权；没有读取Keychain、导出token或修改已保存账号。官方登录不等于本项目设备授权已完成，后续仍需核对额度权限/响应及适用接入途径。Antigravity个人设备OAuth契约仍未闭合，不能给出已验证授权命令；不重用Calendar登录，也不申请替代的Gemini API key。

## 最新 Claude 接入与整体字号调整

用户已确认居中效果不错，保持background_offset=[3,7]。用户要求除了最大的一批字以外增加两个字号：统一small_text_increment=2，原字号<25px的正文、标题、说明和设置弹层文字均+2；25px及以上时间/温度/百分比不变。预览不再把小字自动缩回，普通标签采用Noto Sans JP子集；日历独立16px完整CJK字体sh_font_calendar（实际行高31/基线9），时间31px/双行标题62px。日历仍每屏两项；重新安排列表/事件详情高度、间隔及列表触摸热区。额度详情圆环半径60、底板y239和3行y251/273/295，给加大的文字留空间。主页更新失败文字缩为“更新失敗・天气状态”，保留原值和失败语义，避免长提示拥挤。

Claude：用户明确批准隔离新会话的Keychain导出及设备NVS导入（未加密风险已说明）。只处理.local/claude-device会话；本机私有文件0600、真实响应和实帧截图留在.local，不显示令牌/提交凭据。主机GET订阅额度HTTP200，C parser接受5小时/7天；设备直接刷新ESP_OK/valid=1。前三页定向native检查page0/3/5 CRC通过，Claude真实额度与日历文字可读；不将其扩大成LCD/触摸/长时验收。

首次USB导入没有ACK；设置DTR/RTS false并等待控制台status就绪后返回SH_ACCOUNT OK。导入工具采用同样的打开方式、延长启动等待；未据此断言所有平台复位问题均解决。Google/Codex/Cursor/Wi-Fi保留；Claude新授权已存设备NVS，初次读取成功不等于到期刷新完成。

字号回归先以Codex窗口17px复现旧15px失败，修复后六组C（ASan/UBSan）、合成授权、七页几何/大字不变小字+2、详情和日历分页通过。原生定向记录internal57,691/largest18,432/PSRAM16,752,724/console_stack4,212字节，是短时观测。最终镜像已写0xc0000并Hash of data verified，重启后page0/3/5 CRC通过、Claude真实帧为接続済み且本次启动更新，授权保留。最后服务活动status internal36011/largest18432/PSRAM16710368/console_stack4340，仅短时数据。固件/私有帧以firmware/readable-text-claude manifest为准；整体实板验收暂停，新字号可读性待用户反馈，Gemini/Antigravity仍未接入。

## 本轮总览/Claude窗口/主页细节

用户确认整体效果不错，要求三点：AI总览Codex与Cursor像Claude一样显示残り；Claude详情删除重复余额行并使5時間大小/位置与Codex一致；主页日期加黑描边并再放大2px，天气与高低温再放大2px。

总览共享balance优先unlimited，其次服务明确返回的has_percent剩余率，再回退原币值/单位；因此Codex的0 credits不再遮盖已存在的窗口百分比。Cursor总览残り来自remaining_percent，详情主环仍表示使用率，Other Models仍是使用率；不改变解析/窗口/账期数据。Claude与Codex detail_balance均空，detail_unit共同有效17px/y193。主页date有效16px+8层位图黑色1px描边、condition17px、highlow16px；时钟/城市/温度下移2px，天气y243/高低温y267，按真实行高保留间隔；天气宽度162，最长失败提示也能显示，日期宽180。背景居中3/7、主页其他描边和账号保留。

新增Codex同时百分比与0credits、Cursor剩余32%、Claude重复行删除/17px/193位置和主页16/17/16字号夹具；先复现0credits!=残り75%失败，再修改共享逻辑。六组C与七页/四家详情/日历分页浏览器回归、居中拟合及固定SDK构建通过。最新产物firmware/overview-window-home，已仅写0xc0000并Hash of data verified；定向native page0/2/3 CRC通过，恢复后总览确认Codex/Claude/Cursor残り，Claude无重复行且窗口位置/字号对齐，日期黑边及大字已确认原生渲染。初始Codex连接/响应头请求失败，受限观察后恢复ESP_OK/valid1；未改凭据或重新授权。整体实板/长时验收暂停，Gemini尚未接入，Claude到期续期未实测。

## 最新 Cursor 详情剩余率修正

用户确认效果不错，要求Cursor详情与总览统一为剩余。删除原生ring_percent的详情反转分支，直接复用quota_percent；detail_main也去掉Cursor的100-remain反转。Cursor Models标签为“Cursor Models 残り”，Other Models为剩余率并限制0–100，缺失仍未提供。预览主环/数字/两组标签同步；内部解析仍把服务使用率转换成remaining_percent，不改API/NVS/字段定义。

新增使用68%→剩余32%/Other89%的主数字、环文案、100/0、超额归零与缺失主额度--回归：先因旧68%显示失败；主缺失字段预览误回退余额28的边界也先失败后修正，原生本就--。六组C/七页/额度详情/日历分页完整回归和固定SDK构建通过，未新增字体或测试接口。现有串口只支持page选择，不能选详情provider；不把默认Claude详情截图误记为Cursor验证。最新产物firmware/cursor-remaining，已仅写0xc0000并Hash of data verified；应用7170208字节/factory余量1021792字节。没有自动采集指定Cursor provider的LCD帧，实际详情待用户反馈，整体实板验收暂停。

## 构建与硬件

- EchoEar v1.2：ESP32-S3，16MB Flash／16MB octal PSRAM，360×360 ST77916，CST816S。
- **ESP-IDF release/v6.1固定提交 9a97f6c54ec638111ce55cd36581b3c192f15207**；用户允许master作为备选，不降至旧SDK。
- BSP esp_vocat 1.1.0、LVGL9.4.0、network_provisioning1.3.1；精确组件锁见dependencies.lock。
- 最新已烧录应用7,170,208字节，factory容量8,192,000字节，余量1,021,792字节；SHA以firmware/cursor-remaining/manifest.json为准。SHA256见最新manifest。上一版静态DIRAM201,526字节、链接余量140,234字节；本次未重新量化，不是运行时堆保证。
- 短时观测internal heap63,663、largest17,408、PSRAM16,752,724、UI栈水位3,524字节；没有长时并发结论。
- 只更新应用 **0xc0000**，不擦除Wi-Fi／账号NVS／其他原厂分区。旧五页产物仍在firmware根目录，当前七页产物在firmware/cursor-remaining；上一版保存在firmware/live-accounts-calendar。

本机开发路径（新机器需调整）：

```sh
cd '/Users/kongweilu/Development/EchoEar Token&Weather/SHANHAI_EchoEar_Demo'
source /Users/kongweilu/esp/esp-idf-v6.1_release/export.sh
idf.py -B /private/tmp/shanhai_echoear_v61_build build
```

构建目录避开空格和&。实际设备端口 `/dev/cu.usbmodem11201`（ESP32-S3）；另一端口 `/dev/cu.usbmodem11401` 是ESP32-S31，不要操作。端口可能变化，烧录前重新确认芯片。

## 代码地图

- main/sh_ui.c：七页动态文字、触摸／手势、两项日历分页；main/sh_scene_data.h与main/sh_assets.c：由素材脚本生成的布局和图像。
- main/sh_cloud.*：云任务、服务状态、刷新节奏、快照；main/sh_auth.*：NVS凭据、OAuth刷新和revision。
- main/sh_http.*：共享HTTPS、证书、发送缓冲、响应大小／时限；main/sh_ai.*：各服务解析。
- main/sh_calendar.*：Google日程获取、严格日期和UTF-8解析；网络／天气相关源码在main。
- tools/layout.json、tools/preview.js：预览布局与交互；build_preview.py、build_firmware_assets.py同步生成。
- tools/*_authorize.py、import_accounts.py：一次性独立授权和USB导入；不要复制电脑会话替代已保存的独立授权。
- tests/run_host_checks.sh：固定SDK提交、一条命令执行C合成夹具（ASan/UBSan）、授权合成夹具、预览源码/产物哈希、七页/四家详情、AI详情和日历分页。隔离桩在tests/host；没有真实账号或USB操作，尚未接入CI。

## 用户最新确认的额度目标（以本节为准）

两个来源必须分开：

- Gemini：只显示Antigravity中的Gemini Models额度，Weekly Limit Remaining与Five Hour Limit Remaining（每周/5小时剩余百分比及重置时间）。用户截图99%/100%只是字段语义示例，不写入真实快照。不是Gemini CLI/AI Studio/API，也不把Antigravity内Claude and GPT模型组作为独立Claude账户的额度。
- Claude：显示用户自己的Claude账户在Claude桌面端/Claude Code中可用的订阅额度百分比，来源为该Claude账户；保留其5小时/7天额度窗口，按实际返回支持的窗口显示。不是Antigravity内Claude/GPT组，也不是Anthropic API账户余额或本机会话token/cost统计。固件现有Claude adapter的five_hour/seven_day解析对应这一目标；本轮用户批准隔离授权导出/导入后设备真实获取成功。到期后的续期与轮换仍未实测。

官方Antigravity models文档展示每周/5小时窗口；statusline quota map示例gemini-weekly仅证明CLI脚本输出接口，不直接证明HTTP响应。本机官方language_server静态描述包含POST /v1internal:retrieveUserQuota，Request.project、Response.buckets和BucketInfo.remainingAmount/remainingFraction/resetTime/tokenType/modelId。Gemini两窗口的bucket映射、独立个人授权及project仍待验证。现有Antigravity fetchAvailableModels取全模型最小剩余比例，不能直接代表所需Gemini组两窗口。

此前把用户的Claude目标解释为Antigravity内Claude/GPT组是误解，已纠正；后续必须分别推进Antigravity Gemini组和独立Claude账户接入，不将两者合并。没有启动登录、读取账户或改变固件。

## 之后的计划（按顺序）

1. 保持Google／Codex／Cursor已可用的独立运行链路；先查看新Cursor两类使用率、Codex详情可读性及新日历实体文字和上下滑动的用户反馈，仅处理实际问题，不要求重新授权。
2. 分别完成两家剩余额度：Antigravity来源的Gemini模型组（每周/5小时剩余率），核对retrieveUserQuota窗口映射、独立个人登录、刷新与project；用户独立Claude账户来源的桌面端/Claude Code订阅额度（5小时/7天，实际支持窗口为准），继续核实适用的独立授权与只读获取。两者不能互相替代；需要用户授权操作时说明具体条件，不把未配置显示成真实余额。
3. 完善交付与可复现性：核对当前七页预览、配置、字体／素材来源、组件锁和构建记录。账号状态有明确失败／过期／未配置区分；需要改动时先检查调用方与边界。
4. 用户恢复验收后统一执行：新日历布局与滑动、四家详情、0%／100%／未知上限、错误密码／重配、主动断开抑制重连、忘记账号／网络隔离、重启恢复、45秒待机唤醒、迟到结果、至少30分钟联网切页／内存监测。分别记录已测和未测；不将host解析／framebuffer当作LCD与触摸验收。
5. 日历扩展、语音、摄像头、OTA暂不增加；没有常驻电脑桥接服务。

## 私有资料与公开仓库

公开仓库不含 `.local/`、`backups/`、Google client_secret JSON、账号令牌、原厂Flash/NVS或真实额度响应。已有Google／Codex／Cursor凭据保留在设备NVS和本机私有目录；不重新输出、提交或导出它们。NVS尚未加密，物理Flash读取可获得凭据；不自动启用不可逆eFuse配置。Google测试模式刷新令牌可能7天失效，到时按实际错误处理。

本机父目录还保留SHANHAI_EchoEar_R1参考工程和Google客户端配置；它们不在此仓库。历史docs有阶段性“未导入／未烧录”等记录，以本文和最新manifest为当前状态。

## 新对话可直接使用的请求

> 请先读HANDOFF.md、README.md和docs/validation.md确认进度，再继续EchoEar山海司天项目。必须使用固定ESP-IDF v6.1，设备独立运行，保留已导入账号。Google Calendar、Codex、Cursor已在设备HTTP200；日历已改每屏两项和上下翻页。继续剩余Claude／Antigravity接入和开发整理；整体实板验收仍暂停，只有我恢复验收才统一测试。不要提交私有账号或Flash备份。
