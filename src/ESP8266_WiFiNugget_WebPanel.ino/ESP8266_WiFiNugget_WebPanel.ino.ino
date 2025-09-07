#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ====== CONFIG & DEFAULTS ======
#define CFG_PATH "/config.json"

// NetMode values
enum NetMode : uint8_t { NM_AP_ONLY=0, NM_STA_ONLY=1, NM_AP_STA=2 };
// App Mode values
enum AppMode : uint8_t { IDLE=0, SCAN_ONESHOT=1, SCAN_CONTINUOUS=2 };

// persisted settings
struct Config {
  char ap_ssid[33]   = "Printer-Setup_24G";
  char ap_pass[65]   = "ChangeThisPass!";
  uint8_t ap_ch      = 6;
  bool ap_hidden     = false;

  char sta_ssid[33]  = "";
  char sta_pass[65]  = "";

  uint8_t net_mode   = NM_AP_ONLY; // AP default
} cfg;

// ===== WEB AUTH (UI login) =====
const char* ADMIN_USER = "admin";
const char* ADMIN_PASS = "1234";  // change me

// ===== SERVER =====
ESP8266WebServer server(80);

// ===== RUNTIME =====
volatile AppMode currentMode = IDLE;
bool   staConnected = false;
unsigned long lastScanTick = 0;
unsigned long lastScanTs   = 0;
unsigned long staLastAttempt = 0;
unsigned long staOnlyStartMs = 0;

const unsigned long SCAN_INTERVAL_MS = 7000;
const unsigned long STA_ONLY_TIMEOUT = 60000UL;

String lastRows, lastCSV;

// ===== Helpers =====
bool ensureAuth() {
  if (!server.authenticate(ADMIN_USER, ADMIN_PASS)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

bool saveConfig() {
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  StaticJsonDocument<512> d;
  d["ap_ssid"]  = cfg.ap_ssid;
  d["ap_pass"]  = cfg.ap_pass;
  d["ap_ch"]    = cfg.ap_ch;
  d["ap_hidden"]= cfg.ap_hidden;
  d["sta_ssid"] = cfg.sta_ssid;
  d["sta_pass"] = cfg.sta_pass;
  d["net_mode"] = cfg.net_mode;
  bool ok = (serializeJson(d, f) > 0);
  f.close();
  return ok;
}

bool loadConfig() {
  if (!LittleFS.exists(CFG_PATH)) return false;
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;
  StaticJsonDocument<512> d;
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e) return false;

  strlcpy(cfg.ap_ssid,  d["ap_ssid"]  | cfg.ap_ssid,  sizeof(cfg.ap_ssid));
  strlcpy(cfg.ap_pass,  d["ap_pass"]  | cfg.ap_pass,  sizeof(cfg.ap_pass));
  cfg.ap_ch     = d["ap_ch"]    | cfg.ap_ch;
  cfg.ap_hidden = d["ap_hidden"]| cfg.ap_hidden;

  strlcpy(cfg.sta_ssid, d["sta_ssid"] | cfg.sta_ssid, sizeof(cfg.sta_ssid));
  strlcpy(cfg.sta_pass, d["sta_pass"] | cfg.sta_pass, sizeof(cfg.sta_pass));
  cfg.net_mode  = d["net_mode"] | cfg.net_mode;

  return true;
}

void factoryReset() {
  LittleFS.remove(CFG_PATH);
}

// ===== HTML helpers (with Show/Hide password JS) =====
String htmlHeader(const String& title){
  return String(
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu;max-width:920px;margin:24px auto;padding:0 12px}"
    "a,button,input[type=submit]{display:inline-block;margin:6px 8px 6px 0;padding:8px 12px;text-decoration:none;border:1px solid #ccc;border-radius:8px}"
    "table{border-collapse:collapse;width:100%}th,td{border-bottom:1px solid #eee;padding:8px;text-align:left;white-space:nowrap}"
    "input[type=text],input[type=password],input[type=number]{padding:8px;border:1px solid #ccc;border-radius:8px;width:280px;max-width:100%}"
    "label{display:block;margin-top:8px}"
    ".muted{color:#666;font-size:0.9em}.pill{display:inline-block;padding:2px 8px;border-radius:999px;border:1px solid #ccc;margin-left:8px}"
    ".ok{color:#0a0}.warn{color:#a60}.err{color:#a00}"
    ".pw-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.pw-row small{color:#666}"
    "</style>"
    "<script>"
      "function togglePw(id,btn){var el=document.getElementById(id);if(!el)return;"
      "if(el.type==='password'){el.type='text'; if(btn) btn.textContent='Hide';}"
      "else{el.type='password'; if(btn) btn.textContent='Show';}}"
    "</script>"
    "<title>") + title + "</title></head><body>";
}

