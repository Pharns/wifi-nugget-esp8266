------

## 🔎 Passive Recon Modes

- ✅ **Wi-Fi Scanner** → Lists SSID, RSSI, channel, encryption in-browser.
- ✅ **Channel Hopper / Continuous Scan** → Auto-rescans every few seconds, updates results live.
- **Hidden SSID Detector** → Sniffs for “null SSID” beacons and shows hidden APs.
- **Signal Strength Meter** → Show RSSI in real time (like a Wi-Fi “Geiger counter”).

------

## 📡 Active Lab Modes

⚠️ Educational/home lab only.

- **Beacon Frame Flood** → Broadcast fake SSIDs (to demonstrate spoofing).
- **Captive Portal Demo** → Fake login page for teaching phishing defense.
- **Deauth Simulation** → Send controlled deauth packets (only in a lab network).
- **Probe Response Spoof** → Answer probe requests with custom SSID (fake AP).

------

## 🗄️ Logging & Data Modes

- ✅ **CSV Export Mode** → Serve scan results as downloadable CSV over the web panel.
- **Log to SPIFFS** → Store scan results in ESP8266’s flash, review later.
- **Syslog Mode** → Send scan events to a remote syslog server.
- **MQTT Publish** → Send results to an MQTT broker (common in IoT).

------

## 🖥️ UX / Control Modes

- ✅ **Stealth Mode** → Toggle AP SSID hidden/unhidden from the web panel.
- ✅ **Auth Mode** → Basic Auth login (username/password) for control panel access.
- **Schedule Mode** → Run scans/beacon floods at intervals, then go idle.
- ✅ **Dual Interface Mode** → Connect ESP8266 to your home Wi-Fi as a client while still serving its AP panel.

------

## 📶 Expansion Modes (with hardware add-ons)

- **LoRa TX/RX Mode** → Send/receive long-range packets.
- **OLED Display Mode** → Show current mode/status on screen.
- **Sensor Mode** → Hook up a DHT11/DHT22 (temp/humidity) or PIR sensor → send logs over Wi-Fi.
- **Serial Bridge Mode** → Use the web panel to read/write serial data from another device.

------