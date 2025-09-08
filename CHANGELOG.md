## [0.2.0] - 2025-09-07
### Added
- Dual Interface mode (AP / STA / AP+STA) with safe fallback.
- Persistence via LittleFS (`/config.json`).
- Basic Auth login for web UI.
- Show/Hide password toggles in Network page.
- CSV export, continuous scan, stealth toggle.
- Reboot and Factory Reset endpoints.
- Improved HTML/CSS layout and status footer.

[0.2.0]: https://github.com/pharns/wifi-nugget-esp8266/releases/tag/v0.2.0

## [v0.2.0] — 2025-09-07

### Added
- **Bridge mode (NAPT)**: optional NAT from STA to AP clients.
- **Captive Portal**: DNS redirect on AP with configurable allowlist; serves `/portal.html` from LittleFS if present.
- **Deauth Detection**: promiscuous sniffer flags deauth/disassoc frames; exposed via `status.json` and UI chip.
- **Deauth Simulation**: educational/logging-only flow with targets list and “Run Simulation” action.
- **Live Status UI**: chips for Mode/AP/STA/Bridge/Scan/Portal/DeauthSim/Detect/Schedule; auto-refresh via `/status.json`.
- **Show/Hide Passwords**: buttons for AP and STA password inputs.
- **Explicit AP IP**: SoftAP set to `192.168.4.1` via `WiFi.softAPConfig()`.

### Changed
- **Tools & Network pages** refined with clearer controls, safe defaults, and inline help.
- Continuous scan cadence stabilized; CSV export and rolling log (`/scan_log.csv`) unchanged.

### Fixed
- STA-only **safety fallback** to AP-only after 60s if not connected.
- Route/handler closures and UI compile-order issues.

### Endpoints & Files
- `GET /status.json`, `GET /scan`, `GET /continuous?on=1|0`, `GET /export.csv`, `GET /scan_log.csv`
- `GET/POST /network`, `GET/POST /tools`
- `GET /stealth?on=1|0`, `GET /station/disconnect`, `GET /reboot`, `GET /factory-reset`
- Optional: `LittleFS:/portal.html` for custom captive portal page.

