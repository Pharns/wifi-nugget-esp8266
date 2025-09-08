#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DNSServer.h>

extern "C" {
  #include "lwip/napt.h"
  #include "user_interface.h"
}

/* =========================
   Config / Types / Globals
   ========================= */

#define CFG_PATH "/config.json"

enum NetMode : uint8_t { NM_AP_ONLY=0, NM_STA_ONLY=1, NM_AP_STA=2 };
enum AppMode : uint8_t { IDLE=0, SCAN_ONESHOT=1, SCAN_CONTINUOUS=2 };

struct Config {
  // AP
  char    ap_ssid[33]   = "Printer-Setup_24G";
  char    ap_pass[65]   = "ChangeThisPass!";
  uint8_t ap_ch         = 6;
  bool    ap_hidden     = false;

  // STA
  char    sta_ssid[33]  = "";
  char    sta_pass[65]  = "";

  // Modes
  uint8_t net_mode      = NM_AP_ONLY;
  bool    bridge_enabled= false;

  // Captive portal
  bool    portal_enabled= false;
  char    portal_allowlist[129] = "example.local,router.local";

  // Deauth (simulation only)
  bool    deauth_sim_enabled = false;
  char    deauth_sim_targets[257] = "TestAP1,AA:BB:CC:DD:EE:FF";

  // Schedule
  bool     schedule_enabled   = false;
  uint32_t schedule_interval_s= 60;

  // Deauth detection (sniffer)
  bool    deauth_detect_enabled = false;
} cfg;

const char* ADMIN_USER="admin";
const char* ADMIN_PASS="1234";

ESP8266WebServer server(80);

volatile AppMode currentMode=IDLE;
bool staConnected=false;
unsigned long lastScanTick=0, lastScanTs=0, staLastAttempt=0, staOnlyStartMs=0;
String lastRows, lastCSV;

// Captive portal
DNSServer dns; const byte DNS_PORT=53; bool dnsRunning=false;

// Scheduler
unsigned long schedLast=0;
const char* SCAN_LOG_PATH="/scan_log.csv";

// Deauth detect
bool deauth_attack_seen=false;

// Explicit AP IP
IPAddress apIP(192,168,4,1);
IPAddress netMsk(255,255,255,0);

/* =========================
   Helpers: Auth / Config IO
   ========================= */

bool ensureAuth(){
  if(!server.authenticate(ADMIN_USER,ADMIN_PASS)){
    server.requestAuthentication();
    return false;
  }
  return true;
}

