# 2026-10-09 额度与日历排障证据

内部证据台账（勿发客户）：SDK排障场景05，固定 release/v6.1 9a97f6c54ec638111ce55cd36581b3c192f15207。

| 项目 | 原因/范围 | 证据 | 状态 |
| --- | --- | --- | --- |
| 日历文字裁切 | 本地字体14px500真实line_height27，列表高度18 | main/fonts/sh_font_14_500.c line_height，main/sh_ui.c固定列表高度；用户实物照片 | verified |
| 额度请求头 | 固定SDK的缺省HTTP TX缓冲512，strict header buffer启用；Codex bearer1732bytes；设备日志required1799/1757bytes但可用472/510；另有required525但可用510 | sdkconfig CONFIG_ESP_HTTP_CLIENT_STRICT_HEADER_BUFFER；私有backups/account_failure_diagnostic.log；官方SDK components/esp_http_client/esp_http_client.c config默认与header长度检查 | verified |
| 官方配置依据 | OAuth请求需增大buffer_size_tx，RX buffer_size不影响TX | https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/http.html#how-can-i-resolve-the-error-of-http-header-buffer-length-is-small-to-fit-all-the-headers-returned-by-http-request | verified |
| 账号授权 | 各一次已授权账号只读quota请求均200；Codex1222B，Cursor1360B | private .local response，未记录值/标识/headers公开输出 | verified |
| Cursor解析 | billingCycleEnd字符串为毫秒，本机官方客户端按new Date(Number(...))解释；旧解析器仅支持<=12位秒 | 官方安装Cursor UsageDataService；真实响应字段类型；sh_ai.c cursor parser | verified |

preflight: all hard claims mapped; exact设备最终显示与滑动尚未检查，不从授权或原生构建推断通过。

针对性回归：直接编译固定SDK的http_header.c/http_utils.c，以1732字节和4096字节合成Bearer分别复现512字节缓冲无法生成Authorization头；4608字节均生成完整头。检查已通过，没有网络或真实token参与。Cursor最小毫秒账期fixture与两份私密真实响应隔离解析也通过：Codex原解析可用，Cursor单位修正后可用。最终实板HTTP响应检查待更新固件。

更新后实板定向刷新：Codex HTTP 200/1223B、Cursor HTTP 200/1360B、Calendar HTTP 200/541B；三者 ESP_OK 且 valid=1。应用分区写入校验成功，凭据保留。全体验收继续暂停；设备截图采集因复位得到主页，不能作为日历页通过证据。
