# Live Accounts and Calendar Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development for isolated implementation units; the user has paused all acceptance testing. Write small meaningful fixture checks but leave them unrun.

**Goal:** Run real AI account quota reads and read-only Google Calendar directly on EchoEar, with no always-on desktop service.

**Architecture:** One device cloud worker owns bounded HTTPS and account refresh. Independent provider and calendar parsers populate a mutex-protected live snapshot; UI reads it in the LVGL thread. A one-time local authorization/import tool transfers credentials over privileged USB without printing secrets.

**Tech Stack:** Pinned ESP-IDF v6.1, existing esp_http_client/cJSON/NVS/LVGL; Python stdlib OAuth PKCE and serial import; existing image/font asset pipeline.

- [ ] Define contracts in main/sh_cloud.h, sh_auth.h, sh_http.h: five account IDs; four quota snapshots with actual units/windows; three bounded calendar events; statuses and timestamps.
- [ ] Root: main/sh_auth.c validates/imports per-account JSON and stores it under sh_auth; refresh rotation checks credential revision; main/sh_http.c provides bounded TLS requests tied to network generation.
- [ ] Provider unit: main/sh_ai.c/h parses actual Codex/Claude/Antigravity/Cursor responses and calls fixed trusted endpoints. tools/export_ai_accounts.py reads only the selected local account credential stores, writes mode-600 files, prints no secrets. Document private API dependencies.
- [ ] Calendar unit: main/sh_calendar.c/h reads the earliest three upcoming/ongoing events over Calendar REST; bounded RFC3339/all-day parsing. tools/google_authorize.py performs localhost OAuth with state+PKCE and stores only required credentials in a private file. docs/google-calendar-setup.md gives concrete API/client/browser setup steps.
- [ ] UI unit: main/sh_ui.c/h, tools/layout.json, tools/preview.js and asset generators add calendar/list detail routes and live/unknown/stale quota presentation. Retain original page IDs 0..4 and use IDs 5/6 for calendar/event. Render calendar strings with full basic CJK 14px font. Reuse existing art initially to avoid blocking account implementation.
- [ ] Root: main/sh_cloud.c refreshes configured accounts/calendars, preserves real cached values on errors, rejects late results; wire worker and privileged bounded credential-import console commands through main.c and CMakeLists.
- [ ] Root: tools/import_accounts.py transfers one provider record at a time, does not echo credentials, and uses an explicit acknowledgment; .gitignore excludes all secrets.
- [ ] Generate updated preview/source assets and development firmware with the fixed SDK; update README/version records. No automatic USB page loop, BLE acceptance, browser test suite, or 30-minute run until the user requests acceptance.