bool saveConfig(){
  File f=LittleFS.open(CFG_PATH,"w"); if(!f) return false;
  StaticJsonDocument<1024> d;
  d["ap_ssid"]=cfg.ap_ssid; d["ap_pass"]=cfg.ap_pass;
  d["ap_ch"]=cfg.ap_ch; d["ap_hidden"]=cfg.ap_hidden;
  d["sta_ssid"]=cfg.sta_ssid; d["sta_pass"]=cfg.sta_pass;
  d["net_mode"]=cfg.net_mode; d["bridge_enabled"]=cfg.bridge_enabled;
  d["portal_enabled"]=cfg.portal_enabled; d["portal_allowlist"]=cfg.portal_allowlist;
  d["deauth_sim_enabled"]=cfg.deauth_sim_enabled; d["deauth_sim_targets"]=cfg.deauth_sim_targets;
  d["schedule_enabled"]=cfg.schedule_enabled; d["schedule_interval_s"]=cfg.schedule_interval_s;
  d["deauth_detect_enabled"]=cfg.deauth_detect_enabled;
  bool ok=serializeJson(d,f)>0; f.close(); return ok;
}
bool loadConfig(){
  if(!LittleFS.exists(CFG_PATH)) return false;
  File f=LittleFS.open(CFG_PATH,"r"); if(!f) return false;
  StaticJsonDocument<1024> d; if(deserializeJson(d,f)) return false; f.close();
  strlcpy(cfg.ap_ssid,d["ap_ssid"]|cfg.ap_ssid,sizeof(cfg.ap_ssid));
  strlcpy(cfg.ap_pass,d["ap_pass"]|cfg.ap_pass,sizeof(cfg.ap_pass));
  cfg.ap_ch=d["ap_ch"]|cfg.ap_ch; cfg.ap_hidden=d["ap_hidden"]|cfg.ap_hidden;
  strlcpy(cfg.sta_ssid,d["sta_ssid"]|cfg.sta_ssid,sizeof(cfg.sta_ssid));
  strlcpy(cfg.sta_pass,d["sta_pass"]|cfg.sta_pass,sizeof(cfg.sta_pass));
  cfg.net_mode=d["net_mode"]|cfg.net_mode; cfg.bridge_enabled=d["bridge_enabled"]|cfg.bridge_enabled;
  cfg.portal_enabled=d["portal_enabled"]|cfg.portal_enabled;
  strlcpy(cfg.portal_allowlist,d["portal_allowlist"]|cfg.portal_allowlist,sizeof(cfg.portal_allowlist));
  cfg.deauth_sim_enabled=d["deauth_sim_enabled"]|cfg.deauth_sim_enabled;
  strlcpy(cfg.deauth_sim_targets,d["deauth_sim_targets"]|cfg.deauth_sim_targets,sizeof(cfg.deauth_sim_targets));
  cfg.schedule_enabled=d["schedule_enabled"]|cfg.schedule_enabled;
  cfg.schedule_interval_s=d["schedule_interval_s"]|cfg.schedule_interval_s;
  cfg.deauth_detect_enabled=d["deauth_detect_enabled"]|cfg.deauth_detect_enabled;
  return true;
}
void factoryReset(){ LittleFS.remove(CFG_PATH); }

/* =========================
   Bridge / Portal / Detect
   ========================= */

void applyBridge(bool en){
  if(en && WiFi.status()==WL_CONNECTED){
    ip_napt_enable_no(0,1);
    Serial.println("Bridge: ON");
  } else {
    ip_napt_enable_no(0,0);
    Serial.println("Bridge: OFF");
  }
}

// Captive portal
void startPortal(){ if(dnsRunning) return; dns.start(DNS_PORT,"*",WiFi.softAPIP()); dnsRunning=true; Serial.println("Portal: DNS ON"); }
void stopPortal(){ if(!dnsRunning) return; dns.stop(); dnsRunning=false; Serial.println("Portal: DNS OFF"); }
String captivePage(){
  if(LittleFS.exists("/portal.html")){
    File f=LittleFS.open("/portal.html","r"); String html; while(f.available()) html+=(char)f.read(); f.close(); return html;
  }
  return "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Lab Portal</title></head><body><h1>Home Lab Portal</h1>"
         "<p>Captive portal demo. Control panel: <a href='http://192.168.4.1/'>192.168.4.1</a></p></body></html>";
}
bool hostAllowed(const String& h){
  String list=cfg.portal_allowlist; list+=",";
  int pos=0;
  while(true){
    int sep=list.indexOf(',',pos); if(sep<0) break;
    String tok=list.substring(pos,sep); tok.trim();
    if(tok.length() && h.endsWith(tok)) return true;
    pos=sep+1;
  }
  return false;
}

// Deauth detection (promiscuous)
void ICACHE_FLASH_ATTR sniffer_cb(uint8_t *buf,uint16_t len){
  if(len>0){
    uint8_t type = buf[0] & 0x0C; // coarse bucket for mgmt deauth/disassoc
    if(type==0x0C){
      deauth_attack_seen = true;
      Serial.println("⚠️ Deauth/Disassoc frame detected!");
    }
  }
}
void applyDeauthDetect(bool en){
  if(en){ wifi_set_promiscuous_rx_cb(sniffer_cb); wifi_promiscuous_enable(1); Serial.println("Deauth detect: ON"); }
  else  { wifi_promiscuous_enable(0);                                         Serial.println("Deauth detect: OFF");}
}

