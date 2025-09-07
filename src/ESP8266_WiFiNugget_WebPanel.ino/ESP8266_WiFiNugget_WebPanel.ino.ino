#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ===== AP CONFIG =====
char AP_SSID[33]   = "Printer-Setup_24G";
char AP_PASS[65]   = "ChangeThisPass!";   // >=8 chars
uint8_t AP_CH      = 6;
bool AP_HIDDEN     = false;

// ===== WEB AUTH =====
const char* ADMIN_USER = "admin";
const char* ADMIN_PASS = "1234";          // change me

// ===== SERVER =====
ESP8266WebServer server(80);

// ===== MODES =====
enum Mode { IDLE, SCAN_ONESHOT, SCAN_CONTINUOUS };
volatile Mode currentMode = IDLE;

// ===== STATION (client) state =====
bool   staEnabled   = false;              // toggle via UI
char   STA_SSID[33] = "";
char   STA_PASS[65] = "";
bool   staConnected = false;
unsigned long staLastAttemptMs = 0;

// ===== SCAN STORAGE =====
String lastScanHTMLRows;
String lastScanCSV;
unsigned long lastScanTs = 0;

// ===== CONTINUOUS SCAN =====
unsigned long scanIntervalMs = 7000;
unsigned long lastScanTick   = 0;