String footerLine(){
  String apIp = WiFi.softAPIP().toString();
  String staIp = WiFi.localIP().toString();
  String nm = (cfg.net_mode==NM_AP_ONLY? "AP" : (cfg.net_mode==NM_STA_ONLY? "STA":"AP+STA"));
  String modeStr = (currentMode==SCAN_CONTINUOUS? "Continuous Scan" : (currentMode==SCAN_ONESHOT? "One-shot Scan" : "Idle"));
  return "NetMode: " + nm + " • AP: " + String(cfg.ap_ssid) + " (" + apIp + ") • "
         "STA: " + (staConnected ? "connected " + staIp : "not connected")
         + " • Mode: " + modeStr;
}
String htmlFooter(){ return "<p class='muted'>" + footerLine() + "</p></body></html>"; }

String homePage(){
  String s = htmlHeader("Wi-Fi Control Panel");
  s += "<h1>Wi-Fi Control Panel</h1>";
  s += "<p><a href='/scan'>🔎 One-shot Scan</a>"
       "<a href='/continuous?on=1'>▶️ Continuous</a>"
       "<a href='/continuous?on=0'>⏸ Stop</a>"
       "<a href='/export.csv'>⬇️ Export CSV</a></p>";
  s += "<h3>Stealth</h3><p>Hidden SSID is <b>" + String(cfg.ap_hidden?"ON":"OFF") + "</b> "
       "<a href='/stealth?on=1'>Enable</a> <a href='/stealth?on=0'>Disable</a></p>";
  s += "<h3>Network Mode</h3><p><a href='/network'>Configure AP / STA / AP+STA</a></p>";
  s += "<h3>Maintenance</h3><p><a href='/reboot'>Reboot</a> "
       "<a href='/factory-reset' onclick='return confirm(\"Factory reset? This erases saved Wi-Fi and mode settings.\")'>Factory Reset</a></p>";
  s += "<h3>Latest Scan</h3>";
  if (lastRows.isEmpty()) s += "<p class='muted'>No scan yet.</p>";
  else {
    s += "<table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead><tbody>";
    s += lastRows + "</tbody></table>";
    s += "<p class='muted'>Updated " + String((int)((millis()-lastScanTs)/1000)) + "s ago</p>";
  }
  s += htmlFooter();
  return s;
}

String networkPage(){
  String s = htmlHeader("Network Mode");
  String apChecked  = (cfg.net_mode==NM_AP_ONLY) ? "checked":"";
  String staChecked = (cfg.net_mode==NM_STA_ONLY)? "checked":"";
  String apsChecked = (cfg.net_mode==NM_AP_STA)  ? "checked":"";
  s += "<h1>Network Mode</h1>"
       "<form method='POST' action='/network'>"
       "<p><label><input type='radio' name='mode' value='ap' "+apChecked+"> AP only</label><br>"
       "<label><input type='radio' name='mode' value='sta' "+staChecked+"> Station only</label><br>"
       "<label><input type='radio' name='mode' value='apsta' "+apsChecked+"> AP + Station</label></p>"
       "<h3>AP Settings</h3>"
       "<label>SSID<br><input type='text' name='ap_ssid' value='"+String(cfg.ap_ssid)+"'></label>"
       "<label>Password</label>"
       "<div class='pw-row'>"
         "<input id='ap_pass' type='password' name='ap_pass' value='"+String(cfg.ap_pass)+"'>"
         "<button type='button' onclick=\"togglePw('ap_pass',this)\">Show</button>"
         "<small>(toggle to verify)</small>"
       "</div>"
       "<label>Channel<br><input type='number' min='1' max='13' name='ap_ch' value='"+String(cfg.ap_ch)+"'></label>"
       "<p>Hidden SSID: <a href='/stealth?on=1'>Enable</a> <a href='/stealth?on=0'>Disable</a></p>"
       "<h3>Station Credentials</h3>"
       "<label>SSID<br><input type='text' name='sta_ssid' value='"+String(cfg.sta_ssid)+"'></label>"
       "<label>Password</label>"
       "<div class='pw-row'>"
         "<input id='sta_pass' type='password' name='sta_pass' value='"+String(cfg.sta_pass)+"'>"
         "<button type='button' onclick=\"togglePw('sta_pass',this)\">Show</button>"
         "<small>(toggle to verify)</small>"
       "</div>"
       "<p><input type='submit' value='Apply & Save'></p></form>"
       "<p><a href='/station/disconnect'>Disconnect STA</a> • <a href='/'>Home</a></p>";
  s += htmlFooter();
  return s;
}