/* =========================
   Status / Scan / Logging
   ========================= */

String statusJSON(){
  StaticJsonDocument<768> d;
  d["net_mode"]=(cfg.net_mode==NM_AP_ONLY?"AP":(cfg.net_mode==NM_STA_ONLY?"STA":"AP+STA"));
  d["sta_configured"]=strlen(cfg.sta_ssid)>0; d["sta_connected"]=staConnected;
  d["bridge_enabled"]=cfg.bridge_enabled; d["ap_hidden"]=cfg.ap_hidden;
  d["scan_mode"]=(currentMode==SCAN_CONTINUOUS?"continuous":(currentMode==SCAN_ONESHOT?"oneshot":"idle"));
  d["ap_ip"]=WiFi.softAPIP().toString(); d["sta_ip"]=WiFi.localIP().toString();
  d["ap_ssid"]=cfg.ap_ssid; d["sta_ssid"]=cfg.sta_ssid;
  d["portal_enabled"]=cfg.portal_enabled; d["deauth_sim_enabled"]=cfg.deauth_sim_enabled;
  d["schedule_enabled"]=cfg.schedule_enabled; d["schedule_interval_s"]=cfg.schedule_interval_s;
  d["deauth_detect_enabled"]=cfg.deauth_detect_enabled; d["deauth_attack_seen"]=deauth_attack_seen;
  String out; serializeJson(d,out); return out;
}

void runScanAndStore(){
  int n=WiFi.scanNetworks(false,true);
  String rows, csv;
  if(n<=0){
    rows = "<tr><td colspan='4'><i>No networks found</i></td></tr>";
  } else {
    for(int i=0;i<n;i++){
      String ssid=WiFi.SSID(i);
      int rssi=WiFi.RSSI(i);
      int ch=WiFi.channel(i);
      String enc=(WiFi.encryptionType(i)==ENC_TYPE_NONE?"OPEN":"ENC");
      rows+="<tr><td>"+(ssid.length()?ssid:"<i>(hidden)</i>")+"</td><td>"+String(rssi)+"</td><td>"+String(ch)+"</td><td>"+enc+"</td></tr>";
      String safe=ssid; safe.replace(","," "); csv+=safe+","+String(rssi)+","+String(ch)+","+enc+"\n";
    }
  }
  WiFi.scanDelete();
  lastRows=rows; lastCSV=csv; lastScanTs=millis();
}

void appendScanLogCSV(){
  if(lastCSV.length()==0) return;
  File f=LittleFS.open(SCAN_LOG_PATH,LittleFS.exists(SCAN_LOG_PATH)?"a":"w");
  if(!f) return;
  if(f.size()==0) f.println("ts,ssid,rssi,channel,enc");
  unsigned long ts=millis();
  int pos=0; while(true){
    int nl=lastCSV.indexOf('\n',pos); if(nl<0) break;
    String line=lastCSV.substring(pos,nl);
    if(line.length()) f.println(String(ts)+","+line);
    pos=nl+1;
  }
  f.close();
}

/* =========================
   Wi-Fi Bring-up
   ========================= */

void startAP(){
  WiFi.softAPConfig(apIP, apIP, netMsk); // explicit AP IP 192.168.4.1
  WiFi.softAP(cfg.ap_ssid,cfg.ap_pass,cfg.ap_ch,cfg.ap_hidden);
  Serial.printf("AP %s started. IP: %s\n",cfg.ap_ssid,WiFi.softAPIP().toString().c_str());
  if(cfg.portal_enabled) startPortal(); else stopPortal();
}
void startSTA(){
  if(strlen(cfg.sta_ssid)==0) return;
  WiFi.begin(cfg.sta_ssid,cfg.sta_pass);
  staLastAttempt=millis();
  Serial.printf("STA: connecting to %s…\n", cfg.sta_ssid);
}
void applyNetMode(uint8_t nm){
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  delay(50);
  if(nm==NM_AP_ONLY){
    WiFi.mode(WIFI_AP);       startAP();            applyBridge(false);
  } else if(nm==NM_STA_ONLY){
    WiFi.mode(WIFI_STA);      startSTA(); staOnlyStartMs=millis(); applyBridge(cfg.bridge_enabled);
  } else {
    WiFi.mode(WIFI_AP_STA);   startAP();  startSTA();              applyBridge(cfg.bridge_enabled);
  }
  cfg.net_mode=nm; saveConfig();
}

