# Antigravity 独立设备授权：当前证据与缺失项

截至 2026-10-09，Antigravity 尚未接入喵伴。本次只读取官方文档及已安装 Google 官方应用的静态代码，没有读取用户 token、Keychain、账号数据库、配置或日志，没有启动登录、联网探测、测试、验收或硬件操作。

Google Calendar 与 Codex 的已有独立授权不受此项影响。Antigravity 不能借用 Calendar 的 Desktop OAuth 客户端，也不能把 Calendar 的 refresh token 或 scope 换成 Antigravity 使用。

## 核对了什么

本机安装为 **Antigravity 2.0.6**，版本来自 `/Applications/Antigravity.app/Contents/Resources/app.asar` 内的 `package.json`。

| 静态来源 | 已确认内容 | 不能据此确认的内容 |
| --- | --- | --- |
| `app.asar: dist/languageServer.js` | Electron 启动 `Resources/bin/language_server`，识别 Google OAuth 授权 URL；启动代码包含 Cloud Code 服务地址 | 浏览器授权本身的 client、scope、回调与 token 交换配置 |
| `language_server` 的 Go 函数表及有界函数反汇编 | `authclient.(*AuthClient).LoginWithBrowser` 和 `codeassistclient.(*StandaloneAuthProvider).performAuthFlow` 引用静态 Google OAuth 客户端常量、client secret 与 localhost 回调字串 | 个人与企业登录分支各自采用哪套完整配置；回调路径、端口规则和完整参数 |
| 同一二进制中的 API 符号与常量 | 存在 `fetchLoadCodeAssistResponse`、`OnboardUser`、`GetCAICProject`、`fetchAvailableModels`；存在 `cloud-platform`、用户身份及其他 scope 常量 | 字串共存不能证明这些 scope 属于同一次个人账号授权；函数存在不能证明 project 的 REST 字段与生命周期 |

发现了两套 OAuth client ID，而不是唯一可直接取用的客户端。静态 client secret 是应用内的公开客户端常量，**不是用户凭据**；本次没有将它打印到终端、写入文档或复制到固件。即使找到了常量，也不足以证明完整授权流程兼容当前个人账号额度端点。

## 官方公开资料能证明的范围

- 官方 [CLI 安装与授权文档](https://antigravity.google/docs/cli/install) 描述了本地浏览器登录、远程 SSH 授权，以及本地系统安全存储中的会话。它没有公开供本项目独立 OAuth 工具使用的完整客户端配置，也没有给出个人账号额度 REST 契约。其 Gemini API key 模式用于独立 API 请求，不能据此视为现有 Antigravity 订阅的额度凭据。
- 官方 [Enterprise 设置文档](https://antigravity.google/docs/enterprise) 描述了 Cloud 项目、企业 API 和 ADC，并说明 ADC 的 project 优先从 `quota_project_id` 等来源解析。这是企业/Cloud 路径的证据，不能替代个人 Antigravity 账号的 Code Assist project 获取规则。本项目没有运行 `gcloud`，也没有读取 ADC 文件或假设它存在。
- Google 的 [Desktop OAuth 文档](https://developers.google.com/identity/protocols/oauth2/native-app) 说明了回环回调和 PKCE，但一般 OAuth 机制不等于 Antigravity 专用 client 的回调注册、scope 和服务权限已得到确认。
- 官方 [Antigravity FAQ](https://antigravity.google/docs/faq#why-cant-i-use-third-party-software-such-as-claude-code-openclaw-or-opencode-with-my-antigravity-login) 明确提示第三方软件使用 Antigravity 登录访问服务可能导致账号暂停或终止。该说明主要涉及第三方产品访问，并未给出本项目这种只读额度显示器的豁免或专用授权方式；此风险不能由客户端静态常量消除。

## 落地工具还缺什么

需要同一条个人账号登录路径的完整证据，而不是分别从不同登录方式拼接参数：

1. **OAuth 配置**：明确的 client ID/secret 对应关系、实际授权 scope 集合、注册回调 URI/端口规则，以及 access/refresh token 交换与刷新参数。必须确认一次性独立浏览器授权能取得设备可自行刷新的 refresh token。
2. **Project 获取**：`loadCodeAssist` 在现有个人账号下的请求、响应字段、project 返回类型，以及首次使用时是否还需要 `onboardUser`。不能创建随机 project，也不能用 Calendar 的 Cloud project ID 代替。
3. **额度接口兼容性**：上述 token、scope 与 project 确实适用于现有设备 `fetchAvailableModels` 只读额度路径，而不是 Enterprise、ADC 或其他客户端分支；同时需要明确项目是否允许这种独立只读访问。

这些项未闭合前，不生成 `tools/antigravity_authorize.py`，不导入猜测凭据，界面保持未配置/未连接。后续得到可核对的完整契约后，一次性工具才可采用随机 state、PKCE、固定 HTTPS 端点、受限回环回调及原子私有文件写入；输出仅需设备现有 `sh_auth` 接受的 `provider=antigravity`、`access_token`、`refresh_token`、`client_id`、`client_secret`、`project`、`scope` 与 `expires_at`，电脑关闭后由 ESP32 独立刷新和查询。

SDK 与已批准的独立设备架构保持原状。这里记录的是源码研究结果，不是授权成功或设备运行验证。
