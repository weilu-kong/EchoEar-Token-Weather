# Google Calendar：首次授权与设备独立运行

电脑只负责第一次打开 Google 授权页面和通过 USB 导入凭据。导入完成后，EchoEar 自行通过 HTTPS 刷新 access token、读取主日历，电脑和授权脚本都可以关闭。设备不依赖常驻服务或桥接，不创建、修改或发送日程。

## 1. 在 Google Cloud 创建 Desktop OAuth 客户端

1. 打开 [Google Cloud Console](https://console.cloud.google.com/)，选择或创建自己的项目。
2. 在 **APIs & Services → Library** 搜索 **Google Calendar API** 并启用。官方 [Calendar Python quickstart](https://developers.google.com/workspace/calendar/api/quickstart/python) 也说明了 API、授权页面和 Desktop 客户端的设置流程；本项目脚本只使用 Python 标准库，无须安装该 quickstart 的依赖。
3. 在 **Google Auth Platform → Branding** 填写应用名称、支持邮箱和开发者联系邮箱。
4. 在 **Audience** 选择适合账号的受众。个人 Gmail 账号选择 **External**；处于 **Testing** 时，在 **Test users** 添加实际授权的 Google 账号。Workspace 组织的 Internal 应用按管理员策略设置。
5. 在 **Data Access** 添加唯一所需 scope：`https://www.googleapis.com/auth/calendar.events.readonly`。
6. 在 **Clients → Create client → Desktop app** 创建客户端，并下载 JSON。必须使用下载内容中的 `installed` 客户端；Web application、API key、服务账号 JSON 都不适用。Google 的 [Desktop OAuth 官方文档](https://developers.google.com/identity/protocols/oauth2/native-app) 说明了回环地址、随机端口和 PKCE。

保存客户端文件到项目的 `.local/`，避免放入源码或版本库。新建目录时权限设为 700，客户端 JSON 权限设为 600。不要把文件内容、授权码、token 或 client secret 贴入聊天、截图或日志。

## 2. 运行一次性授权工具

在 `SHANHAI_EchoEar_Demo` 目录运行：

```sh
mkdir -p .local
chmod 700 .local
# 将下载的 Desktop JSON 保存为 .local/google-client.json
chmod 600 .local/google-client.json
python3 tools/google_authorize.py --client .local/google-client.json
```

工具打开系统浏览器。选择自己的账号并同意只读日程权限；测试应用如显示未验证提示，核对它确实是自己刚创建的项目。浏览器将返回临时 `127.0.0.1` 端口；5 分钟内完成后关闭页面并返回终端。工具校验随机 state，使用 PKCE S256，仅向 Google 固定 HTTPS token 端点交换授权码，不输出凭据或 Google 原始错误体。

结果写入 `.local/google-account.json`（文件 600、目录 700），包含设备需要的 `provider`、`access_token`、`refresh_token`、`client_id`、`client_secret` 和 Unix 秒级 `expires_at`。只有授权成功后才原子替换旧文件；失败不会覆盖已有授权。

## 3. 通过 USB 导入并关闭电脑端工具

设备连接 USB 后，使用本项目账号导入工具：

```sh
python3 tools/import_accounts.py --port /dev/cu.usbmodemXXXX --file .local/google-account.json
```

将端口替换为设备实际的 USB 串口。导入工具应明确返回设备确认后再断开；不要使用串口终端手工粘贴 JSON，以免留下凭据历史或回显。导入后保持设备 Wi-Fi 联网、系统时间已同步，日历由设备刷新。重新授权时再次运行授权工具并导入新的文件。账号清除功能删除设备本地授权；如要撤销云端授权，在 [Google 账号的第三方连接](https://myaccount.google.com/connections) 中移除自己的应用。

## 设备读取范围与授权寿命

设备读取 `primary` 主日历未来七天内仍未结束的最早三条事件，包含正在进行的事件和展开后的重复日程；只请求标题、开始/结束时间、地点、描述与状态。取消事件不显示；全日事件的结束日期是排他的。定时事件按 RFC3339 的时区偏移解析，全日日期以设备当前时区解释；此演示设备使用日本时间。长文字按 UTF-8 边界截断，控制字符不会传入界面。过滤规则及只读 scope 见 [Google events.list](https://developers.google.com/workspace/calendar/api/v3/reference/events/list)。

响应上限 16 KiB，每页最多三条，必要时最多读取八页稀疏结果；超过大小或分页上限会保留旧快照并显示读取失败，不发布不完整的新快照。

**External + Testing 的 refresh token 通常在 7 天后过期。** Calendar scope 不属于 Google 对基础身份 scope 的例外范围，因此设备独立刷新也不能延长这个测试授权期限。长期使用应按 Google 要求处理发布状态及可能的验证；撤销、组织策略、授权数量等原因仍可能使 token 失效。失效后重新授权并导入。见 [Google refresh token expiration](https://developers.google.com/identity/protocols/oauth2#expiration)。

设备凭据存于独立 NVS namespace；现有硬件 Flash 未加密，能够物理读取 Flash 的人可能取得授权。项目不会自动修改 eFuse。不要提交 `.local/`，不要分享含授权资料的 Flash/NVS 备份。

SDK 保持 `release/v6.1`，固定提交 `9a97f6c54ec638111ce55cd36581b3c192f15207`。按当前安排，本次开发不执行 OAuth、联网验收、自动测试或硬件操作；授权与设备验收由用户后续安排。