/* =========================
   HTML Helpers & Pages
   ========================= */

String htmlHeader(const String& title){
  return String(
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu;margin:24px;max-width:960px}"
    "a,button,input[type=submit]{display:inline-block;margin:6px 8px 6px 0;padding:8px 12px;text-decoration:none;border:1px solid #ccc;border-radius:8px;background:#fff;cursor:pointer}"
    ".chip{display:inline-block;padding:4px 10px;border-radius:999px;margin:2px;background:#eee}"
    ".chip.ok{background:#e6ffed;border:1px solid #c3f3cf}"
    ".chip.warn{background:#fff4e5;border:1px solid #ffe0b2}"
    ".chip.err{background:#ffecec;border:1px solid #ffc7c7}"
    "table{border-collapse:collapse;width:100%}th,td{border-bottom:1px solid #eee;padding:8px;text-align:left}"
    "input[type=text],input[type=password],input[type=number]{padding:8px;border:1px solid #ccc;border-radius:8px;width:280px;max-width:100%}"
    "label{display:block;margin-top:10px}"
    ".muted{color:#666}"
    ".row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}"
    "</style><title>") + title + "</title></head><body>";
}
String htmlFooter(){
  return "<p class='muted'>AP IP: "+WiFi.softAPIP().toString()+" • STA IP: "+WiFi.localIP().toString()+"</p></body></html>";
}

String homePage(){
  String s=htmlHeader("Home");
  s+="<h1>WiFi Nugget</h1>";

  s+="<div id='chips' class='row'>"
     "<span class='chip' id='chip_mode'>Mode: …</span>"
     "<span class='chip' id='chip_ap'>AP: …</span>"
     "<span class='chip' id='chip_sta'>STA: …</span>"
     "<span class='chip' id='chip_bridge'>Bridge: …</span>"
     "<span class='chip' id='chip_scan'>Scan: …</span>"
     "<span class='chip' id='chip_portal'>Portal: …</span>"
     "<span class='chip' id='chip_deauth'>DeauthSim: …</span>"
     "<span class='chip' id='chip_detect'>Detect: …</span>"
     "<span class='chip' id='chip_sched'>Schedule: …</span>"
     "</div>";

  s+="<p class='row'>"
     "<a href='/scan'>🔎 One-shot Scan</a>"
     "<a href='/continuous?on=1'>▶️ Continuous</a>"
     "<a href='/continuous?on=0'>⏸ Stop</a>"
     "<a href='/export.csv'>⬇️ Export CSV</a>"
     "<a href='/scan_log.csv'>🗄️ Download Scan Log</a>"
     "<a href='/network'>⚙️ Network & Bridge</a>"
     "<a href='/tools'>🧰 Tools</a>"
     "<a href='/reboot'>🔄 Reboot</a>"
     "<a href='/factory-reset' onclick='return confirm(\"Factory reset? This erases saved settings.\")'>🧹 Factory Reset</a>"
     "</p>";

  s+="<h3>Latest Scan</h3>";
  if(lastRows.length()){
    s+="<table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead>"
       "<tbody>"+lastRows+"</tbody></table>"
       "<p class='muted'>Updated "+String((int)((millis()-lastScanTs)/1000))+"s ago</p>";
  } else {
    s+="<p><i>No scan yet.</i></p>";
  }

  // live updater for chips
  s+=R"(<script>
function cls(el, on){ el.classList.remove('ok','warn','err'); if(on) el.classList.add(on); }
function set(el, label, val, clsName){ el.textContent = label + ': ' + val; if(clsName) cls(el, clsName); }
function refresh(){
  fetch('/status.json').then(r=>r.json()).then(j=>{
    const m  = document.getElementById('chip_mode');
    const ap = document.getElementById('chip_ap');
    const st = document.getElementById('chip_sta');
    const br = document.getElementById('chip_bridge');
    const sc = document.getElementById('chip_scan');
    const po = document.getElementById('chip_portal');
    const da = document.getElementById('chip_deauth');
    const dt = document.getElementById('chip_detect');
    const sh = document.getElementById('chip_sched');

    set(m, 'Mode', j.net_mode, 'ok');
    set(ap, 'AP', j.ap_ssid + ' (' + (j.ap_ip || 'n/a') + ') ' + (j.ap_hidden?'Hidden':'Visible'), j.ap_hidden?'warn':'ok');
    set(st, 'STA', (j.sta_ssid?j.sta_ssid:'(unset)') + ' ' + (j.sta_connected?'Connected':'Down') + ' ' + (j.sta_ip||''), j.sta_connected?'ok':'warn');
    set(br, 'Bridge', j.bridge_enabled?'ON':'OFF', j.bridge_enabled?'ok':'');
    set(sc, 'Scan', j.scan_mode, j.scan_mode==='continuous'?'ok':(j.scan_mode==='oneshot'?'warn':''));
    set(po, 'Portal', j.portal_enabled?'ON':'OFF', j.portal_enabled?'ok':'');
    set(da, 'DeauthSim', j.deauth_sim_enabled?'ON':'OFF', j.deauth_sim_enabled?'warn':'');
    const detLabel = j.deauth_detect_enabled ? (j.deauth_attack_seen ? 'Detected!' : 'ON') : 'OFF';
    set(dt, 'Detect', detLabel, j.deauth_detect_enabled ? (j.deauth_attack_seen?'err':'ok') : '');
    set(sh, 'Schedule', j.schedule_enabled?('every '+j.schedule_interval_s+'s'):'OFF', j.schedule_enabled?'ok':'');
  }).catch(_=>{}).finally(_=>setTimeout(refresh, 1500));
}
refresh();
</script>)";

  s+=htmlFooter();
  return s;
}

