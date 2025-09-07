# ESP8266 WiFi Nugget (Web-Controlled)

A **Wi-Fi Nugget–style toolkit** built on an ESP8266 NodeMCU for my cybersecurity home lab.  
This project replaces the Nugget’s physical buttons with a **web-based control panel**, letting me control Wi-Fi scanning and future payloads directly from a browser.

⚠️ **Disclaimer**: This project is for **educational use only** inside my home lab.  
Do not use on networks you don’t own or have explicit permission to test.

---

## 🎯 Project Goals
- Learn how to build embedded Wi-Fi tools with ESP8266.
- Replace physical buttons with a **stealthy AP + web control panel**.
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
1. Install **Arduino IDE**  
2. Add ESP8266 board support:  
   - File → Preferences → Additional Boards URL:  
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
   - Tools → Board → Boards Manager → install **ESP8266 by ESP8266 Community**  
3. Select **NodeMCU 1.0 (ESP-12E Module)**  
4. Copy the code from `src/ESP8266_WiFiNugget_WebPanel.ino`  
5. Upload & open Serial Monitor → connect to AP → browse `http://192.168.4.1`

---

## 🚀 Features (Milestone 1)
- Creates an **AP named `Printer-Setup_24G`** (default password: `ChangeThisPass!`)  
- Serves a **web-based control panel**  
- Can **scan nearby Wi-Fi networks** and display results in the browser  
- Placeholders for:
  - Beacon spam lab demo
  - LoRa module integration
  - OLED status display

---

## 🧪 Testing
1. Flash ESP8266 with the sketch  
2. Connect to AP: `Printer-Setup_24G`  
3. Open [http://192.168.4.1](http://192.168.4.1)  
4. Run **Scan Wi-Fi** → see SSIDs, RSSI, channel, encryption  

---

## 📈 Roadmap
- [x] Milestone 1: Basic AP + Web Control + Wi-Fi Scan  
- [ ] Milestone 2: Add OLED display for status/mode  
- [ ] Milestone 3: Add LoRa SX1276 TX/RX integration  
- [ ] Milestone 4: Safe beacon/captive portal demo for lab use  

---

## 📜 License
MIT License (see [LICENSE](LICENSE))