// ---------- Auth helper ----------
bool ensureAuth() {
  if (!server.authenticate(ADMIN_USER, ADMIN_PASS)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// ---------- HTML helpers ----------
String htmlHeader(const String& title) {
  return String(
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu;"
      "max-width:860px;margin:24px auto;padding:0 12px}"
    "a,button,input[type=submit]{display:inline-block;margin:6px 8px 6px 0;"
      "padding:8px 12px;text-decoration:none;border:1px solid #ccc;border-radius:8px}"
    "table{border-collapse:collapse;width:100%}"
    "th,td{border-bottom:1px solid #eee;padding:8px;text-align:left;white-space:nowrap}"
    "input[type=text],input[type=password]{padding:8px;border:1px solid #ccc;border-radius:8px;width:280px;max-width:100%}"
    "label{display:block;margin-top:8px}"
    ".muted{color:#666;font-size:0.9em}"
    ".pill{display:inline-block;padding:2px 8px;border-radius:999px;border:1px solid #ccc;margin-left:8px}"
    ".ok{color:#0a0}.warn{color:#a60}.err{color:#a00}"
    "</style><title>") + title + "</title></head><body>";
}

String footerLine() {
  String apIp  = WiFi.softAPIP().toString();
  String staIp = WiFi.localIP().toString();
  String modeStr = (currentMode==SCAN_CONTINUOUS ? "Continuous Scan" :
                   (currentMode==SCAN_ONESHOT ? "One-shot Scan" : "Idle"));
  return "AP: " + String(AP_SSID) + " (" + apIp + ") • "
         "STA: " + (staEnabled ? (staConnected ? "connected " + staIp : "enabled, connecting…") : "disabled")
         + " • Mode: " + modeStr;
}

String htmlFooter() {
  return "<p class='muted'>" + footerLine() + "</p></body></html>";
}

// ---------- UI pages ----------
String homePage() {
  String s = htmlHeader("Wi-Fi Control Panel");
  s += "<h1>Wi-Fi Control Panel</h1>";

  // Scan controls
  s += "<p><a href='/scan'>🔎 One-shot Scan</a>"
       "<a href='/continuous?on=1'>▶️ Continuous</a>"
       "<a href='/continuous?on=0'>⏸ Stop</a>"
       "<a href='/export.csv'>⬇️ Export CSV</a></p>";

  // Stealth controls
  s += "<h3>Stealth</h3><p>Hidden SSID is <b>" + String(AP_HIDDEN ? "ON" : "OFF") + "</b> "
       "<a href='/stealth?on=1'>Enable</a> "
       "<a href='/stealth?on=0'>Disable</a></p>";

  // Dual interface controls
  s += "<h3>Dual Interface (AP + Station)</h3>"
       "<p>Station is <b>" + String(staEnabled ? "ENABLED" : "DISABLED") + "</b>"
       "<span class='pill " + String(staConnected ? "ok" : "warn") + "'>" +
       (staConnected ? "connected" : (staEnabled ? "connecting…" : "off")) + "</span> "
       "<a href='/station'>Configure</a> "
       "<a href='/station/toggle?on=" + String(staEnabled?0:1) + "'>" +
       (staEnabled ? "Disable" : "Enable") + "</a></p>";

  // Latest scan table
  s += "<h3>Latest Scan</h3>";
  if (lastScanHTMLRows.isEmpty()) {
    s += "<p class='muted'>No scan yet. Click One-shot or start Continuous.</p>";
  } else {
    s += "<table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead><tbody>";
    s += lastScanHTMLRows;
    s += "</tbody></table>";
    s += "<p class='muted'>Updated " + String((int)((millis()-lastScanTs)/1000)) + "s ago</p>";
  }

  s += htmlFooter();
  return s;
}

String stationPage() {
  String s = htmlHeader("Station (Client) Settings");
  s += "<h1>Station (Client) Settings</h1>";
  s += "<p>Status: " + String(staEnabled ? "ENABLED" : "DISABLED") +
       " • " + (staConnected ? "<span class='ok'>Connected</span>" : (staEnabled ? "Connecting…" : "Off")) + "</p>";
  s += "<p>STA IP: " + WiFi.localIP().toString() + "</p>";

  s += "<form method='POST' action='/station'>"
       "<label>SSID<br><input type='text' name='ssid' value='" + String(STA_SSID) + "'></label>"
       "<label>Password<br><input type='password' name='pass' value='" + String(STA_PASS) + "'></label>"
       "<input type='submit' value='Save & Connect'></form>";

  s += "<p><a href='/station/toggle?on=1'>Enable</a> "
       "<a href='/station/toggle?on=0'>Disable</a> "
       "<a href='/station/disconnect'>Disconnect</a> "
       "<a href='/'>Home</a></p>";

  s += htmlFooter();
  return s;
}

// ---------- scanning ----------
void runScanAndStore() {
  int n = WiFi.scanNetworks(false, true);
  String rows, csv;

  if (n <= 0) {
    rows = "<tr><td colspan='4' class='warn'>No networks found</td></tr>";
  } else {
    for (int i=0; i<n; i++) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      int32_t chan = WiFi.channel(i);
      String enc = "UNK";
      switch (WiFi.encryptionType(i)) {
        case ENC_TYPE_WEP:  enc="WEP"; break;
        case ENC_TYPE_TKIP: enc="WPA/TKIP"; break;
        case ENC_TYPE_CCMP: enc="WPA2/CCMP"; break;
        case ENC_TYPE_AUTO: enc="AUTO"; break;
        case ENC_TYPE_NONE: enc="OPEN"; break;
        default: break;
      }

      rows += "<tr><td>" + (ssid.length()? ssid : String("<i>(hidden)</i>")) +
              "</td><td>" + String(rssi) + " dBm</td><td>" + String(chan) +
              "</td><td>" + enc + "</td></tr>\n";

      String safe = ssid; safe.replace(",", " ");
      csv += safe + "," + String(rssi) + "," + String(chan) + "," + enc + "\n";
    }
  }
  WiFi.scanDelete();
  lastScanHTMLRows = rows;
  lastScanCSV      = csv;
  lastScanTs       = millis();
}

// ---------- AP/STA helpers ----------
void startAPOnly() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CH, AP_HIDDEN);
  Serial.printf("AP up: %s  IP: %s  ch%d  hidden=%d\n",
    AP_SSID, WiFi.softAPIP().toString().c_str(), AP_CH, AP_HIDDEN);
}

void startAP_STA() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CH, AP_HIDDEN);
  Serial.printf("AP up: %s  IP: %s  ch%d  hidden=%d\n",
    AP_SSID, WiFi.softAPIP().toString().c_str(), AP_CH, AP_HIDDEN);

  // kick off a non-blocking STA connect if we have creds
  if (strlen(STA_SSID) > 0) {
    WiFi.begin(STA_SSID, STA_PASS);
    staLastAttemptMs = millis();
    Serial.printf("STA: connecting to \"%s\"…\n", STA_SSID);
  }
}