String networkPage(){
  String s=htmlHeader("Network & Bridge");
  s+="<h1>Network & Bridge</h1>";
  s+="<form method='POST' action='/network'>";

  s+="<h3>Mode</h3>"
     "<label><input type='radio' name='mode' value='ap' "+String(cfg.net_mode==NM_AP_ONLY?"checked":"")+"> AP only</label><br>"
     "<label><input type='radio' name='mode' value='sta' "+String(cfg.net_mode==NM_STA_ONLY?"checked":"")+"> Station only</label><br>"
     "<label><input type='radio' name='mode' value='apsta' "+String(cfg.net_mode==NM_AP_STA?"checked":"")+"> AP + Station</label><br>";

  s+="<h3>Bridge (NAPT)</h3>"
     "<label><input type='checkbox' name='bridge' "+String(cfg.bridge_enabled?"checked":"")+"> Enable internet bridging to AP clients</label><br>";

  s+="<h3>AP Settings</h3>"
     "<label>SSID<br><input type='text' name='ap_ssid' value='"+String(cfg.ap_ssid)+"'></label>"
     "<label>Password<br>"
       "<input type='password' id='ap_pass' name='ap_pass' value='"+String(cfg.ap_pass)+"'> "
       "<button type='button' onclick=\"togglePw('ap_pass', this)\">Show</button>"
     "</label>"
     "<label>Channel (1-13)<br><input type='number' name='ap_ch' min='1' max='13' value='"+String(cfg.ap_ch)+"'></label>"
     "<p>Hidden SSID: <a href='/stealth?on=1'>Enable</a> • <a href='/stealth?on=0'>Disable</a></p>";

  s+="<h3>Station Credentials</h3>"
     "<label>SSID<br><input type='text' name='sta_ssid' value='"+String(cfg.sta_ssid)+"'></label>"
     "<label>Password<br>"
       "<input type='password' id='sta_pass' name='sta_pass' value='"+String(cfg.sta_pass)+"'> "
       "<button type='button' onclick=\"togglePw('sta_pass', this)\">Show</button>"
     "</label>";

  s+="<p><input type='submit' value='Apply & Save'></p></form>"
     "<p><a href='/station/disconnect'>Disconnect STA</a> • <a href='/'>Home</a></p>";

  // show/hide JS
  s+=R"(<script>
function togglePw(id, btn){
  const el=document.getElementById(id);
  if(!el) return;
  if(el.type==='password'){ el.type='text'; btn.textContent='Hide'; }
  else { el.type='password'; btn.textContent='Show'; }
}
</script>)";

  s+=htmlFooter();
  return s;
}

