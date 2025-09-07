#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ==== Config (change these) ====
const char* AP_SSID   = "Printer-Setup_24G";   // boring/stealthy name recommended
const char* AP_PASS   = "ChangeThisPass!";     // 8+ chars
const uint8_t AP_CH   = 6;                     // 1/6/11 typical in 2.4 GHz
const bool AP_HIDDEN  = false;                 // set true to hide SSID

ESP8266WebServer server(80);

enum Mode { IDLE, SCAN_RUNNING };
volatile Mode currentMode = IDLE;

// ---- tiny HTML helpers ----
String htmlHeader(const String& title) {
  return String(F(
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu;"
    "max-width:720px;margin:24px auto;padding:0 12px}"
    "a,button{display:inline-block;margin:6px 8px 6px 0;padding:8px 12px;"
    "text-decoration:none;border:1px solid #ccc;border-radius:8px}"
    "table{border-collapse:collapse;width:100%}"
    "th,td{border-bottom:1px solid #eee;padding:8px;text-align:left}"
    ".muted{color:#666;font-size:0.9em}"
    "</style><title>"
  )) + title + F("</title></head><body>");
}
String htmlFooter() {
  return F("<p class='muted'>ESP8266 Control • <a href='/'>Home</a></p></body></html>");
}
String homePage() {
  String ip = WiFi.softAPIP().toString();
  String s = htmlHeader(F("ESP8266 Control"));
  s += F("<h1>Wi-Fi Control Panel</h1>"
         "<p><a href='/scan'>🔎 Scan Wi-Fi</a>"
         "<a href='/stop'>⏹ Stop / Idle</a></p>"
         "<h3>Extras</h3>"
         "<p><a href='/beacon'>Beacon (placeholder)</a> "
         "<a href='/lora'>LoRa (placeholder)</a></p>");
  s += "<p class='muted'>AP SSID: " + String(AP_SSID) + " • IP: " + ip + "</p>";
  s += htmlFooter();
  return s;
}

// ---- routes ----
void handleRoot() { server.send(200, "text/html", homePage()); }

void handleStop() {
  currentMode = IDLE;
  server.send(200, "text/plain", "Stopped. Back to idle.");
}

// synchronous scan -> HTML table
void handleScan() {
  currentMode = SCAN_RUNNING;
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  String s = htmlHeader(F("Scan Results"));
  s += F("<h1>Wi-Fi Scan Results</h1>");

  if (n <= 0) {
    s += F("<p>No networks found.</p>");
  } else {
    s += F("<table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead><tbody>");
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      int32_t chan = WiFi.channel(i);

      // Map encryption type to readable text
      uint8_t encType = WiFi.encryptionType(i);
      String enc = "UNK";
      switch (encType) {
        case ENC_TYPE_WEP:  enc = "WEP"; break;
        case ENC_TYPE_TKIP: enc = "WPA/TKIP"; break;
        case ENC_TYPE_CCMP: enc = "WPA2/CCMP"; break;
        case ENC_TYPE_AUTO: enc = "AUTO"; break;
        case ENC_TYPE_NONE: enc = "OPEN"; break;
        default: break;
      }

      s += "<tr><td>" + (ssid.length() ? ssid : String("<i>(hidden)</i>")) +
           "</td><td>" + String(rssi) + " dBm" +
           "</td><td>" + String(chan) +
           "</td><td>" + enc + "</td></tr>";
    }
    s += F("</tbody></table>");
  }

  s += F("<p><a href='/scan'>🔁 Rescan</a> <a href='/'>🏠 Home</a></p>");
  s += htmlFooter();

  WiFi.scanDelete();
  currentMode = IDLE;
  server.send(200, "text/html", s);
}

void handleBeacon() {
  server.send(200, "text/plain",
              "Beacon placeholder. (We can add a safe demo later for lab use.)");
}
void handleLoRa() {
  server.send(200, "text/plain",
              "LoRa placeholder. (Add SX1276 wiring + RadioLib to enable.)");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, AP_CH, AP_HIDDEN);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("AP %s • SSID: %s • IP: %s • ch%d • hidden=%d\n",
                ok ? "started" : "FAILED", AP_SSID, ip.toString().c_str(),
                AP_CH, AP_HIDDEN);

  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/stop", handleStop);
  server.on("/beacon", handleBeacon);
  server.on("/lora", handleLoRa);
  server.begin();
  Serial.println("Web server: http://192.168.4.1");
}

void loop() {
  server.handleClient();
  // background tasks can be added here later
}
