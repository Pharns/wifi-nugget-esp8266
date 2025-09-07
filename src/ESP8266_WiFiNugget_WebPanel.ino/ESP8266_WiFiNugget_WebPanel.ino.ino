#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ---------- NAT/Bridge (NAPT) ----------
extern "C" {
  #include "lwip/napt.h"
}

// ====== CONFIG & DEFAULTS ======
#define CFG_PATH "/config.json"

enum NetMode : uint8_t { NM_AP_ONLY=0, NM_STA_ONLY=1, NM_AP_STA=2 };
enum AppMode : uint8_t { IDLE=0, SCAN_ONESHOT=1, SCAN_CONTINUOUS=2 };

struct Config {
  // AP
  char ap_ssid[33]   = "Printer-Setup_24G";
  char ap_pass[65]   = "ChangeThisPass!";
  uint8_t ap_ch      = 6;
  bool ap_hidden     = false;

  // STA
  char sta_ssid[33]  = "";
  char sta_pass[65]  = "";

  // Modes
  uint8_t net_mode   = NM_AP_ONLY;
  bool bridge_enabled = false;   // desired state; active only when STA connected
} cfg;

// ===== WEB AUTH =====
const char* ADMIN_USER = "admin";
const char* ADMIN_PASS = "1234";  // change me

ESP8266WebServer server(80);

// ===== RUNTIME =====
volatile AppMode currentMode = IDLE;
bool   staConnected = false;
unsigned long lastScanTick = 0, lastScanTs = 0, staLastAttempt = 0, staOnlyStartMs = 0;

const unsigned long SCAN_INTERVAL_MS = 7000;
const unsigned long STA_ONLY_TIMEOUT = 60000UL;

String lastRows, lastCSV;