String toolsPage(){
  String s=htmlHeader("Tools");
  s+="<h1>Tools</h1><form method='POST' action='/tools'>";

  s+="<h3>Captive Portal</h3>"
     "<label><input type='checkbox' name='portal' "+String(cfg.portal_enabled?"checked":"")+"> Enable DNS redirect on AP</label><br>"
     "<label>Allowlist (comma-separated)<br><input type='text' name='portal_allowlist' value='"+String(cfg.portal_allowlist)+"' style='width:100%'></label>";

  s+="<h3>Deauth (Simulation)</h3>"
     "<label><input type='checkbox' name='deauth' "+String(cfg.deauth_sim_enabled?"checked":"")+"> Enable deauth <i>simulation</i></label><br>"
     "<label>Targets (SSIDs/BSSIDs, comma-separated)<br><input type='text' name='deauth_targets' value='"+String(cfg.deauth_sim_targets)+"' style='width:100%'></label>"
     "<p><a href='/deauth-sim-run'>Run Simulation Now</a></p>";

  s+="<h3>Deauth Detection</h3>"
     "<label><input type='checkbox' name='detect' "+String(cfg.deauth_detect_enabled?"checked":"")+"> Enable deauth attack detection (promiscuous sniff)</label>";

  s+="<h3>Schedule</h3>"
     "<label><input type='checkbox' name='schedule' "+String(cfg.schedule_enabled?"checked":"")+"> Run one-shot Wi-Fi scan on a schedule</label><br>"
     "<label>Interval (seconds)<br><input type='number' min='10' max='86400' name='interval' value='"+String(cfg.schedule_interval_s)+"'></label>";

  s+="<p><input type='submit' value='Apply & Save'></p></form>"
     "<p><a href='/'>Home</a></p>";

  s+=htmlFooter();
  return s;
}

/* =========================
   Routes / Setup / Loop
   ========================= */