// ===== Networking bring-up =====
void startAP(){
  WiFi.softAP(cfg.ap_ssid, cfg.ap_pass, cfg.ap_ch, cfg.ap_hidden);
  Serial.printf("AP up: %s  IP: %s  ch%d hidden=%d\n",
    cfg.ap_ssid, WiFi.softAPIP().toString().c_str(), cfg.ap_ch, cfg.ap_hidden);
}

void startSTA(){
  if (strlen(cfg.sta_ssid)==0) return;
  WiFi.begin(cfg.sta_ssid, cfg.sta_pass);
  staLastAttempt = millis();
  Serial.printf("STA: connecting to \"%s\"…\n", cfg.sta_ssid);
}

void applyNetMode(uint8_t nm, bool armFallback){
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  delay(50);

  if (nm == NM_AP_ONLY){
    WiFi.mode(WIFI_AP);
    startAP();
  } else if (nm == NM_STA_ONLY){
    WiFi.mode(WIFI_STA);
    startSTA();
    staOnlyStartMs = millis(); // start fallback timer
  } else { // NM_AP_STA
    WiFi.mode(WIFI_AP_STA);
    startAP();
    startSTA();
  }

  cfg.net_mode = nm;
  saveConfig();
  // fallback is handled in loop if STA_ONLY times out
}