// ---------- Auth ----------
bool ensureAuth() {
  if (!server.authenticate(ADMIN_USER, ADMIN_PASS)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// ---------- Config I/O ----------
bool saveConfig() {
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  StaticJsonDocument<640> d;
  d["ap_ssid"]        = cfg.ap_ssid;
  d["ap_pass"]        = cfg.ap_pass;
  d["ap_ch"]          = cfg.ap_ch;
  d["ap_hidden"]      = cfg.ap_hidden;
  d["sta_ssid"]       = cfg.sta_ssid;
  d["sta_pass"]       = cfg.sta_pass;
  d["net_mode"]       = cfg.net_mode;
  d["bridge_enabled"] = cfg.bridge_enabled;
  bool ok = (serializeJson(d, f) > 0);
  f.close();
  return ok;
}

bool loadConfig() {
  if (!LittleFS.exists(CFG_PATH)) return false;
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;
  StaticJsonDocument<640> d;
  auto e = deserializeJson(d, f);
  f.close();
  if (e) return false;

  strlcpy(cfg.ap_ssid,  d["ap_ssid"]  | cfg.ap_ssid,  sizeof(cfg.ap_ssid));
  strlcpy(cfg.ap_pass,  d["ap_pass"]  | cfg.ap_pass,  sizeof(cfg.ap_pass));
  cfg.ap_ch     = d["ap_ch"]    | cfg.ap_ch;
  cfg.ap_hidden = d["ap_hidden"]| cfg.ap_hidden;

  strlcpy(cfg.sta_ssid, d["sta_ssid"] | cfg.sta_ssid, sizeof(cfg.sta_ssid));
  strlcpy(cfg.sta_pass, d["sta_pass"] | cfg.sta_pass, sizeof(cfg.sta_pass));
  cfg.net_mode  = d["net_mode"] | cfg.net_mode;

  cfg.bridge_enabled = d["bridge_enabled"] | cfg.bridge_enabled;
  return true;
}

void factoryReset() { LittleFS.remove(CFG_PATH); }

// ---------- NAT Bridge ----------
void applyBridge(bool enable) {
  if (enable && (WiFi.status() == WL_CONNECTED)) {
    ip_napt_enable_no(0, 1); // enable NAPT on STA (if#0)
    Serial.println("Bridge (NAPT): ENABLED on STA uplink");
  } else {
    ip_napt_enable_no(0, 0); // disable NAPT
    Serial.println("Bridge (NAPT): disabled");
  }
}

// ---------- Status JSON ----------
String statusJSON() {
  StaticJsonDocument<384> d;
  d["net_mode"]       = (cfg.net_mode==NM_AP_ONLY? "AP" : (cfg.net_mode==NM_STA_ONLY? "STA" : "AP+STA"));
  d["sta_configured"] = (strlen(cfg.sta_ssid) > 0);
  d["sta_connected"]  = staConnected;
  d["bridge_enabled"] = cfg.bridge_enabled;
  d["ap_hidden"]      = cfg.ap_hidden;
  d["scan_mode"]      = (currentMode==SCAN_CONTINUOUS? "continuous" : (currentMode==SCAN_ONESHOT? "oneshot" : "idle"));
  d["ap_ip"]          = WiFi.softAPIP().toString();
  d["sta_ip"]         = WiFi.localIP().toString();
  d["ap_ssid"]        = cfg.ap_ssid;
  d["sta_ssid"]       = cfg.sta_ssid;
  String out; out.reserve(360);
  serializeJson(d, out);
  return out;
}

// ---------- HTML helpers (chips + UI + auto-refresh) ----------
String htmlHeader(const String& title){
  return String(
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu;max-width:960px;margin:24px auto;padding:0 12px}"
    "a,button,input[type=submit]{display:inline-block;margin:6px 8px 6px 0;padding:8px 12px;text-decoration:none;border:1px solid #ccc;border-radius:8px;background:#fff}"
    "table{border-collapse:collapse;width:100%}th,td{border-bottom:1px solid #eee;padding:8px;text-align:left;white-space:nowrap}"
    "input[type=text],input[type=password],input[type=number]{padding:8px;border:1px solid #ccc;border-radius:8px;width:280px;max-width:100%}"
    "label{display:block;margin-top:8px}"
    ".muted{color:#666;font-size:0.9em}"
    ".row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}"
    ".chips{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0 4px}"
    ".chip{display:inline-flex;align-items:center;gap:8px;border:1px solid #ddd;border-radius:999px;padding:4px 10px;font-size:.9em}"
    ".chip b{font-weight:600}"
    ".ok{background:#e8f7ee;border-color:#bfe8cc}"
    ".warn{background:#fff5e5;border-color:#ffd797}"
    ".off{background:#f4f4f4;border-color:#ddd;color:#555}"
    ".bad{background:#fdecec;border-color:#f6b2b2}"
    ".pw-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.pw-row small{color:#666}"
    ".grid{display:grid;gap:12px}@media(min-width:780px){.grid{grid-template-columns:1fr 1fr}}"
    ".note{font-size:.9em;color:#444;background:#f8f8f8;border:1px solid #eee;border-radius:8px;padding:8px 12px;display:inline-block}"
    "</style>"
    "<script>"
      "function togglePw(id,btn){var el=document.getElementById(id);if(!el)return;"
      "if(el.type==='password'){el.type='text';if(btn)btn.textContent='Hide';}"
      "else{el.type='password';if(btn)btn.textContent='Show';}}"

      // auto-refresh: fetch /status.json every ~3s and update chips/footer
      "let _timer=null;"
      "function clsFor(label,val){"
        "if(label==='STA'){if(val==='Connected')return'ok'; if(val==='Connecting…'||val==='Not configured')return'warn'; return'off';}"
        "if(label==='Bridge'){if(val.indexOf('active')>=0)return'ok'; if(val.indexOf('ON')>=0)return'warn'; return'off';}"
        "if(label==='AP'){return (val==='Hidden')?'warn':'ok';}"
        "if(label==='Scan'){return (val==='Continuous')?'ok':(val==='One-shot'?'warn':'off');}"
        "return'ok';"
      "}"
      "function setChip(id,label,val){"
        "var el=document.getElementById(id); if(!el) return;"
        "el.textContent=val;"
        "el.className='chip '+clsFor(label,val);"
      "}"
      "function refreshStatus(){"
        "fetch('/status.json',{cache:'no-store'}).then(r=>{if(!r.ok)throw 0;return r.json();}).then(j=>{"
          "const nm=j.net_mode;"
          "const sta=(j.sta_connected? 'Connected' : (j.sta_configured? 'Connecting…':'Not configured'));"
          "const br=(j.bridge_enabled? (j.sta_connected? 'ON (active)':'ON (waiting for STA)'):'OFF');"
          "const ap=(j.ap_hidden? 'Hidden':'Visible');"
          "const scan = (j.scan_mode==='continuous'?'Continuous':(j.scan_mode==='oneshot'?'One-shot':'Idle'));"
          "setChip('chip_nm','NetMode',nm);"
          "setChip('chip_sta','STA',sta);"
          "setChip('chip_br','Bridge',br);"
          "setChip('chip_ap','AP',ap);"
          "setChip('chip_scan','Scan',scan);"
          "var f=document.getElementById('footer-line'); if(f){"
            "f.textContent = 'AP IP: '+j.ap_ip+' • STA IP: '+j.sta_ip+' • App: '+scan;"
          "}"
        "}).catch(_=>{});"
      "}"
      "function startRefresh(){"
        "refreshStatus();"
        "if(_timer) clearInterval(_timer);"
        " _timer=setInterval(refreshStatus, 3000);"
      "}"
      "document.addEventListener('DOMContentLoaded', startRefresh);"
    "</script>"
    "<title>") + title + "</title></head><body>";
}

String chipSpan(const char* id, const String& label, const String& value, const char* cls){
  return String("<span class='chip ") + cls + "'><b>" + label + ":</b> <span id='" + id + "' class='chip " + cls + "'>" + value + "</span></span>";
}

String htmlFooter(){
  String apIp  = WiFi.softAPIP().toString();
  String staIp = WiFi.localIP().toString();
  String modeStr = (currentMode==SCAN_CONTINUOUS? "Continuous" : (currentMode==SCAN_ONESHOT? "One-shot" : "Idle"));
  return "<p id='footer-line' class='muted'>AP IP: " + apIp + " • STA IP: " + staIp + " • App: " + modeStr + "</p></body></html>";
}

// ---------- Pages ----------
String homePage(){
  String nmLabel = (cfg.net_mode==NM_AP_ONLY? "AP" : (cfg.net_mode==NM_STA_ONLY? "STA":"AP+STA"));
  const char* nmCls = "ok";

  String staVal, staCls;
  if (cfg.net_mode==NM_STA_ONLY || cfg.net_mode==NM_AP_STA) {
    staVal = staConnected ? "Connected" : (strlen(cfg.sta_ssid)? "Connecting…":"Not configured");
    staCls = staConnected ? "ok" : (strlen(cfg.sta_ssid)? "warn":"off");
  } else { staVal="Off"; staCls="off"; }

  String brVal, brCls;
  if (cfg.bridge_enabled) { brVal = staConnected ? "ON (active)" : "ON (waiting for STA)"; brCls = staConnected ? "ok":"warn"; }
  else { brVal="OFF"; brCls="off"; }

  String apVal = cfg.ap_hidden ? "Hidden" : "Visible";
  const char* apCls = cfg.ap_hidden ? "warn":"ok";

  String scanVal = (currentMode==SCAN_CONTINUOUS? "Continuous" : (currentMode==SCAN_ONESHOT? "One-shot" : "Idle"));
  const char* scanCls = (currentMode==SCAN_CONTINUOUS? "ok" : (currentMode==SCAN_ONESHOT? "warn":"off"));

  String s = htmlHeader("Wi-Fi Control Panel");
  s += "<h1>Wi-Fi Control Panel</h1>";

  s += "<div class='chips'>";
  s += "<span class='chip ok'><b>NetMode:</b> <span id='chip_nm' class='chip ok'>" + nmLabel + "</span></span>";
  s += "<span class='chip "+staCls+"'><b>STA:</b> <span id='chip_sta' class='chip "+staCls+"'>" + staVal + "</span></span>";
  s += "<span class='chip "+brCls+"'><b>Bridge:</b> <span id='chip_br' class='chip "+brCls+"'>" + brVal + "</span></span>";
  s += "<span class='chip " + String(apCls) + "'><b>AP:</b> <span id='chip_ap' class='chip " + String(apCls) + "'>" + apVal + "</span></span>";
  s += "<span class='chip " + String(scanCls) + "'><b>Scan:</b> <span id='chip_scan' class='chip " + String(scanCls) + "'>" + scanVal + "</span></span>";
  s += "</div>";

  s += "<p class='row'><a href='/scan'>🔎 One-shot Scan</a>"
       "<a href='/continuous?on=1'>▶️ Continuous</a>"
       "<a href='/continuous?on=0'>⏸ Stop</a>"
       "<a href='/export.csv'>⬇️ Export CSV</a>"
       "<a href='/network'>⚙️ Network & Bridge</a>"
       "<a href='/reboot'>🔄 Reboot</a>"
       "<a href='/factory-reset' onclick='return confirm(\"Factory reset? This erases saved Wi-Fi/mode settings.\")'>🧹 Factory Reset</a></p>";

  if (lastRows.isEmpty()) {
    s += "<h3>Latest Scan</h3><p class='muted'>No scan yet.</p>";
  } else {
    s += "<h3>Latest Scan</h3><table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead><tbody>";
    s += lastRows + "</tbody></table>";
    s += "<p class='muted'>Updated " + String((int)((millis()-lastScanTs)/1000)) + "s ago</p>";
  }
  s += htmlFooter();
  return s;
}

String networkPage(){
  String apChecked  = (cfg.net_mode==NM_AP_ONLY) ? "checked":"";
  String staChecked = (cfg.net_mode==NM_STA_ONLY)? "checked":"";
  String apsChecked = (cfg.net_mode==NM_AP_STA)  ? "checked":"";
  String brChecked  = (cfg.bridge_enabled) ? "checked":"";

  String s = htmlHeader("Network & Bridge");
  s += "<h1>Network & Bridge</h1>";

  // small status chips (live)
  String staVal = (cfg.net_mode==NM_STA_ONLY || cfg.net_mode==NM_AP_STA)
                  ? (staConnected ? "Connected" : (strlen(cfg.sta_ssid)? "Connecting…":"Not configured"))
                  : "Off";
  String staCls = (cfg.net_mode==NM_STA_ONLY || cfg.net_mode==NM_AP_STA)
                  ? (staConnected ? "ok" : (strlen(cfg.sta_ssid)? "warn":"off"))
                  : "off";
  String brVal = cfg.bridge_enabled ? (staConnected ? "ON (active)":"ON (waiting)") : "OFF";
  String brCls = cfg.bridge_enabled ? (staConnected ? "ok":"warn") : "off";
  String apVal = (cfg.ap_hidden? "Hidden":"Visible");
  String apCls = (cfg.ap_hidden? "warn":"ok");

  s += "<div class='chips'>"
       "<span class='chip "+staCls+"'><b>STA:</b> <span id='chip_sta' class='chip "+staCls+"'>"+staVal+"</span></span>"
       "<span class='chip "+brCls+"'><b>Bridge:</b> <span id='chip_br' class='chip "+brCls+"'>"+brVal+"</span></span>"
       "<span class='chip "+apCls+"'><b>AP:</b> <span id='chip_ap' class='chip "+apCls+"'>"+apVal+"</span></span>"
       "</div>";

  s += "<p class='note'>Bridge Mode is a <b>NAT bridge</b> (routed repeater). Keep AP and STA on <b>different subnets</b> (default AP: 192.168.4.1/24).</p>";

  s += "<form method='POST' action='/network'><div class='grid'>";

  s += "<div><h3>Mode</h3>"
       "<p><label><input type='radio' name='mode' value='ap' "+apChecked+"> AP only</label><br>"
       "<label><input type='radio' name='mode' value='sta' "+staChecked+"> Station only</label><br>"
       "<label><input type='radio' name='mode' value='apsta' "+apsChecked+"> AP + Station</label></p>"
       "<h3>Bridge (NAT)</h3>"
       "<p><label><input type='checkbox' name='bridge' value='1' "+brChecked+"> Enable internet bridging to AP clients (NAPT)</label></p></div>";

  s += "<div><h3>AP Settings</h3>"
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
       "</div></div>";

  s += "</div><p><input type='submit' value='Apply & Save'></p></form>"
       "<p><a href='/station/disconnect'>Disconnect STA</a> • <a href='/'>Home</a></p>";

  s += htmlFooter();
  return s;
}

// ---------- Wi-Fi bring-up ----------
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

void applyNetMode(uint8_t nm){
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  delay(50);

  if (nm == NM_AP_ONLY){
    WiFi.mode(WIFI_AP);
    startAP();
    applyBridge(false);
  } else if (nm == NM_STA_ONLY){
    WiFi.mode(WIFI_STA);
    startSTA();
    staOnlyStartMs = millis();
    applyBridge(cfg.bridge_enabled); // will be active once STA connects
  } else {
    WiFi.mode(WIFI_AP_STA);
    startAP();
    startSTA();
    applyBridge(cfg.bridge_enabled); // active when STA connects
  }

  cfg.net_mode = nm;
  saveConfig();
}

// ---------- Scanning ----------
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

// ---------- Routes ----------
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
  bool wantBridge = server.hasArg("bridge") && server.arg("bridge") == "1";

  if (apS.length()) apS.toCharArray(cfg.ap_ssid, sizeof(cfg.ap_ssid));
  if (apP.length()) apP.toCharArray(cfg.ap_pass, sizeof(cfg.ap_pass));
  uint8_t ch = apCh.length()? (uint8_t) constrain(apCh.toInt(),1,13) : cfg.ap_ch;
  cfg.ap_ch = ch;

  stS.toCharArray(cfg.sta_ssid, sizeof(cfg.sta_ssid));
  stP.toCharArray(cfg.sta_pass, sizeof(cfg.sta_pass));

  cfg.bridge_enabled = wantBridge;

  uint8_t requested = cfg.net_mode;
  if (m=="ap") requested = NM_AP_ONLY;
  else if (m=="sta") requested = NM_STA_ONLY;
  else requested = NM_AP_STA;

  saveConfig();
  applyNetMode(requested);
  server.sendHeader("Location","/network");
  server.send(302,"text/plain","redirect");
}