void setup(){
  Serial.begin(115200); delay(200);
  LittleFS.begin();
  loadConfig();
  ip_napt_init(IP_NAPT_MAX, IP_PORTMAP_MAX);

  if(cfg.net_mode==NM_AP_ONLY){ WiFi.mode(WIFI_AP); startAP(); applyBridge(false); }
  else if(cfg.net_mode==NM_STA_ONLY){ WiFi.mode(WIFI_STA); startSTA(); staOnlyStartMs=millis(); applyBridge(cfg.bridge_enabled); }
  else { WiFi.mode(WIFI_AP_STA); startAP(); startSTA(); applyBridge(cfg.bridge_enabled); }

  if(cfg.deauth_detect_enabled) applyDeauthDetect(true);

  // Pages
  server.on("/", [](){ if(!ensureAuth()) return; server.send(200,"text/html",homePage()); });
  server.on("/network", HTTP_GET, [](){ if(!ensureAuth()) return; server.send(200,"text/html",networkPage()); });
  server.on("/tools",   HTTP_GET, [](){ if(!ensureAuth()) return; server.send(200,"text/html",toolsPage()); });

  // Network POST
  server.on("/network", HTTP_POST, [](){
    if(!ensureAuth()) return;
    String m = server.arg("mode");
    cfg.net_mode = (m=="ap" ? NM_AP_ONLY : (m=="sta" ? NM_STA_ONLY : NM_AP_STA));
    cfg.bridge_enabled = server.hasArg("bridge");

    if(server.hasArg("ap_ssid")) strlcpy(cfg.ap_ssid, server.arg("ap_ssid").c_str(), sizeof(cfg.ap_ssid));
    if(server.hasArg("ap_pass")) strlcpy(cfg.ap_pass, server.arg("ap_pass").c_str(), sizeof(cfg.ap_pass));
    if(server.hasArg("ap_ch")){
      int ch = constrain(server.arg("ap_ch").toInt(), 1, 13);
      cfg.ap_ch = (uint8_t)ch;
    }

    if(server.hasArg("sta_ssid")) strlcpy(cfg.sta_ssid, server.arg("sta_ssid").c_str(), sizeof(cfg.sta_ssid));
    if(server.hasArg("sta_pass")) strlcpy(cfg.sta_pass, server.arg("sta_pass").c_str(), sizeof(cfg.sta_pass));

    saveConfig();
    applyNetMode(cfg.net_mode);

    server.sendHeader("Location","/network"); server.send(302);
  });

  // Tools POST  (complete)
  server.on("/tools", HTTP_POST, []() {
    if (!ensureAuth()) return;

    cfg.portal_enabled = server.hasArg("portal");
    strlcpy(cfg.portal_allowlist,
            server.arg("portal_allowlist").c_str(),
            sizeof(cfg.portal_allowlist));

    cfg.deauth_sim_enabled = server.hasArg("deauth");
    strlcpy(cfg.deauth_sim_targets,
            server.arg("deauth_targets").c_str(),
            sizeof(cfg.deauth_sim_targets));

    cfg.deauth_detect_enabled = server.hasArg("detect");

    cfg.schedule_enabled = server.hasArg("schedule");
    if (server.hasArg("interval")) {
      cfg.schedule_interval_s = server.arg("interval").toInt();
      if (cfg.schedule_interval_s < 10) cfg.schedule_interval_s = 10;
      if (cfg.schedule_interval_s > 86400) cfg.schedule_interval_s = 86400;
    }

    saveConfig();

    if (WiFi.getMode() & WIFI_AP) {
      if (cfg.portal_enabled) startPortal();
      else stopPortal();
    }
    applyDeauthDetect(cfg.deauth_detect_enabled);

    schedLast = millis();

    server.sendHeader("Location", "/tools");
    server.send(302);
  });

  // Actions
  server.on("/scan", [](){
    if(!ensureAuth()) return;
    currentMode=SCAN_ONESHOT; runScanAndStore(); currentMode=IDLE;
    server.send(200,"text/html",
      htmlHeader("Scan Results") +
      "<h1>Scan Results</h1><p><a href='/scan'>🔁 Rescan</a> <a href='/'>Home</a> <a href='/export.csv'>⬇️ Export CSV</a></p>"
      "<table><thead><tr><th>SSID</th><th>RSSI</th><th>Chan</th><th>Enc</th></tr></thead><tbody>"+lastRows+"</tbody></table>" +
      htmlFooter()
    );
  });

  server.on("/continuous", [](){
    if(!ensureAuth()) return;
    currentMode = (server.arg("on")=="1") ? SCAN_CONTINUOUS : IDLE;
    server.sendHeader("Location","/"); server.send(302);
  });

  server.on("/export.csv", [](){
    if(!ensureAuth()) return;
    server.send(200,"text/csv","SSID,RSSI,Channel,Encryption\n"+lastCSV);
  });

  server.on("/scan_log.csv", [](){
    if(!ensureAuth()) return;
    if(!LittleFS.exists(SCAN_LOG_PATH)){ server.send(404,"text/plain","No log yet"); return; }
    File f=LittleFS.open(SCAN_LOG_PATH,"r"); server.streamFile(f,"text/csv"); f.close();
  });

  server.on("/stealth", [](){
    if(!ensureAuth()) return;
    bool want = (server.arg("on")=="1");
    if (cfg.ap_hidden != want){
      cfg.ap_hidden = want; saveConfig();
      if (WiFi.getMode() & WIFI_AP){ WiFi.softAPdisconnect(true); delay(20); startAP(); }
    }
    server.sendHeader("Location","/"); server.send(302);
  });

  server.on("/station/disconnect", [](){
    if(!ensureAuth()) return;
    WiFi.disconnect(); staConnected=false; applyBridge(false);
    server.sendHeader("Location","/network"); server.send(302);
  });

  server.on("/reboot", [](){
    if(!ensureAuth()) return; server.send(200,"text/plain","Rebooting…"); delay(200); ESP.restart();
  });

  server.on("/factory-reset", [](){
    if(!ensureAuth()) return; factoryReset(); server.send(200,"text/plain","Factory reset. Rebooting…"); delay(200); ESP.restart();
  });

  server.on("/deauth-sim-run", [](){
    if(!ensureAuth()) return;
    Serial.printf("Deauth(SIM): targets: %s\n", cfg.deauth_sim_targets);
    Serial.println("Deauth(SIM): Scanning… (SIM ONLY)");
    Serial.println("Deauth(SIM): Would transmit here (SIM ONLY).");
    server.sendHeader("Location","/tools"); server.send(302);
  });

  server.on("/status.json", [](){
    if(!ensureAuth()) return;
    server.send(200,"application/json", statusJSON());
  });

  // Captive portal intercept on unknown paths
  server.onNotFound([&](){
    if(!ensureAuth()) return;
    if (cfg.portal_enabled && (WiFi.getMode() & WIFI_AP)) {
      String h=server.hostHeader();
      if(!hostAllowed(h)){ server.send(200,"text/html",captivePage()); return; }
    }
    server.send(404,"text/plain","Not found");
  });

  server.begin();
  schedLast = millis();
  Serial.println("Web server ready.");
}

