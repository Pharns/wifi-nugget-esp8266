# WiFi Nugget (ESP8266) — Lab Notes

## 🧑‍💻 Project Context
This is my cybersecurity home lab project to build a **Wi-Fi Nugget–style device** using an **ESP8266 NodeMCU**.  

Instead of physical buttons, I’m creating a **web-based control panel** to make it stealthy and easier to expand.

---

## 🛠️ Hardware Setup
- **Board**: NodeMCU ESP8266 (ESP-12E Module)
- **Power/Programming**: Micro-USB cable
- **Optional modules**:
  - OLED SSD1306 (I²C)
  - LoRa SX1276/77/78/79
- **Test network**: Dedicated home lab Wi-Fi AP

---

## 🔌 Wiring Notes
### Current Milestone (no buttons)
- ESP8266 runs AP mode, no external wiring needed.
- Web server served at `http://192.168.4.1`.

### Future wiring (planned):
- **OLED (SSD1306 I²C)**  
  - SDA → D2 (GPIO4)  
  - SCL → D3 (GPIO0)  
  - VCC → 3.3V  
  - GND → GND  
- **LoRa SX1276** (SPI + control pins)  
  - NSS → D0 (GPIO16)  
  - DIO0 (IRQ) → D1 (GPIO5)  
  - SCK → D5 (GPIO14)  
  - MISO → D6 (GPIO12)  
  - MOSI → D7 (GPIO13)  

---

## ⚡ Software Milestones

### ✅ Milestone 1: Basic AP + Web Control + Wi-Fi Scan
- ESP8266 boots into AP mode with SSID `Printer-Setup_24G`.
- Web panel available at `http://192.168.4.1`.
- User can run a **Wi-Fi scan** and see results in the browser.

### 🔜 Milestone 2: Add OLED Display
- Display current mode (Idle / Scanning / Beacon / LoRa).
- Mirror control panel actions.

### 🔜 Milestone 3: LoRa Integration
- Wire SX1276 module.
- Add web panel buttons for **LoRa TX/RX demo**.

### 🔜 Milestone 4: Safe Beacon/Captive Portal Lab Demo
- Implement beacon frames / captive portal for educational red-team testing.

---

## 🧪 Testing Checklist
- [x] Verify ESP8266 board appears in Arduino IDE.  
- [x] Upload basic sketch successfully.  
- [x] Confirm AP appears with correct SSID/password.  
- [x] Connect phone/laptop → open `http://192.168.4.1`.  
- [x] Run Wi-Fi scan → see SSIDs, RSSI, channel, encryption.  
- [ ] Add OLED + verify I²C wiring.  
- [ ] Integrate LoRa module + basic TX demo.  

---

## 📈 Notes & Observations
- Serial monitor at **115200 baud** shows AP startup info and IP address.
- Changing `AP_HIDDEN = true` hides SSID, but you can still connect manually.
- ESP8266 scan results can include hidden SSIDs (shown as `<hidden>`).

---

## 📚 References
- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
- [RadioLib (LoRa)](https://github.com/jgromes/RadioLib)
- [ESP8266WebServer examples](https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/examples)

---

## ⚠️ Ethics & Legal
This project is for **educational purposes only** in a **controlled home lab**.  
Do not use scanning or beacon/captive portal features on networks you do not own or have explicit permission to test.