void handleStationDisconnect(){
  if(!ensureAuth()) return;
  WiFi.disconnect(); staConnected=false;
  applyBridge(false);
  server.sendHeader("Location","/network"); server.send(302,"text/plain","redirect");
}

void handleReboot(){ if(!ensureAuth()) return; server.send(200,"text/plain","Rebooting…"); delay(250); ESP.restart(); }
void handleFactoryReset(){ if(!ensureAuth()) return; factoryReset(); server.send(200,"text/plain","Factory reset done. Rebooting…"); delay(250); ESP.restart(); }

// status JSON
void handleStatusJson(){
  if(!ensureAuth()) return;
  server.send(200, "application/json", statusJSON());
}

// ---------- Setup / Loop ----------
void setup(){
  Serial.begin(115200);
  delay(200);

  LittleFS.begin();
  loadConfig();

  ip_napt_init(IP_NAPT_MAX, IP_PORTMAP_MAX);

  if (cfg.net_mode==NM_AP_ONLY) {
    WiFi.mode(WIFI_AP);       startAP();            applyBridge(false);
  } else if (cfg.net_mode==NM_STA_ONLY) {
    WiFi.mode(WIFI_STA);      startSTA(); staOnlyStartMs = millis(); applyBridge(cfg.bridge_enabled);
  } else {
    WiFi.mode(WIFI_AP_STA);   startAP();  startSTA();                applyBridge(cfg.bridge_enabled);
  }

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
  server.on("/status.json", handleStatusJson);
  server.onNotFound([](){ if(!ensureAuth()) return; server.send(404,"text/plain","Not found"); });

  server.begin();
  Serial.println("Web server ready (http://192.168.4.1 when AP is active).");
}