void loop(){
  server.handleClient();
  if(dnsRunning) dns.processNextRequest();

  // Continuous scan cadence
  if(currentMode==SCAN_CONTINUOUS){
    unsigned long now=millis();
    if(now-lastScanTick>=7000){ lastScanTick=now; runScanAndStore(); }
  }

  // STA watcher + bridge
  wl_status_t st=WiFi.status();
  bool nowConn = (st==WL_CONNECTED);
  if(nowConn && !staConnected){ staConnected=true; applyBridge(cfg.bridge_enabled); }
  else if(!nowConn && staConnected){ staConnected=false; applyBridge(false); }

  // STA reconnect
  if((cfg.net_mode==NM_STA_ONLY || cfg.net_mode==NM_AP_STA) && strlen(cfg.sta_ssid)>0 && !nowConn){
    unsigned long now=millis();
    if(now - staLastAttempt > 10000UL){ staLastAttempt=now; WiFi.begin(cfg.sta_ssid,cfg.sta_pass); }
  }

  // Safety: STA-only fallback -> AP-only
  if(cfg.net_mode==NM_STA_ONLY && !staConnected && (millis()-staOnlyStartMs > 60000UL)){
    Serial.println("Safety: STA-only timeout, reverting to AP-only.");
    cfg.net_mode = NM_AP_ONLY; saveConfig(); applyNetMode(NM_AP_ONLY);
  }

  // Scheduler
  if(cfg.schedule_enabled){
    unsigned long now=millis();
    if(now - schedLast >= (cfg.schedule_interval_s * 1000UL)){
      schedLast = now;
      Serial.printf("Schedule: one-shot scan (every %lus)\n", cfg.schedule_interval_s);
      runScanAndStore();
      appendScanLogCSV();
      currentMode = IDLE;
    }
  }

  // Deauth simulation: periodic log
  if(cfg.deauth_sim_enabled){
    static unsigned long lastSim=0;
    unsigned long now=millis();
    if(now - lastSim > 15000UL){
      lastSim = now;
      Serial.printf("Deauth(SIM): would target %s\n", cfg.deauth_sim_targets);
    }
  }
}
