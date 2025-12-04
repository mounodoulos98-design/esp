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
        
        <h3>BLE Parent Discovery</h3>
        <div class="muted">Use BLE to find Repeater/Root before WiFi connection (power efficient)</div>
        <label>
          <input type="checkbox" id="bleBeaconEnabled" name="bleBeaconEnabled" value="1" checked>
          Enable BLE scanning for parent discovery
        </label>
        <label for="bleScanDurationSec">BLE Scan Duration (sec):</label>
        <input type="number" id="bleScanDurationSec" name="bleScanDurationSec" value="5" min="1" max="30">
        <div class="muted">Recommended: 5 seconds. Longer = more reliable, higher power</div>
        
        <h3>Parent Node (Where to Send Data)</h3>
        <div class="muted">Connect to Repeater or Root to upload collected sensor data</div>
        <div class="row">
          <div style="flex:2">
            <label for="uplinkSSID">Parent WiFi SSID:</label>
            <input type="text" id="uplinkSSID" name="uplinkSSID" placeholder="Repeater_AP" required>
          </div>
          <div style="flex:1;align-self:end">
            <button type="button" onclick="scanWiFi('uplinkSSID')">Scan WiFi</button>
          </div>
        </div>
        <select id="wifiList" style="width:100%;margin-top:5px;display:none;"></select>
        <label for="uplinkPASS">Parent WiFi Password:</label>
        <input type="text" id="uplinkPASS" name="uplinkPASS" placeholder="(leave empty if open)">
        
        <div class="muted" style="margin-top:10px">📡 With BLE enabled, parent IP is auto-discovered. Without BLE, set manually:</div>
        <label for="uplinkHost">Parent IP Address (optional with BLE):</label>
        <input type="text" id="uplinkHost" name="uplinkHost" placeholder="Auto: 192.168.20.1 or 192.168.10.1">
        <label for="uplinkPort">Parent HTTP Port:</label>
        <input type="number" id="uplinkPort" name="uplinkPort" value="8080">
      </div>

      <!-- REPEATER -->
      <div id="repeaterSettings" class="group">
        <h3>Repeater Settings</h3>
        <label for="apSSID">Repeater AP SSID (for collectors):</label><input type="text" id="apSSID" name="apSSID" placeholder="Repeater_AP">
        <label for="apPASS">Repeater AP Password:</label><input type="text" id="apPASS" name="apPASS" placeholder="Password">
        
        <h3>BLE Beacon (for Child Discovery)</h3>
        <div class="muted">Advertise BLE beacon so Collectors can find this Repeater</div>
        <label>
          <input type="checkbox" id="bleBeaconEnabled_r" name="bleBeaconEnabled" value="1" checked>
          Enable BLE beacon advertising (continuous, light sleep)
        </label>
        <div class="muted">Power: ~20-30 mA. Allows instant wake-up when Collector connects.</div>
        
        <h3>Parent Node (Forward Data To Root)</h3>
        <div class="muted">Connect to Root to forward data from Collectors</div>
        <div class="row">
          <div style="flex:2">
            <label for="uplinkSSID_r">Root WiFi SSID:</label>
            <input type="text" id="uplinkSSID_r" name="uplinkSSID" placeholder="Root_AP" required>
          </div>
          <div style="flex:1;align-self:end">
            <button type="button" onclick="scanWiFi('uplinkSSID_r')">Scan WiFi</button>
          </div>
        </div>
        <select id="wifiList_r" style="width:100%;margin-top:5px;display:none;"></select>
        <label for="uplinkPASS_r">Root WiFi Password:</label>
        <input type="text" id="uplinkPASS_r" name="uplinkPASS" placeholder="(leave empty if open)">
        <label for="uplinkHost_r">Root IP Address:</label>
        <input type="text" id="uplinkHost_r" name="uplinkHost" placeholder="192.168.10.1" required>
        <label for="uplinkPort_r">Root HTTP Port:</label>
        <input type="number" id="uplinkPort_r" name="uplinkPort" value="8080">
      </div>

      <!-- ROOT -->
      <div id="rootSettings" class="group">
        <h3>Root Settings</h3>
        <label for="apSSID_root">Root AP SSID:</label><input type="text" id="apSSID_root" name="apSSID" placeholder="Root_AP">
        <label for="apPASS_root">Root AP Password:</label><input type="text" id="apPASS_root" name="apPASS">
        <label for="uplinkPort_root">HTTP Port:</label><input type="number" id="uplinkPort_root" name="uplinkPort" value="8080">
        
        <h3>BLE Configuration</h3>
        <div class="muted">Root is always on via WiFi. BLE beacon not needed.</div>
        <label>
          <input type="checkbox" id="bleBeaconEnabled_root" name="bleBeaconEnabled" value="0">
          Enable BLE beacon (not recommended for Root)
        </label>
      </div>

      <input type="submit" value="Save and Reboot">
    </form>
  </div>

  <script>
    console.log('[CONFIG-JS] Script loaded');
    
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
    
    // Initialize on page load
    window.addEventListener('load', function() {
      console.log('[CONFIG-JS] Page loaded, initializing');
      toggleRoleFields();
      
      // Handle form submission with visual feedback
      const form = document.querySelector('form');
      const submitBtn = document.querySelector('input[type="submit"]');
      
      if (!form || !submitBtn) {
        console.error('[CONFIG-JS] ERROR: Form or submit button not found!');
        console.error('[CONFIG-JS] form:', form);
        console.error('[CONFIG-JS] submitBtn:', submitBtn);
        return;
      }
      
      console.log('[CONFIG-JS] Form handler attached');
      
      form.addEventListener('submit', async function(e) {
        e.preventDefault();
        console.log('Form submitted');
        
        // Disable button and show progress
        submitBtn.disabled = true;
        submitBtn.value = 'Saving...';
        submitBtn.style.backgroundColor = '#999';
        
        try {
          const formData = new FormData(form);
          console.log('Sending form data to /save');
          
          const response = await fetch('/save', {
            method: 'POST',
            body: formData
          });
          
          console.log('Response status:', response.status);
          
          if (response.ok) {
            submitBtn.value = 'Saved! Rebooting...';
            submitBtn.style.backgroundColor = '#4CAF50';
            alert('Configuration saved successfully! Device will reboot now.');
            // Give time for user to see the message
            setTimeout(function() {
              // Try to reconnect after reboot (30 seconds)
              setTimeout(function() {
                window.location.reload();
              }, 30000);
            }, 2000);
          } else {
            const error = await response.text();
            throw new Error(error || 'Save failed');
          }
        } catch (error) {
          console.error('Save error:', error);
          alert('Error saving configuration: ' + error.message);
          submitBtn.disabled = false;
          submitBtn.value = 'Save and Reboot';
          submitBtn.style.backgroundColor = '#4CAF50';
        }
      });
    });
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
    Serial.println("[CONFIG] Received save request");
    
    // Validate required parameters
    if(!request->hasParam("nodeName",true) || !request->hasParam("role",true)) {
      Serial.println("[CONFIG] ERROR: Missing required parameters");
      request->send(400,"text/plain","ERROR: Missing node name or role");
      return;
    }
    
    config.nodeName = request->getParam("nodeName",true)->value();
    String roleStr = request->getParam("role",true)->value();
    Serial.printf("[CONFIG] Role: %s, Name: %s\n", roleStr.c_str(), config.nodeName.c_str());

    if(roleStr=="collector"){
      config.role = ROLE_COLLECTOR;
      if(request->hasParam("sensorAP_SSID",true)) config.sensorAP_SSID = request->getParam("sensorAP_SSID",true)->value();
      if(request->hasParam("collectorApCycleSec",true)) config.collectorApCycleSec = request->getParam("collectorApCycleSec",true)->value().toInt();
      if(request->hasParam("collectorApWindowSec",true)) config.collectorApWindowSec = request->getParam("collectorApWindowSec",true)->value().toInt();
      if(request->hasParam("collectorDataTimeoutSec",true)) config.collectorDataTimeoutSec = request->getParam("collectorDataTimeoutSec",true)->value().toInt();
      if(request->hasParam("uplinkSSID",true)) config.uplinkSSID = request->getParam("uplinkSSID",true)->value();
      if(request->hasParam("uplinkPASS",true)) config.uplinkPASS = request->getParam("uplinkPASS",true)->value();
      if(request->hasParam("uplinkHost",true)) config.uplinkHost = request->getParam("uplinkHost",true)->value();
      if(request->hasParam("uplinkPort",true)) config.uplinkPort = request->getParam("uplinkPort",true)->value().toInt();
      
      // BLE configuration for Collector (scanning for parent)
      config.bleBeaconEnabled = request->hasParam("bleBeaconEnabled",true);
      if(request->hasParam("bleScanDurationSec",true)) {
        config.bleScanDurationSec = request->getParam("bleScanDurationSec",true)->value().toInt();
      }
      Serial.printf("[CONFIG] Collector config: BLE=%d, Scan=%ds\n", config.bleBeaconEnabled, config.bleScanDurationSec);
    } else if(roleStr=="repeater"){
      config.role = ROLE_REPEATER;
      if(request->hasParam("apSSID",true)) config.apSSID = request->getParam("apSSID",true)->value();
      if(request->hasParam("apPASS",true)) config.apPASS = request->getParam("apPASS",true)->value();
      if(request->hasParam("uplinkSSID",true)) config.uplinkSSID = request->getParam("uplinkSSID",true)->value();
      if(request->hasParam("uplinkPASS",true)) config.uplinkPASS = request->getParam("uplinkPASS",true)->value();
      if(request->hasParam("uplinkHost",true)) config.uplinkHost = request->getParam("uplinkHost",true)->value();
      if(request->hasParam("uplinkPort",true)) config.uplinkPort = request->getParam("uplinkPort",true)->value().toInt();
      
      // BLE configuration for Repeater (beacon advertising)
      config.bleBeaconEnabled = request->hasParam("bleBeaconEnabled",true);
      Serial.printf("[CONFIG] Repeater config: BLE=%d\n", config.bleBeaconEnabled);
    } else {
      config.role = ROLE_ROOT;
      if(request->hasParam("apSSID",true)) config.apSSID = request->getParam("apSSID",true)->value();
      if(request->hasParam("apPASS",true)) config.apPASS = request->getParam("apPASS",true)->value();
      if(request->hasParam("uplinkPort",true)) config.uplinkPort = request->getParam("uplinkPort",true)->value().toInt();
      
      // BLE configuration for Root (typically disabled)
      config.bleBeaconEnabled = request->hasParam("bleBeaconEnabled",true);
      Serial.printf("[CONFIG] Root config: BLE=%d\n", config.bleBeaconEnabled);
    }

    config.isConfigured = true;
    Serial.println("[CONFIG] Saving configuration to flash...");
    saveConfiguration();
    
    time_t now; time(&now);
    if(now>1700000000) persistRtcTime(now);
    
    Serial.println("[CONFIG] Sending success response and rebooting...");
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