void loop(){
  server.handleClient();

  // continuous scan
  if (currentMode==SCAN_CONTINUOUS) {
    unsigned long now = millis();
    if (now - lastScanTick >= SCAN_INTERVAL_MS) {
      lastScanTick = now;
      runScanAndStore();
    }
  }

  // STA watcher + bridge hook
  wl_status_t st = WiFi.status();
  bool nowConn = (st == WL_CONNECTED);
  if (nowConn && !staConnected) {
    staConnected = true;
    Serial.printf("STA: connected, IP=%s\n", WiFi.localIP().toString().c_str());
    applyBridge(cfg.bridge_enabled);
  } else if (!nowConn && staConnected) {
    staConnected = false;
    Serial.println("STA: disconnected");
    applyBridge(false);
  }

  // retry STA when configured but down
  if ((cfg.net_mode==NM_STA_ONLY || cfg.net_mode==NM_AP_STA) && strlen(cfg.sta_ssid)>0 && !nowConn) {
    unsigned long now = millis();
    if (now - staLastAttempt > 10000UL) {
      staLastAttempt = now;
      WiFi.begin(cfg.sta_ssid, cfg.sta_pass);
      Serial.printf("STA: reconnecting to \"%s\"…\n", cfg.sta_ssid);
    }
  }

  // Fallback from STA-only to AP-only
  if (cfg.net_mode==NM_STA_ONLY && !staConnected && (millis()-staOnlyStartMs > STA_ONLY_TIMEOUT)) {
    Serial.println("Safety: STA-only timeout, reverting to AP-only.");
    cfg.net_mode = NM_AP_ONLY; saveConfig();
    applyNetMode(NM_AP_ONLY);
  }
}
