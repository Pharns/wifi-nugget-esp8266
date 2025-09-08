# ESP8266 WiFi Nugget (Web-Controlled)

A **Wi-Fi Nugget–style toolkit** built on an ESP8266 NodeMCU for my cybersecurity home lab.  
This project replaces the Nugget’s physical buttons with a **web-based control panel**, letting me control Wi-Fi scanning and payloads directly from a browser.  

Now with **Dual Interface Mode + Persistent Config**, the device can operate as an AP, a Station on my LAN, or both — while remembering settings across reboots.  

⚠️ **Disclaimer**: This project is for **educational use only** inside my home lab.  
Do not use on networks you don’t own or have explicit permission to test.

---

## 🎯 Project Goals
- Learn how to build embedded Wi-Fi tools with ESP8266.  
- Replace physical buttons with a **stealthy AP + web control panel**.  
- Support **AP-only**, **Station-only**, and **AP+Station** modes.  
- Save network configuration persistently using **LittleFS**.  
- Extend with optional **OLED display** and **LoRa module**.  
- Document everything as part of my **cybersecurity portfolio**.  

---

## 🛠️ Hardware
- NodeMCU ESP8266 (ESP-12E Module)  
- Micro-USB cable (power + programming)  
- *(Optional)* SSD1306 OLED (I²C)  
- *(Optional)* SX1276/77/78/79 LoRa module  

---

## 🧰 Software Setup
1. Install **Arduino IDE**.  
2. Add ESP8266 board support:  
   - File → Preferences → Additional Boards URL:  
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
   - Tools → Board → Boards Manager → install **ESP8266 by ESP8266 Community**  
3. Select **NodeMCU 1.0 (ESP-12E Module)**.  
4. Copy the code from `src/ESP8266_WiFiNugget_WebPanel.ino`.  
5. Upload & open Serial Monitor → connect to AP → browse `http://192.168.4.1`.  

---

## 🚀 Features
### ✅ Milestone 1
- Creates an **AP named `Printer-Setup_24G`** (default password: `ChangeThisPass!`).  
- Serves a **web-based control panel**.  
- Can **scan nearby Wi-Fi networks** and display results in the browser.  

### ✅ Milestone 2
- **Dual Interface Mode**: AP-only, Station-only, or AP+Station.  
- **Persistent Config**: saves AP/STA SSIDs, passwords, hidden flag, and mode in `/config.json` (LittleFS).  
- **Safe Fallback**: if STA-only can’t connect within 60s → auto-reverts to AP-only.  
- **Basic Auth Login** for web panel.  
- **Web UI Enhancements**:  
  - Show/Hide password toggles  
  - CSV export of scan results  
  - Continuous scan (channel hopper)  
  - Stealth toggle for AP SSID  
  - Reboot & Factory Reset endpoints  
  - Cleaner HTML/CSS with status footer showing AP IP, STA IP, and mode  

---

## 🧪 Testing
1. Flash ESP8266 with the sketch.  
2. Connect to AP: `Printer-Setup_24G`.  
3. Open [http://192.168.4.1](http://192.168.4.1).  
4. Run **Scan Wi-Fi** → see SSIDs, RSSI, channel, encryption.  
5. Switch to **AP+Station**, configure STA credentials → confirm dual IP access.  
6. Test persistence: reboot → device reloads saved settings.  

---

## Roadmap

- [x] **Milestone 1 — AP + Web Control + Wi-Fi Scan**  
  ESP8266 boots as an AP, serves a control panel at `http://192.168.4.1`, and can scan nearby Wi-Fi networks.

- [x] **Milestone 2 — Dual Interface + Persistence**  
  AP / STA / AP+STA selector with safe fallback, persistent config (LittleFS), Basic Auth, show/hide password toggles, CSV export, continuous scan, stealth toggle, reboot & factory reset endpoints, explicit AP IP.

- [x] **Milestone 3 — Bridge + Captive Portal + Deauth (Sim/Detect) + UI Polish**  
  NAPT **Bridge mode** (internet to AP clients), **Captive Portal** (DNS redirect with allowlist), **Deauth Simulation** (lab-only logging), **Deauth Detection** (promiscuous sniff), live status **chips** with auto-refresh, refined **Tools** & **Network** pages, and password **show/hide** controls.

- [ ] **Milestone 4 — LoRa Integration**  
  Wire SX1276 module; add web controls for LoRa TX/RX demo and status.

- [ ] **Milestone 5 — Beacon/Captive Portal Demo (Education)**  
  Classroom-safe beacon/captive portal demonstration with clear warnings and toggles.


---

## 📜 License
MIT License (see [LICENSE](LICENSE))