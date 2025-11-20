#include "config.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;
extern DNSServer dnsServer;

void persistRtcTime(time_t epoch);

const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Node Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; }
    .container { max-width: 560px; margin: auto; background: white; padding: 20px; border-radius: 8px;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    h2 { text-align: center; }
    label { display: block; margin-top: 15px; font-weight: bold; }
    input, select { width: 100%; padding: 10px; margin-top: 5px; border-radius: 4px;
      border: 1px solid #ccc; box-sizing: border-box; }
    input[type=submit], button { background-color: #4CAF50; color: white; padding: 12px 16px;
      margin-top: 20px; border: none; cursor: pointer; font-size: 16px; border-radius: 6px; }
    .id-display { background-color: #e3f2fd; color: #1e88e5; padding: 15px; border-radius: 4px;
      text-align: center; font-size: 1.0em; font-weight: bold; }
    .row { display:flex; gap:10px; }
    .row > div { flex:1; }
    .muted { color:#666; font-size: 0.92em; }
    .group { background:#fafafa; border:1px solid #eee; padding:12px; border-radius:8px; margin-top:16px;}
  </style>
</head>
<body>
  <div class="container">
    <h2>Node Configuration</h2>
    <div class="id-display">Node ID: {NODE_ID}</div>
    <button type="button" onclick="syncTime()">Sync Time from Browser</button>
    <div class="muted">Sets the node's clock. This is essential for scheduled operations.</div>

    <form action="/save" method="POST">
      <label for="nodeName">Node Name:</label>
      <input type="text" id="nodeName" name="nodeName" required>

      <label for="role">Node Role:</label>
      <select id="role" name="role" required onchange="toggleRoleFields()">
        <option value="collector">Collector (Receives sensor data)</option>
        <option value="repeater">Repeater (Forwards mesh data)</option>
        <option value="root">Root (Final destination)</option>
      </select>

      <!-- COLLECTOR -->
      <div id="collectorSettings" class="group">
        <h3>Collector Settings</h3>
        <label for="sensorAP_SSID">Sensor AP SSID:</label>
        <input type="text" id="sensorAP_SSID" name="sensorAP_SSID" placeholder="e.g., Sensor_AP">
        <div class="row">
          <div><label for="collectorApCycleSec">Cycle (sec):</label><input type="number" id="collectorApCycleSec" name="collectorApCycleSec" value="120"></div>
          <div><label for="collectorApWindowSec">AP Window (sec):</label><input type="number" id="collectorApWindowSec" name="collectorApWindowSec" value="15"></div>
        </div>
        <label for="collectorDataTimeoutSec">Data Timeout (sec):</label>
        <input type="number" id="collectorDataTimeoutSec" name="collectorDataTimeoutSec" value="120">
        <h3>Uplink (Send Data To)</h3>
        <div class="row">
          <div style="flex:2">
            <label for="uplinkSSID">Uplink SSID:</label>
            <input type="text" id="uplinkSSID" name="uplinkSSID" placeholder="Repeater_AP or Root_AP">
          </div>
          <div style="flex:1;align-self:end">
            <button type="button" onclick="scanWiFi('uplinkSSID')">Scan</button>
          </div>
        </div>
        <select id="wifiList" style="width:100%;margin-top:5px;display:none;"></select>
        <label for="uplinkPASS">Uplink Password:</label><input type="text" id="uplinkPASS" name="uplinkPASS">
        <label for="uplinkHost">Uplink Host/IP:</label><input type="text" id="uplinkHost" name="uplinkHost" placeholder="192.168.10.1">
        <label for="uplinkPort">Uplink Port:</label><input type="number" id="uplinkPort" name="uplinkPort" value="8080">
      </div>

      <!-- REPEATER -->
      <div id="repeaterSettings" class="group">
        <h3>Repeater Settings</h3>
        <label for="apSSID">Repeater AP SSID (for collectors):</label><input type="text" id="apSSID" name="apSSID" placeholder="Repeater_AP">
        <label for="apPASS">Repeater AP Password:</label><input type="text" id="apPASS" name="apPASS" placeholder="Password">
        <h3>Uplink to Root</h3>
        <div class="row">
          <div style="flex:2">
            <label for="uplinkSSID_r">Uplink SSID:</label>
            <input type="text" id="uplinkSSID_r" name="uplinkSSID" placeholder="Root_AP">
          </div>
          <div style="flex:1;align-self:end">
            <button type="button" onclick="scanWiFi('uplinkSSID_r')">Scan</button>
          </div>
        </div>
        <select id="wifiList_r" style="width:100%;margin-top:5px;display:none;"></select>
        <label for="uplinkPASS_r">Uplink Password:</label><input type="text" id="uplinkPASS_r" name="uplinkPASS">
        <label for="uplinkHost_r">Uplink Host:</label><input type="text" id="uplinkHost_r" name="uplinkHost">
        <label for="uplinkPort_r">Uplink Port:</label><input type="number" id="uplinkPort_r" name="uplinkPort" value="8080">
      </div>

      <!-- ROOT -->
      <div id="rootSettings" class="group">
        <h3>Root Settings</h3>
        <label for="apSSID_root">Root AP SSID:</label><input type="text" id="apSSID_root" name="apSSID" placeholder="Root_AP">
        <label for="apPASS_root">Root AP Password:</label><input type="text" id="apPASS_root" name="apPASS">
        <label for="uplinkPort_root">HTTP Port:</label><input type="number" id="uplinkPort_root" name="uplinkPort" value="8080">
      </div>

      <input type="submit" value="Save and Reboot">
    </form>
  </div>

  <script>
    async function syncTime() {
      const epoch = Math.floor(Date.now() / 1000);
      try {
        const resp = await fetch('/settime?epoch=' + epoch, { method: 'POST' });
        if (resp.ok) alert('Time synced: ' + new Date(epoch * 1000).toLocaleString());
        else alert('Failed to sync time.');
      } catch (e) { alert('Error: ' + e); }
    }

    async function scanWiFi(targetInput) {
      const btns = document.querySelectorAll("button[onclick^='scanWiFi']");
      btns.forEach(b=>b.disabled=true);
      try {
        const resp = await fetch('/scan');
        if (!resp.ok) throw new Error(resp.statusText);
        const nets = await resp.json();
        const sel = document.getElementById(targetInput==='uplinkSSID_r'?'wifiList_r':'wifiList');
        sel.innerHTML='';
        nets.forEach(n=>{
          const opt=document.createElement('option');
          opt.value=n.ssid;
          opt.text=`${n.ssid} (${n.rssi} dBm)`;
          sel.appendChild(opt);
        });
        sel.style.display='block';
        sel.onchange=()=>{
          document.getElementById(targetInput).value=sel.value;
          sel.style.display='none';
        };
      }catch(e){alert('Scan failed: '+e.message);}
      finally{btns.forEach(b=>b.disabled=false);}
    }

    function toggleRoleFields() {
      const role = document.getElementById('role').value;
      document.getElementById('collectorSettings').style.display = (role==='collector')?'block':'none';
      document.getElementById('repeaterSettings').style.display = (role==='repeater')?'block':'none';
      document.getElementById('rootSettings').style.display = (role==='root')?'block':'none';
    }
    window.addEventListener('load', toggleRoleFields);
  </script>
</body>
</html>
)rawliteral";

void startConfigurationMode() {
  setStatusLed(STATUS_CONFIG_MODE);
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_SSID, MESH_PASSWORD, &userScheduler, MESH_PORT);
  while (mesh.getNodeId() == 0) { mesh.update(); delay(10); }
  uint32_t nodeId = mesh.getNodeId();
  mesh.stop();

  WiFi.mode(WIFI_AP_STA); // ✅ AP + STA για Wi-Fi scan
  String ap_ssid = CONFIG_AP_SSID_PREFIX + String(nodeId);
  WiFi.softAP(ap_ssid.c_str(), CONFIG_AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.printf("[CONFIG] AP '%s' started. IP: %s\n", ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, [nodeId](AsyncWebServerRequest *req) {
    String html = CONFIG_PAGE;
    html.replace("{NODE_ID}", String(nodeId));
    req->send(200, "text/html", html);
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i=0;i<n;++i){
      if(i>0) json+=",";
      json+="{\"ssid\":\""+WiFi.SSID(i)+"\",\"rssi\":"+String(WiFi.RSSI(i))+"}";
    }
    json += "]";
    WiFi.scanDelete();
    request->send(200,"application/json",json);
  });

  server.on("/settime", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("epoch")) {
      time_t e = (time_t)atol(request->getParam("epoch")->value().c_str());
      struct timeval tv = { .tv_sec = e };
      settimeofday(&tv, NULL);
      persistRtcTime(e);
      request->send(200,"text/plain","OK");
    } else request->send(400,"text/plain","Bad Request");
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    config.nodeName = request->getParam("nodeName",true)->value();
    String roleStr = request->getParam("role",true)->value();

    if(roleStr=="collector"){
      config.role = ROLE_COLLECTOR;
      config.sensorAP_SSID = request->getParam("sensorAP_SSID",true)->value();
      config.collectorApCycleSec = request->getParam("collectorApCycleSec",true)->value().toInt();
      config.collectorApWindowSec = request->getParam("collectorApWindowSec",true)->value().toInt();
      config.collectorDataTimeoutSec = request->getParam("collectorDataTimeoutSec",true)->value().toInt();
      config.uplinkSSID = request->getParam("uplinkSSID",true)->value();
      config.uplinkPASS = request->getParam("uplinkPASS",true)->value();
      config.uplinkHost = request->getParam("uplinkHost",true)->value();
      config.uplinkPort = request->getParam("uplinkPort",true)->value().toInt();
    } else if(roleStr=="repeater"){
      config.role = ROLE_REPEATER;
      config.apSSID = request->getParam("apSSID",true)->value();
      config.apPASS = request->getParam("apPASS",true)->value();
      config.uplinkSSID = request->getParam("uplinkSSID",true)->value();
      config.uplinkPASS = request->getParam("uplinkPASS",true)->value();
      config.uplinkHost = request->getParam("uplinkHost",true)->value();
      config.uplinkPort = request->getParam("uplinkPort",true)->value().toInt();
    } else {
      config.role = ROLE_ROOT;
      config.apSSID = request->getParam("apSSID",true)->value();
      config.apPASS = request->getParam("apPASS",true)->value();
      config.uplinkPort = request->getParam("uplinkPort",true)->value().toInt();
    }

    config.isConfigured = true;
    saveConfiguration();
    time_t now; time(&now);
    if(now>1700000000) persistRtcTime(now);
    request->send(200,"text/plain","Settings saved. Rebooting...");
    delay(1000);
    ESP.restart();
  });

  server.onNotFound([](AsyncWebServerRequest *req){ req->redirect("/"); });
  server.begin();
  Serial.println("[CONFIG] Web server started.");
}

void loopConfigurationMode(){
  dnsServer.processNextRequest();
  esp_task_wdt_reset();
  delay(10);
}
