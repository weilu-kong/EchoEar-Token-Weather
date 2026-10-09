# Gemini / Claude 设备授权：当前可执行步骤

目标分开：Gemini取Antigravity Gemini Models组的每周/五小时剩余率；Claude取你自己的Claude订阅账户五小时/每周额度。Claude已完成当前额度的设备真实读取；Gemini/Antigravity仍未完成。

## Claude：当前已接入

用户已完成官方隔离登录，并明确允许导出新会话及导入设备NVS。只读取`.local/claude-device`对应的Keychain授权；输出在`.local/claude-device-account.json`（0600，Git忽略），真实响应与native截图也留在私有目录。没有复制日常桌面会话。

主机额度HTTP200，原生C解析接受five_hour/seven_day；设备直接刷新ESP_OK、valid=1，已看到真实5小时/7天剩余率。当前不需要重新授权。令牌自动刷新已实现，但实际到期后的续期/轮换尚未验证；消费端私有额度接口不等于官方第三方设备API。NVS未加密，用户已获知并批准存入该设备。

以下保留初次授权步骤，供确实需要重登时使用。

## Claude：先完成官方隔离登录

已准备本机私有目录`.local/claude-device`（0700，Git忽略）。本机官方Claude Code的`auth login --help`已确认支持`--claudeai`。在终端执行：

```sh
CLAUDE_CONFIG_DIR='/Users/kongweilu/Development/EchoEar Token&Weather/SHANHAI_EchoEar_Demo/.local/claude-device' /opt/homebrew/bin/claude auth login --claudeai
```

1. 在官方浏览器页面登录你在Claude桌面端/Claude Code使用的同一Claude订阅账户。
2. 核对页面展示的权限，再完成授权。如果浏览器返回登录码，只粘贴回这个官方CLI终端。
3. 看到`Login successful`后告诉Codex“Claude隔离登录完成”；不要把登录码、令牌或凭据文件内容发送到聊天。

[官方多账户配置](https://code.claude.com/docs/en/authentication#log-in-with-multiple-accounts)说明每个配置目录有自己的claude.ai登录。此命令只为此次实验创建官方客户端登录，避免直接复制你日常桌面会话；不使用`setup-token`，其推理授权不等于额度查询所需的权限。

**官方客户端登录本身不等于设备授权。** 本轮经过用户明确批准，已导出隔离新会话并成功导入、验证当前额度读取；刷新可行性还需到期后验证。官方[凭据使用边界](https://code.claude.com/docs/en/legal-and-compliance#authentication-and-credential-use)限制第三方登录和凭据代管；不能把本项目的私有额度接口描述为官方支持的设备API。已有Google/Codex/Cursor不需要重新授权。

## Gemini：Antigravity设备授权仍缺条件

截图中的Gemini额度来自Antigravity账户。现有Antigravity桌面登录可以继续使用，但没有已验证的独立设备授权命令。目前已找到`retrieveUserQuota`的静态接口字段，尚未闭合个人OAuth客户端/回调/scope/刷新/project及Gemini每周/五小时bucket映射。

现在不需要申请Gemini API key，不需要重做Google Calendar登录，也不要复制Calendar的refresh token。两者不能提供Antigravity订阅额度。等上述路径验证完成，再提供一次性浏览器授权步骤；不会让电脑充当常驻额度桥接。
