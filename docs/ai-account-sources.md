# AI account sources and device requests

The ESP32 performs the HTTPS requests itself after credentials are provisioned. A
computer is used only once to export and import credentials; no desktop bridge,
app-server process or local runtime is required during device operation. Exporting
credentials does not query provider accounts. Tests, live account calls, flashing and hardware acceptance remain paused by the user; native development compilation has completed; nothing here is a
claim that live integration has passed.

## Direct requests and meaning

| Provider | Fixed read-only request | Normalized fields |
| --- | --- | --- |
| Codex | `GET https://chatgpt.com/backend-api/wham/usage` | `rate_limit.primary_window` and `secondary_window`: `used_percent`, `limit_window_seconds`, `reset_at`; `credits.balance` stays credits |
| Claude Code | `GET https://api.anthropic.com/api/oauth/usage` | `five_hour` and `seven_day`: `utilization` is used percentage, `resets_at` is an ISO timestamp |
| Cursor | `POST https://api2.cursor.sh/aiserver.v1.DashboardService/GetCurrentPeriodUsage`, body `{}` | `planUsage.limit` and `includedSpend` are cents; included-spend balance and total are USD; `billingCycleEnd` is Unix milliseconds (converted to seconds on the device) |
| Antigravity | `POST https://daily-cloudcode-pa.googleapis.com/v1internal:fetchAvailableModels`, body `{"project":"<provider cloud project>"}` | `models.*.quotaInfo.remainingFraction` is remaining fraction; `resetTime` is an ISO timestamp |

Bearer credentials come from the matching provider login. Codex also sends the
selected `ChatGPT-Account-Id` when present. Claude uses
`anthropic-beta: oauth-2025-04-20`. Cursor uses Connect JSON unary framing with
`Connect-Protocol-Version: 1` and `x-cursor-client-type: ide`.

These HTTPS interfaces are private client contracts. Their existence and field
schemas were inspected in the installed official clients on 2026-10-08, without
launching them, querying accounts, reading chat databases or reading credentials.
They may change or reject another client/device. Cloudflare challenges, provider
access policy and device TLS behavior still require future live validation.

The parsers reject missing/malformed quota fields and error envelopes. Failures
leave the previous snapshot untouched so the cloud worker can mark it stale or
show authentication/error state. HTTP 401/403 mean authentication required;
429 means rate limited. No token totals are inferred from percentages. A missing
window or credit field is unavailable, not zero usage. Over-limit usage clamps
remaining percentage to zero. An `unlimited` credit flag describes credit funding;
it does not erase Codex subscription windows.

Codex additional metered buckets are not combined into the Codex bucket. Claude
model-specific windows and additional-usage spend are not presented as ordinary
subscription credits. Cursor follows its client's included-spend calculation:
`max(0, limit - includedSpend) / 100`, excluding bonus/overage spend. A zero Cursor
limit does not prove unlimited usage. Antigravity's service ring means **the least
remaining quota among returned models**, with that model's reset time; it is not
a token balance or the sum of the models. Antigravity returns no window duration,
so none is invented. The current four-service view cannot show all named buckets.

## Separate Codex device authorization

`tools/codex_authorize.py` creates a new browser authorization-code exchange for
the device instead of copying the desktop refresh token. It is not started
automatically. After reviewing the permissions, the user can run:

```sh
python3 tools/codex_authorize.py
```