// ===== Scanning =====
void runScanAndStore(){
  int n = WiFi.scanNetworks(false, true);
  String rows, csv;
  if (n <= 0) {
    rows = "<tr><td colspan='4' class='warn'>No networks found</td></tr>";
  } else {
    for (int i=0; i<n; i++){
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
  lastRows = rows; lastCSV = csv; lastScanTs = millis();
}

// ===== Routes =====
void handleRoot(){ if(!ensureAuth()) return; server.send(200,"text/html",homePage()); }

void handleScan(){ 
  if(!ensureAuth()) return; 
  currentMode=SCAN_ONESHOT; runScanAndStore(); currentMode=IDLE;
  server.send(200,"text/html",
    htmlHeader("Scan Results") +
    "<h1>Scan Results</h1><p><a href='/scan'>🔁 Rescan</a> <a href='/'>🏠 Home</a> <a href='/export.csv'>⬇️ Export CSV</a></p>"
    "<table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead><tbody>" +
    lastRows + "</tbody></table>" + htmlFooter());
}

void handleContinuous(){ 
  if(!ensureAuth()) return; 
  currentMode = (server.arg("on")=="1")?SCAN_CONTINUOUS:IDLE; 
  server.sendHeader("Location","/"); server.send(302,"text/plain","redirect"); 
}

void handleExportCSV(){ 
  if(!ensureAuth()) return; 
  server.send(200,"text/csv","SSID,RSSI,Channel,Encryption\n"+lastCSV); 
}

void handleStealth(){ 
  if(!ensureAuth()) return; 
  bool want = (server.arg("on")=="1"); 
  if (want!=cfg.ap_hidden){ 
    cfg.ap_hidden=want; saveConfig(); 
    if (WiFi.getMode() & WIFI_AP){ WiFi.softAPdisconnect(true); delay(20); startAP(); }
  } 
  server.sendHeader("Location","/"); server.send(302,"text/plain","redirect"); 
}

void handleNetworkGet(){ if(!ensureAuth()) return; server.send(200,"text/html",networkPage()); }

void handleNetworkPost(){
  if(!ensureAuth()) return;
  String m = server.arg("mode");
  String apS = server.arg("ap_ssid"), apP = server.arg("ap_pass"), apCh = server.arg("ap_ch");
  String stS = server.arg("sta_ssid"), stP = server.arg("sta_pass");

  if (apS.length()) apS.toCharArray(cfg.ap_ssid, sizeof(cfg.ap_ssid));
  if (apP.length()) apP.toCharArray(cfg.ap_pass, sizeof(cfg.ap_pass));
  uint8_t ch = apCh.length()? (uint8_t) constrain(apCh.toInt(),1,13) : cfg.ap_ch;
  cfg.ap_ch = ch;

  stS.toCharArray(cfg.sta_ssid, sizeof(cfg.sta_ssid));
  stP.toCharArray(cfg.sta_pass, sizeof(cfg.sta_pass));

  uint8_t requested = cfg.net_mode;
  if (m=="ap") requested = NM_AP_ONLY;
  else if (m=="sta") requested = NM_STA_ONLY;
  else requested = NM_AP_STA;

  saveConfig();
  applyNetMode(requested, /*armFallback=*/true);
  server.sendHeader("Location","/network");
  server.send(302,"text/plain","redirect");
}

void handleStationDisconnect(){ 
  if(!ensureAuth()) return; 
  WiFi.disconnect(); staConnected=false; 
  server.sendHeader("Location","/network"); server.send(302,"text/plain","redirect"); 
}

void handleReboot(){ if(!ensureAuth()) return; server.send(200,"text/plain","Rebooting…"); delay(250); ESP.restart(); }
void handleFactoryReset(){ if(!ensureAuth()) return; factoryReset(); server.send(200,"text/plain","Factory reset done. Rebooting…"); delay(250); ESP.restart(); }

// ===== Setup / Loop =====
void setup(){
  Serial.begin(115200);
  delay(200);

  LittleFS.begin();
  loadConfig();  // defaults if not present

  // bring up per saved mode
  if (cfg.net_mode==NM_AP_ONLY) {
    WiFi.mode(WIFI_AP);       startAP();
  } else if (cfg.net_mode==NM_STA_ONLY) {
    WiFi.mode(WIFI_STA);      startSTA(); staOnlyStartMs = millis();
  } else {
    WiFi.mode(WIFI_AP_STA);   startAP();  startSTA();
  }

  // routes
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/continuous", handleContinuous);
  server.on("/export.csv", handleExportCSV);
  server.on("/stealth", handleStealth);
  server.on("/network", HTTP_GET,  handleNetworkGet);
  server.on("/network", HTTP_POST, handleNetworkPost);
  server.on("/station/disconnect", handleStationDisconnect);
  server.on("/reboot", handleReboot);
  server.on("/factory-reset", handleFactoryReset);
  server.onNotFound([](){ if(!ensureAuth()) return; server.send(404,"text/plain","Not found"); });

  server.begin();
  Serial.println("Web server ready (start page at http://192.168.4.1 when AP is active).");
}

void loop(){
  server.handleClient();

  // continuous scan tick
  if (currentMode==SCAN_CONTINUOUS) {
    unsigned long now = millis();
    if (now - lastScanTick >= SCAN_INTERVAL_MS) {
      lastScanTick = now;
      runScanAndStore();
    }
  }

  // STA status + retry
  wl_status_t st = WiFi.status();
  bool nowConn = (st == WL_CONNECTED);
  if (nowConn && !staConnected) {
    staConnected = true;
    Serial.printf("STA: connected, IP=%s\n", WiFi.localIP().toString().c_str());
  } else if (!nowConn && staConnected) {
    staConnected = false;
    Serial.println("STA: disconnected");
  }
  if ((cfg.net_mode==NM_STA_ONLY || cfg.net_mode==NM_AP_STA) && strlen(cfg.sta_ssid)>0 && !nowConn) {
    unsigned long now = millis();
    if (now - staLastAttempt > 10000UL) {
      staLastAttempt = now;
      WiFi.begin(cfg.sta_ssid, cfg.sta_pass);
      Serial.printf("STA: reconnecting to \"%s\"…\n", cfg.sta_ssid);
    }
  }

  // Fallback: STA-only connect timeout => revert to AP-only
  if (cfg.net_mode==NM_STA_ONLY && !staConnected && (millis()-staOnlyStartMs > STA_ONLY_TIMEOUT)) {
    Serial.println("Safety: STA-only timeout, reverting to AP-only.");
    applyNetMode(NM_AP_ONLY, false);
  }
}