void restartInterfaces() {
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  delay(50);
  if (staEnabled) startAP_STA(); else startAPOnly();
}

// ---------- routes ----------
void handleRoot()               { if (!ensureAuth()) return; server.send(200, "text/html", homePage()); }
void handleScan()               { if (!ensureAuth()) return; currentMode = SCAN_ONESHOT; runScanAndStore(); currentMode = IDLE; server.send(200, "text/html", htmlHeader("Scan Results") + "<h1>Scan Results</h1><p><a href='/scan'>🔁 Rescan</a> <a href='/'>🏠 Home</a> <a href='/export.csv'>⬇️ Export CSV</a></p><table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead><tbody>" + lastScanHTMLRows + "</tbody></table>" + htmlFooter()); }
void handleContinuous()         { if (!ensureAuth()) return; String on = server.hasArg("on") ? server.arg("on") : ""; currentMode = (on=="1") ? SCAN_CONTINUOUS : IDLE; server.sendHeader("Location","/"); server.send(302,"text/plain","redirect"); }
void handleExportCSV()          { if (!ensureAuth()) return; String csv = "SSID,RSSI,Channel,Encryption\n" + lastScanCSV; server.send(200,"text/csv",csv); }
void handleStealth()            { if (!ensureAuth()) return; bool wantHidden = (server.arg("on")=="1"); if (wantHidden!=AP_HIDDEN){ AP_HIDDEN=wantHidden; restartInterfaces(); } server.sendHeader("Location","/"); server.send(302,"text/plain","redirect"); }

void handleStationGet()         { if (!ensureAuth()) return; server.send(200, "text/html", stationPage()); }
void handleStationPost() {
  if (!ensureAuth()) return;
  String s = server.arg("ssid"); String p = server.arg("pass");
  s.toCharArray(STA_SSID, sizeof(STA_SSID));
  p.toCharArray(STA_PASS, sizeof(STA_PASS));
  if (!staEnabled) { staEnabled = true; }
  restartInterfaces();
  server.sendHeader("Location","/station");
  server.send(302,"text/plain","redirect");
}
void handleStationToggle()      { if (!ensureAuth()) return; staEnabled = (server.arg("on")=="1"); restartInterfaces(); server.sendHeader("Location","/station"); server.send(302,"text/plain","redirect"); }
void handleStationDisconnect()  { if (!ensureAuth()) return; WiFi.disconnect(); staConnected=false; server.sendHeader("Location","/station"); server.send(302,"text/plain","redirect"); }

void setup() {
  Serial.begin(115200);
  delay(200);

  // start interfaces (AP only by default)
  startAPOnly();

  // routes
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/continuous", handleContinuous);
  server.on("/export.csv", handleExportCSV);
  server.on("/stealth", handleStealth);

  // station config
  server.on("/station", HTTP_GET,  handleStationGet);
  server.on("/station", HTTP_POST, handleStationPost);
  server.on("/station/toggle", handleStationToggle);
  server.on("/station/disconnect", handleStationDisconnect);

  // 404
  server.onNotFound([](){
    if (!ensureAuth()) return;
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("Web server on http://192.168.4.1");
}

void loop() {
  server.handleClient();

  // continuous scan scheduler
  if (currentMode == SCAN_CONTINUOUS) {
    unsigned long now = millis();
    if (now - lastScanTick >= scanIntervalMs) {
      lastScanTick = now;
      runScanAndStore();
    }
  }

  // STA connection watcher (non-blocking)
  if (staEnabled) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      if (!staConnected) {
        staConnected = true;
        Serial.printf("STA: connected, IP=%s\n", WiFi.localIP().toString().c_str());
      }
    } else {
      if (staConnected) {
        staConnected = false;
        Serial.println("STA: disconnected");
      }
      // if we have creds and not currently connecting, retry every ~10s
      if (strlen(STA_SSID)>0) {
        unsigned long now = millis();
        if (now - staLastAttemptMs > 10000UL) {
          staLastAttemptMs = now;
          WiFi.begin(STA_SSID, STA_PASS);
          Serial.printf("STA: reconnecting to \"%s\"…\n", STA_SSID);
        }
      }
    }
  }
}