**The requested grant is not limited to reading quota.** The current official CLI
scope includes `api.connectors.read` and `api.connectors.invoke`, in addition to
OpenID/profile/email and offline access. The firmware only makes quota requests,
but that does not narrow the OAuth grant. The current official implementation
hard-codes this scope; no supported narrower scope was established, so the tool
retains it. Review the browser consent before authorizing.
[Official authorization parameters](https://github.com/openai/codex/blob/main/codex-rs/login/src/server.rs).

The tool binds only `127.0.0.1`, using registered callback ports 1455 or 1457 and
`/auth/callback`. It does not cancel another application's login listener. It
checks callback Host, state, duplicate query fields and the authorization code,
then closes the temporary server. The callback wait expires after five minutes.
Authorization goes to `https://auth.openai.com/oauth/authorize`; the token POST
goes to `https://auth.openai.com/oauth/token` with form encoding. Token POSTs are
not retried and redirects are rejected.
[Official callback and exchange implementation](https://github.com/openai/codex/blob/main/codex-rs/login/src/server.rs),
[official grant encoding](https://github.com/openai/codex/blob/main/codex-rs/login/src/oauth/client.rs).

PKCE uses a new 64-byte verifier and SHA-256 challenge; state uses 32 random
bytes. Both are encoded as base64url without padding.
[Official PKCE generator](https://github.com/openai/codex/blob/main/codex-rs/login/src/oauth/pkce.rs),
[official state generator](https://github.com/openai/codex/blob/main/codex-rs/login/src/oauth/authorization.rs).
The public client ID comes from the
[official auth manager](https://github.com/openai/codex/blob/main/codex-rs/login/src/auth/manager.rs).

The newly returned tokens are atomically saved as one `sh_auth_import` record in
`.local/codex-account.json` (`0600` file, `0700` directory), including selected
account ID and access-token expiry. The ID token is used only for metadata and
is not saved. Metadata decoding follows the
[official token parser](https://github.com/openai/codex/blob/main/codex-rs/login/src/token_data.rs);
JWT signatures are not locally verified, so only the authenticated token
endpoint's response supplies these values. Tokens, state, callback URLs and raw
provider errors are never printed. No existing credential store or desktop
session is read or modified; the tool does not set or use `HOME` or `CODEX_HOME`.
Import the resulting file separately using `tools/import_accounts.py --file` and
the device's USB port. The tool performs no quota request or device import.

`fixture_checks()` contains synthetic PKCE, callback and record checks but has
not been executed. Browser login, independent refresh-token behavior, provider
permission acceptance and device quota access remain unvalidated. Accounts
requiring FedRAMP routing are rejected because the current device adapter has a
fixed ordinary ChatGPT endpoint.

## Separate Cursor device session

`tools/cursor_authorize.py` implements the installed official client's new login
flow for the user account and its optional selected team. When
authorization is desired, run:

```sh
python3 tools/cursor_authorize.py
```

The script generates a fresh 32-byte base64url verifier and UUID. Its browser URL
is `https://cursor.com/loginDeepControl` with the SHA-256 challenge, `uuid`,
`mode=login` and `supportsSelectedTeamLogin=true`. The script polls
`GET https://api2.cursor.sh/auth/poll?uuid=...&verifier=...` every 500 ms, for at
most five minutes; 404 means pending. No local callback listener is needed.
The official client also handles a `cursor://` deep link, but polling completes
the same pending browser session. **If the browser offers Open Cursor, decline
it** so the new session is not handed to the existing desktop application.

The 200 response uses `accessToken` and `refreshToken`. The script saves only its
fresh response in `.local/cursor-account.json`, as one firmware import record;
the file is `0600` and its `.local` directory `0700`. It extracts expiry and
account ID from the returned access token's `exp` and `sub`. JWT metadata is
decoded from the HTTPS polling response without local signature verification.
It reads no existing account database or credential file. It does not change
desktop storage or automatically import to the device.

**This is a full Cursor session, not a quota-only OAuth grant.** The installed
login flow provides no selectable `scope` parameter, so the script does not
invent a restricted permission set. Review the browser login before proceeding.
The optional `selectedTeamId` is preserved as a canonical `team_id` string.
Finite integral numeric IDs are limited to the client's int32 range; decimal
strings are normalized, and bounded ASCII IDs are preserved. Null, empty, and
zero values supply no valid selected-team ID and are omitted; booleans,
fractions, negatives, non-finite numbers, whitespace and control characters are
rejected. The client itself checks presence with `!== undefined`, then validates
the selected ID against refreshed team membership. It provides no evidence that
zero or empty strings identify a real team, so these must not produce a TEAM
badge.

The device uses the same
`aiserver.v1.DashboardService/GetCurrentPeriodUsage` endpoint and empty request
body for both personal and team-selected sessions. When `team_id` exists it sends
`x-cursor-team-id`, matching the installed extension's
`TransportFactory.applyAuthorization` and `addTeamIdHeader`. The workbench's
`UsageDataService` sends an empty `GetCurrentPeriodUsageRequest`; the common
transport adds the selected team alongside Bearer authorization. There is no
switch to a separate team-usage service and no fallback to an unscoped request
when a selected-team request fails.

`include_pooled_usage` is an optional request field. The installed official
extension's RPC documentation explicitly distinguishes the default per-user
reading from an Enterprise pooled reading. This adapter leaves it unset, as the
workbench does. `TEAM` therefore means **this user's included usage in the
selected team**, not the team's total remaining budget. The response has the same
`planUsage` schema and the same included-spend calculation. It does not add pooled
or on-demand spend from `spendLimitUsage`. If the expected plan fields are absent,
the adapter reports unavailable data rather than substituting pooled totals.
[Official Teams pricing](https://prod.cursor.com/docs/account/teams/pricing)
distinguishes per-seat included usage from Enterprise pooled usage;
[official Enterprise pooled-usage documentation](https://prod.cursor.com/docs/enterprise/pooled-usage)
describes the latter separately.

Endpoint, challenge, polling interval, response and refresh evidence comes from
the installed official
`/Applications/Cursor.app/Contents/Resources/app/out/vs/workbench/workbench.desktop.main.js`:
`loginLink`, `getLoginUrl`, `getPollingEndpoint`, `fetchPendingBrowserLoginSession`,
`base64URLEncode`, `sha256`, `getAuthIdFromToken`, production `cursorCreds`, and
`_performAccessTokenRefresh`. Team-header and per-user/pooled semantics additionally
come from the official installed
`/Applications/Cursor.app/Contents/Resources/app/extensions/cursor-always-local/dist/main.js`,
`TransportFactory` and its `GetCurrentPeriodUsage` RPC field documentation.
These are private client contracts, not a public
third-party OAuth registration API. This independent client and its session
isolation still require real authorization validation.
[Official Cursor browser-login overview](https://cursor.com/docs/sdk/typescript)
describes the SDK's browser login, but that public SDK flow mints a user API key;
it is not evidence that its key works with the device's private billing RPC.
[Official sign-in-domain notice](https://cursor.com/help/troubleshooting/sign-in-domains)
documents current identity-provider migration while Cursor's own application and
API hosts remain unchanged.

The script suppresses callback/polling URLs, verifier, tokens, IDs and raw provider
errors in terminal output, rejects redirects, and treats 403/429 as explicit
errors. It requests privacy-mode polling headers and sends no machine identifiers
or telemetry. `fixture_checks()` contains synthetic PKCE, token, selected-team
normalization and invalid-ID checks; it has not been run. The first actual login
was rejected by the earlier personal-only tool because a team was selected; its
tokens were not saved. The team-support change did not initiate another login,
request provider quota data or run hardware checks.

## Claude Code independent-login support boundary

A supported third-party Claude.ai login and quota authorization flow was not
established. The official
[authentication and credential-use documentation](https://code.claude.com/docs/en/legal-and-compliance#authentication-and-credential-use)
describes subscription OAuth as intended for Claude Code and native Anthropic
applications, says third-party developers cannot offer Claude.ai login in their
own applications or collect/store/intermediate those credentials, and distinguishes
sign-in to the unmodified Claude Code binary. These are the provider's published
support constraints; the existence of an internal endpoint does not establish
support for an EchoEar authorization tool.

The technical flow was also inspected in the installed official Claude Code
2.1.286 binary. `x9n` uses `https://claude.com/cai/oauth/authorize`, public client
ID `9d1c250a-e61b-44d9-88ed-5944d1962f5e`, a dynamic loopback listener on
`127.0.0.1` with redirect URI `http://localhost:<port>/callback`, a random
32-byte PKCE verifier/state and SHA-256 challenge. `ajr` sends a JSON
authorization-code exchange, including state, to
`https://platform.claude.com/v1/oauth/token`. Normal `L5r` requests
`org:create_api_key user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload user:plugins`.
These permissions include inference and API-key creation; they are not quota-only.
The inference-only path lacks the profile scope needed by subscription usage.
[Official authentication guidance](https://code.claude.com/docs/en/authentication)
documents the native application's browser and local callback/manual-code login.

No `claude_authorize.py` was added and no Claude login was initiated. The existing
quota adapter and explicit credential importer remain; no desktop credential was
automatically read, exported or imported. A public Claude subscription quota API
and a supported reduced-scope independent device login remain unavailable in the
sources inspected; an API key is not a substitute for subscription quota.

## Export only selected credentials

The exporter is not run automatically. When provisioning is authorized, the user
can run, from the project directory:

```sh
python3 tools/export_ai_accounts.py codex claude cursor
```

Only the named providers are read. Output defaults to `.local/ai-accounts.json`,
containing `{"accounts":[{"provider":"...", ...}]}`. It is atomically written
with mode `0600`; the `.local` directory is created with `0700` when absent.
Optional fields are `access_token`, `refresh_token`, `client_id`, `client_secret`,
`account_id`, `project`, `scope`, and Unix-seconds `expires_at`. Console output never prints
tokens, account identifiers, request headers or raw provider/store errors. Keep
the file private and use the separate USB credential importer for provisioning.

| Provider | Selected store | Boundaries and limitations |
| --- | --- | --- |
| Codex | `$CODEX_HOME/auth.json`, default `~/.codex/auth.json`; override `--codex-auth` | Reads only `tokens`; API-key-only login is unsupported for subscription quotas. Keychain-only or newer auth-store formats may require an explicit exported OAuth file. |
| Claude | macOS Keychain service `Claude Code-credentials`, selected user account | `security` runs only when the user executes the exporter; stdout and stderr are captured. Custom config-dir Keychain suffix follows the client's SHA-256 convention. `--claude-credentials` accepts the selected `.credentials.json` format. Only `claudeAiOauth` is copied; unfamiliar store versions fail. |
| Cursor | `~/Library/Application Support/Cursor/User/globalStorage/state.vscdb`; override `--cursor-db` | Opens SQLite with `mode=ro`; queries only `cursorAuth/accessToken` and `cursorAuth/refreshToken`, never chats or unrelated records. |
| Antigravity | Explicit `--antigravity-credentials` user OAuth JSON | Native automatic export is unsupported for the installed version because its persistence codec and OAuth-client selection were not established. Require provider-specific `client_id`, `client_secret`, `project` and access/refresh token; do not substitute a Calendar login. |

Antigravity's installed app declares its core storage at
`~/.gemini/antigravity` and settings at `~/.gemini/config/config.json`; its
language server embeds `OAuthTokenInfo` protobuf fields `access_token`,
`refresh_token`, `expiry`. Those facts do not establish which file or codec holds
the active account. The exporter does not recursively scan those directories,
decode arbitrary chat/state files, or call a running language server to obtain
tokens. A selected Antigravity credential JSON must originate from the matching
Google OAuth grant with the Cloud Code scopes and its validated provider project.
Its automatic extraction remains an explicit limitation.

The file contains a copy of the selected desktop grant; exporting does not create
an independent device session. Refreshing a copied token can conflict with token
rotation in the desktop client. Prefer separately authorized device sessions for
ongoing use. The exporter neither writes credentials to the device nor refreshes
the selected desktop session.

## Refresh evidence

- Codex: `POST https://auth.openai.com/oauth/token`, JSON refresh grant with
  `client_id: app_EMoamEEZ73f0CkXaXp7hrann` and `refresh_token`.
- Claude 2.1.286: `POST https://platform.claude.com/v1/oauth/token`, JSON refresh
  grant with `client_id: 9d1c250a-e61b-44d9-88ed-5944d1962f5e`. Its client sends
  `scope: user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload user:plugins`.
  Preserve a rotated refresh token when returned.
- Cursor: `POST https://api2.cursor.sh/oauth/token`, JSON refresh grant with
  `client_id: KbZUR41cY7W6zRSdpSUJ7I7mLYBKOCmB`, header
  `x-cursor-client-type: ide`. Installed `_performAccessTokenRefresh` stores the
  returned `access_token` as both access and refresh token. A `shouldLogout` reply
  requires reauthentication.
- Antigravity: Google token endpoint `https://oauth2.googleapis.com/token`
  exists in the installed language server, but multiple OAuth client IDs are
  embedded. Only the selected grant's client ID/secret may be used. The native
  client's refresh parameters were not sufficiently established to hard-code.

## Primary evidence

- [Official Codex app-server documentation](https://learn.chatgpt.com/docs/app-server)
  defines `usedPercent`, `windowDurationMins`, `resetsAt` and multi-bucket limits.
  App-server runs on a desktop; firmware requests the upstream endpoint directly.
- [Official Codex backend client](https://github.com/openai/codex/blob/main/codex-rs/backend-client/src/client.rs)
  defines `/wham/usage`, account headers, snake-case window mapping and credits.
- [Official Codex OAuth implementation](https://github.com/openai/codex/blob/main/codex-rs/login/src/auth/manager.rs)
  defines refresh endpoint, JSON encoding and public client ID.
- [Official Claude costs documentation](https://code.claude.com/docs/en/costs)
  describes subscription plan bars, `/usage` and last-known data when usage fails.
- Installed official Claude binary:
  `/opt/homebrew/Caskroom/claude-code/2.1.286/claude`, `pF`, `Pde`, `yJe`, `QN`.
- Installed official Cursor client:
  `/Applications/Cursor.app/Contents/Resources/app/out/vs/workbench/workbench.desktop.main.js`,
  `DashboardService`, `GetCurrentPeriodUsageResponse`, `UsageDataService`,
  `_performAccessTokenRefresh` and production `cursorCreds` defaults.
- Installed official Antigravity client:
  `/Applications/Antigravity.app/Contents/Resources/app.asar` sets
  `--cloud_code_endpoint`; `bin/language_server` embeds the
  `FetchAvailableModelsRequest/Response`, `QuotaInfo` protobuf descriptors and
  HTTP POST `/v1internal:fetchAvailableModels` binding.

`tests/ai_account_test.c` contains synthetic parser fixtures and invalid-body
checks. It was written but **not run**, in accordance with the testing pause.
