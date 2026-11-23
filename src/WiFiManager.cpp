#include "WiFiManager.h"
#include <esp_log.h>
#include "config.h"
#include "esp_wifi.h"

static const char* TAG = "WiFi";

WiFiManager::WiFiManager() :
 webServer(nullptr),
 dnsStarted(false),
 configMode(false),
 credentialCount(0),
 scanComplete(false),
 networksFound(0),
 apStarted(false),
 apStartTime(0),
 reconnectCount(0),
 initialized(false),
 _initialConnected(false),
 smartConfigStarted(false),
 smartConfigDone(false),
 smartConfigStartTime(0),
 _configModeStartTime(0),
 _configDisabled(false),
 _reconnectAttempts(0),
 _reconnectWaitUntil(0),
 _netReady(false),
 _activeCredIndex(-1) {
}

bool WiFiManager::connect() {
 ESP_LOGI(TAG, "Starting STA mode connection...");

 if (!initialized) {
  begin();
  initialized = true;
 }

 if (autoConnect()) {
  ESP_LOGI(TAG, "STA mode connected successfully!");
  onConnected();
  return true;
 }

 ESP_LOGI(TAG, "No visible saved WiFi, entering config mode...");
 startConfigMode();
 return false;
}

void WiFiManager::onConnected() {
 _netReady = true;
 reconnectCount = 0;
 _reconnectAttempts = 0;
 _reconnectWaitUntil = 0;
 _initialConnected = true;
}

void WiFiManager::maintainConnection() {
 handleClient();

 if (apStarted && !configMode) return;

 if (_configDisabled) {
  if (WiFi.getMode() != WIFI_OFF) {
   ESP_LOGW(TAG, "[WiFi] 配网已禁用，关闭射频（重启后可重新配网）");
   WiFi.mode(WIFI_OFF);
  }
  _netReady = false;
  return;
 }

 if (isConfigMode()) {
  handleConfigMode();
  return;
 }

 if (WiFi.status() == WL_CONNECTED) {
  onConnected();
  return;
 }

 _netReady = false;
 handleReconnect();
}

void WiFiManager::startConfigMode() {
 configMode = true;
 _configModeStartTime = millis();
 _netReady = false;

 ESP_LOGI(TAG, "[WiFi] 进入配网模式：开启热点门户，总窗口 %lu 分钟",
               CONFIG_MODE_TIMEOUT_MS / 60000);

 startConfigPortal();
}

void WiFiManager::handleConfigMode() {
 unsigned long elapsed = millis() - _configModeStartTime;

 if (WiFi.status() == WL_CONNECTED) {
  ESP_LOGI(TAG, "[WiFi] 配网成功! IP: %s", WiFi.localIP().toString().c_str());
  stopAPMode();
  configMode = false;
  onConnected();
  return;
 }

 if (elapsed >= CONFIG_MODE_TIMEOUT_MS) {
  ESP_LOGW(TAG, "[WiFi] 配网超时（%lu 分钟），关闭 WiFi", CONFIG_MODE_TIMEOUT_MS / 60000);
  stopAPMode();
  WiFi.mode(WIFI_OFF);
  configMode = false;
  _configDisabled = true;
  _netReady = false;
 }
}

void WiFiManager::handleReconnect() {
 unsigned long now = millis();

 if (_reconnectWaitUntil != 0) {
  if (now < _reconnectWaitUntil) return;
  _reconnectWaitUntil = 0;
  _reconnectAttempts = 0;
 }

 if (_reconnectAttempts >= RECONNECT_RETRY_MAX) {
  ESP_LOGW(TAG, "[WiFi] 已重试 %d 次仍失败，等待 %lu 分钟后再试",
                RECONNECT_RETRY_MAX, RECONNECT_WAIT_MS / 60000);
  _reconnectWaitUntil = now + RECONNECT_WAIT_MS;
  return;
 }

 _reconnectAttempts++;
 reconnectCount++;
 ESP_LOGI(TAG, "[WiFi] 断线重连 第 %d/%d 次", _reconnectAttempts, RECONNECT_RETRY_MAX);

 int idx = (_activeCredIndex >= 0 && _activeCredIndex < credentialCount) ? _activeCredIndex : 0;
 if (connectSavedIndex(idx, RECONNECT_RETRY_INTERVAL_MS)) {
  ESP_LOGI(TAG, "[WiFi] 重连成功! IP: %s", WiFi.localIP().toString().c_str());
  onConnected();
 }
}

bool WiFiManager::connectSavedIndex(int idx, unsigned long timeoutMs) {
 if (idx < 0 || idx >= credentialCount) return false;

 WiFi.mode(apStarted ? WIFI_AP_STA : WIFI_STA);
 WiFi.begin(savedCredentials[idx].ssid.c_str(), savedCredentials[idx].password.c_str());
 esp_wifi_set_max_tx_power(WIFI_TX_POWER_RAW);

 unsigned long start = millis();
 while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
  delay(500);
 }

 if (WiFi.status() == WL_CONNECTED) {
  _activeCredIndex = idx;
  ESP_LOGI(TAG, "[WiFi] SSID: %s connected", savedCredentials[idx].ssid.c_str());
  return true;
 }

 WiFi.disconnect(false);
 delay(100);
 return false;
}

bool WiFiManager::isConnected() {
 return _netReady;
}

int WiFiManager::getReconnectCount() { return reconnectCount; }
int WiFiManager::getRSSI() { return WiFi.RSSI(); }

const char* WiFiManager::getLocalIP() {
 static String ipStr;
 ipStr = WiFi.localIP().toString();
 return ipStr.c_str();
}

const char* WiFiManager::getSSID() {
 static String ssidStr;
 if (WiFi.status() == WL_CONNECTED) {
  ssidStr = WiFi.SSID();
 } else {
  ssidStr = "";
 }
 return ssidStr.c_str();
}

bool WiFiManager::isAPMode() { return isConfigMode(); }

void WiFiManager::handleClient() {
 if (dnsStarted) {
  dnsServer.processNextRequest();
 }
 if (webServer) {
  webServer->handleClient();
 }
}

void WiFiManager::startAPMode() {
 if (apStarted) return;

 configMode = false;

 ESP_LOGI(TAG, "Starting AP mode...");

 WiFi.mode(WIFI_AP_STA);

 WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
 updateApSsid();
 WiFi.softAP(apSsid.c_str(), NULL, 1, false, 4);

 delay(500);

 ESP_LOGI(TAG, "AP IP: %s", WiFi.softAPIP().toString().c_str());
 ESP_LOGI(TAG, "AP SSID: %s", WiFi.softAPSSID().c_str());

 apStarted = true;
 apStartTime = millis();

 scanNetworks();
 ESP_LOGI(TAG, "[WiFi] 预扫描完成：%d 个网络", networksFound);

 startPortalDns();
 startWebServer();
}

void WiFiManager::stopAPMode() {
 stopPortalDns();

 if (!apStarted) return;

 WiFi.scanDelete();
 scanComplete = false;

 ESP_LOGI(TAG, "Stopping AP mode...");
 WiFi.softAPdisconnect(true);
 delay(100);

 stopWebServer();

 apStarted = false;
 apStartTime = 0;
 configMode = false;

 ESP_LOGI(TAG, "AP mode stopped");
}

void WiFiManager::stopWebServer() {
 if (webServer) {
  webServer->stop();
  delete webServer;
  webServer = nullptr;
 }
}

void WiFiManager::startPortalDns() {
 if (dnsStarted) {
  dnsServer.stop();
  dnsStarted = false;
 }

 dnsStarted = dnsServer.start(53, "*", WiFi.softAPIP());
 if (dnsStarted) {
  ESP_LOGI(TAG, "[CaptivePortal] DNS 劫持已启动 -> %s", WiFi.softAPIP().toString().c_str());
 } else {
  ESP_LOGW(TAG, "[CaptivePortal] DNS 劫持启动失败（不影响手动打开 http://192.168.4.1/）");
 }
}

void WiFiManager::stopPortalDns() {
 if (!dnsStarted) return;
 dnsServer.stop();
 dnsStarted = false;
 ESP_LOGI(TAG, "[CaptivePortal] DNS 劫持已停止");
}

void WiFiManager::updateApSsid() {
 uint8_t mac[6] = {0};
 WiFi.macAddress(mac);
 char buf[16];
 snprintf(buf, sizeof(buf), "esp32-%02x%02x", mac[4], mac[5]);
 apSsid = buf;
}

bool WiFiManager::isAPStarted() { return apStarted; }
bool WiFiManager::hasInitialConnected() const { return _initialConnected; }

void WiFiManager::begin() {
 ESP_LOGI(TAG, "Initializing NVS...");
 if (preferences.begin("wifi-config", false)) {
  ESP_LOGI(TAG, "NVS initialized successfully");
 } else {
  ESP_LOGI(TAG, "NVS read-only failed, trying read-write...");
  if (preferences.begin("wifi-config", true)) {
   ESP_LOGI(TAG, "NVS opened in read-write mode");
  } else {
   ESP_LOGE(TAG, "NVS open FAILED!");
  }
 }

 loadCredentials();
 ESP_LOGI(TAG, "Initialized");
 initialized = true;
}

bool WiFiManager::autoConnect() {
 if (!hasSavedCredentials()) {
  ESP_LOGW(TAG, "[WiFi] 无已存凭据，跳过自动连接");
  return false;
 }

 ESP_LOGI(TAG, "Trying saved WiFi credentials...");

 WiFi.disconnect(false);
 delay(100);
 int found = WiFi.scanNetworks(false, false);

 if (found < 0) {
  ESP_LOGW(TAG, "[WiFi] 扫描失败(%d)，逐个尝试已存凭据", found);
  for (int i = 0; i < credentialCount; i++) {
   if (connectSavedIndex(i, WIFI_CONNECT_TIMEOUT_MS)) return true;
  }
  return false;
 }

 int rssi[MAX_WIFI_CREDENTIALS];
 int order[MAX_WIFI_CREDENTIALS];
 int visible = 0;
 for (int i = 0; i < credentialCount; i++) {
  rssi[i] = -127;
  for (int j = 0; j < found; j++) {
   if (savedCredentials[i].ssid == WiFi.SSID(j)) {
    rssi[i] = WiFi.RSSI(j);
    break;
   }
  }
  if (rssi[i] != -127) order[visible++] = i;
 }
 WiFi.scanDelete();

 if (visible == 0) {
  ESP_LOGW(TAG, "[WiFi] 已存凭据均不可见（扫描到 %d 个网络），不尝试连接", found);
  return false;
 }

 for (int i = 0; i < visible; i++) {
  for (int j = i + 1; j < visible; j++) {
   if (rssi[order[j]] > rssi[order[i]]) {
    int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
   }
  }
 }

 for (int k = 0; k < visible; k++) {
  const int i = order[k];
  ESP_LOGI(TAG, "[WiFi] 尝试可见 SSID: %s (RSSI %d)", savedCredentials[i].ssid.c_str(), rssi[i]);
  if (connectSavedIndex(i, WIFI_CONNECT_TIMEOUT_MS)) {
   ESP_LOGI(TAG, "\n[WiFi] Saved WiFi connected!");
   return true;
  }
  ESP_LOGI(TAG, "[WiFi] SSID %s failed, trying next...", savedCredentials[i].ssid.c_str());
 }

 ESP_LOGW(TAG, "[WiFi] 可见凭据均连接失败");
 return false;
}

void WiFiManager::startConfigPortal() {
 ESP_LOGI(TAG, "Starting Config Portal...");

 configMode = true;

 WiFi.mode(WIFI_AP_STA);
 WiFi.disconnect(false);
 delay(100);

 WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
 updateApSsid();
 WiFi.softAP(apSsid.c_str(), NULL, 1, false, 4);

 delay(500);

 ESP_LOGI(TAG, "AP IP: %s", WiFi.softAPIP().toString().c_str());
 ESP_LOGI(TAG, "AP SSID: %s", WiFi.softAPSSID().c_str());

 apStarted = true;
 apStartTime = millis();

 scanNetworks();
 ESP_LOGI(TAG, "[WiFi] 预扫描完成：%d 个网络", networksFound);

 startPortalDns();
 startWebServer();
}

void WiFiManager::startWebServer() {
 if (webServer) {
  stopWebServer();
 }
 webServer = new WebServer(80);

 webServer->on("/", HTTP_GET, [this]() { handleRoot(); });
 webServer->on("/scan", HTTP_GET, [this]() { handleScan(); });
 webServer->on("/save", HTTP_POST, [this]() { handleSave(); });
 webServer->on("/forget", HTTP_POST, [this]() { handleForget(); });
 webServer->onNotFound([this]() { handleNotFound(); });

 webServer->begin();
 ESP_LOGI(TAG, "Web server started");
}

void WiFiManager::handleRoot() {
 String html = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
 <meta charset="UTF-8">
 <meta name="viewport" content="width=device-width,initial-scale=1.0">
 <title>网络配置 & OTA</title>
 <style>
  :root {
   --accent: #2563eb; --accent-hover: #1d4ed8; --accent-soft: rgba(37,99,235,.10);
   --bg-grad-1: #eef2ff; --bg-grad-2: #e0f2fe;
   --surface: #ffffff; --surface-muted: #f5f7fb;
   --border: #e2e8f0; --border-strong: #cbd5e1;
   --text: #0f172a; --text-muted: #64748b;
   --danger: #dc2626; --success: #16a34a;
   --radius: 14px; --radius-sm: 10px;
   --shadow: 0 10px 30px rgba(15,23,42,.08), 0 2px 6px rgba(15,23,42,.04);
  }
  @media (prefers-color-scheme: dark) {
   :root {
    --accent: #60a5fa; --accent-hover: #93c5fd; --accent-soft: rgba(96,165,250,.16);
    --bg-grad-1: #0b1220; --bg-grad-2: #111827;
    --surface: #1e293b; --surface-muted: #18233a;
    --border: #334155; --border-strong: #475569;
    --text: #e2e8f0; --text-muted: #94a3b8;
    --danger: #f87171; --success: #4ade80;
    --shadow: 0 10px 30px rgba(0,0,0,.35), 0 2px 6px rgba(0,0,0,.25);
   }
  }
  * { box-sizing: border-box; }
  body { margin: 0; min-height: 100vh; padding: 20px; color: var(--text);
   background: linear-gradient(160deg, var(--bg-grad-1), var(--bg-grad-2));
   font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "PingFang SC", "Microsoft YaHei", sans-serif; }
  .page { max-width: 460px; margin: 0 auto; }
  .app-header { display: flex; align-items: center; gap: 10px; margin: 2px 2px 14px; }
  .logo { width: 34px; height: 34px; border-radius: 10px; background: var(--accent-soft);
   color: var(--accent); display: flex; align-items: center; justify-content: center; }
  .logo svg { width: 20px; height: 20px; }
  .app-header h1 { margin: 0; font-size: 18px; }
  .card { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius);
   box-shadow: var(--shadow); padding: 18px; }
  .section + .section { margin-top: 18px; padding-top: 16px; border-top: 1px solid var(--border); }
  h3 { margin: 0 0 10px; font-size: 14px; font-weight: 600; color: var(--text-muted); }
  ul.saved { list-style: none; margin: 0; padding: 0; display: grid; gap: 8px; }
  ul.saved li { display: flex; align-items: center; justify-content: space-between; gap: 10px;
   background: var(--surface-muted); border: 1px solid var(--border); border-radius: var(--radius-sm); padding: 10px 12px; }
  .ssid-name { font-weight: 600; font-size: 14px; word-break: break-all; }
  .pass-hint { margin-top: 2px; font-size: 12px; color: var(--text-muted); }
  .empty { justify-content: center !important; color: var(--text-muted); font-size: 13px; }
  button { cursor: pointer; font-family: inherit; border-radius: var(--radius-sm); }
  button.del { background: transparent; color: var(--danger); border: 1px solid var(--border); padding: 6px 10px; font-size: 12px; }
  .field { margin-bottom: 12px; }
  label { display: block; margin: 0 0 6px; font-size: 13px; color: var(--text-muted); }
  input { width: 100%; padding: 11px 12px; font-size: 15px; color: var(--text);
   background: var(--surface); border: 1px solid var(--border-strong); border-radius: var(--radius-sm); }
  input:focus { outline: none; border-color: var(--accent); box-shadow: 0 0 0 3px var(--accent-soft); }
  .row { display: flex; gap: 10px; }
  button.primary { flex: 1; padding: 12px; font-size: 15px; font-weight: 600; color: #fff;
   background: var(--accent); border: none; }
  button.primary:hover { background: var(--accent-hover); }
  button.ghost { padding: 10px 12px; font-size: 13px; color: var(--text-muted);
   background: transparent; border: 1px solid var(--border-strong); }
  #status { margin-top: 10px; min-height: 18px; font-size: 13px; color: var(--text-muted); }
  #status.err { color: var(--danger); }
  #status.ok { color: var(--success); }
  #ap_list { margin-top: 14px; display: grid; gap: 6px; }
  #ap_list p { margin: 0; font-size: 13px; color: var(--text-muted); }
  .ap { display: flex; align-items: center; justify-content: space-between; gap: 10px;
   padding: 10px 12px; font-size: 14px; color: var(--text); text-decoration: none;
   background: var(--surface-muted); border: 1px solid var(--border); border-radius: var(--radius-sm); }
  .ap:hover, .ap:active { border-color: var(--accent); }
  .ap .ssid { word-break: break-all; }
  .ap .meta { flex-shrink: 0; font-size: 12px; color: var(--text-muted); }
  .ota-info { font-size: 13px; color: var(--text-muted); margin-bottom: 10px; }
  .ota-info strong { color: var(--text); }
  #ota_progress { margin-top: 12px; }
  #ota_progress div { height: 8px; background: var(--surface-muted); border-radius: 4px; overflow: hidden; }
  #ota_progress span { display: block; height: 100%; background: var(--accent); width: 0%; transition: width .3s; }
  #ota_progress p { font-size: 12px; color: var(--text-muted); margin: 6px 0 0; }
 </style>
</head>
<body>
 <div class="page">
  <header class="app-header">
   <span class="logo">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
     <path d="M5 12.55a11 11 0 0 1 14.08 0"/>
     <path d="M1.42 9a16 16 0 0 1 21.16 0"/>
     <path d="M8.53 16.11a6 6 0 0 1 6.95 0"/>
     <line x1="12" y1="20" x2="12.01" y2="20"/>
    </svg>
   </span>
   <h1>网络配置 & OTA</h1>
  </header>
  <div class="card">
   <div class="section">
    <h3>已保存的 Wi-Fi</h3>
    <ul class="saved" id="savedList">)HTML";

 for (int i = 0; i < credentialCount; i++) {
  String ssid = savedCredentials[i].ssid;
  String escapedSsid = ssid;
  escapedSsid.replace("\"", "\\\"");
  escapedSsid.replace("'", "\\'");
  String maskedPass;
  for (int j = 0; j < (int)savedCredentials[i].password.length(); j++) {
   maskedPass += "*";
  }
  html += "<li><div><div class='ssid-name'>" + ssid + "</div><div class='pass-hint'>密码: " + maskedPass + "</div></div><button class='del' onclick=\"forgetWifi('" + escapedSsid + "')\">删除</button></li>";
 }

 if (credentialCount == 0) {
  html += "<li class='empty'>暂无已保存的 Wi-Fi</li>";
 }

 html += R"HTML(</ul>
   </div>
   <div class="section">
    <h3>连接到新的 Wi-Fi</h3>
    <div class="field">
     <label for="ssid">SSID</label>
     <input id="ssid" readonly placeholder="点击下方 Wi-Fi 列表选择">
    </div>
    <div class="field">
     <label for="password">密码</label>
     <input id="password" type="password" placeholder="开放网络可留空">
    </div>
    <div class="row">
     <button class="primary" onclick="saveConfig()">连接</button>
     <button class="ghost" onclick="loadAPList(true)">重新扫描</button>
    </div>
    <div id="status"></div>
    <div id="ap_list"><p>正在扫描周边 Wi-Fi...</p></div>
   </div>
   <div class="section">
    <h3>固件升级</h3>
    <div class="ota-info">当前版本: <strong id="cur_ver">--</strong></div>
    <form id="ota_form" onsubmit="doUpdate(event)">
     <div class="field">
      <input type="file" id="ota_file" name="update" accept=".bin" required>
     </div>
     <button class="primary" type="submit" id="ota_btn">上传固件</button>
    </form>
    <div id="ota_progress"><div><span></span></div><p id="ota_text"></p></div>
   </div>
  </div>
 </div>
 <script>
  let scanning = false;
  let hasList = false;

  function loadAPList(rescan) {
   if (scanning) return;
   scanning = true;
   const box = document.getElementById('ap_list');
   if (!hasList) box.innerHTML = '<p>正在扫描周边 Wi-Fi...</p>';

   fetch('/scan' + (rescan ? '?rescan=1' : ''))
    .then(r => r.json())
    .then(list => {
     hasList = true;
     box.innerHTML = '';
     const tip = document.createElement('p');
     tip.textContent = list.length ? '选择要连接的 Wi-Fi：' : '未发现周边 Wi-Fi，请点击"重新扫描"';
     box.appendChild(tip);

     list.sort((a, b) => b.rssi - a.rssi).forEach(ap => {
      const item = document.createElement('a');
      item.className = 'ap';
      item.href = '#';
      const name = document.createElement('span');
      name.className = 'ssid';
      name.textContent = ap.ssid || '(隐藏网络)';
      const meta = document.createElement('span');
      meta.className = 'meta';
      meta.textContent = (ap.authmode === 0 ? '🌐 ' : '🔒 ') + ap.rssi + ' dBm';
      item.appendChild(name);
      item.appendChild(meta);
      item.onclick = e => {
       e.preventDefault();
       document.getElementById('ssid').value = ap.ssid;
      };
      box.appendChild(item);
     });
     scanning = false;
    })
    .catch(() => {
     box.innerHTML = '<p>扫描失败，请点击"重新扫描"</p>';
     scanning = false;
    });
  }

  function saveConfig() {
   const ssid = document.getElementById('ssid').value;
   const password = document.getElementById('password').value;
   const st = document.getElementById('status');
   if (!ssid) {
    st.className = 'err';
    st.textContent = '请先选择 Wi-Fi 网络';
    return;
   }
   st.className = '';
   st.textContent = '正在保存，设备将重启…';
   const fd = new FormData();
   fd.append('ssid', ssid);
   fd.append('password', password);
   fetch('/save', { method: 'POST', body: fd })
    .then(r => r.text())
    .then(() => {
     st.className = 'ok';
     st.innerHTML = '配置已保存，设备正在重启<br><small>请重新连接 Wi-Fi</small>';
    })
    .catch(() => {
     st.className = 'err';
     st.textContent = '保存失败，请重试';
    });
  }

  function forgetWifi(ssid) {
   if (!confirm('确定删除 ' + ssid + ' 的配置？')) return;
   fetch('/forget?ssid=' + encodeURIComponent(ssid), { method: 'POST' })
    .then(() => location.reload())
    .catch(() => alert('删除失败'));
  }

  function doUpdate(e) {
   e.preventDefault();
   const file = document.getElementById('ota_file').files[0];
   if (!file) return;
   if (!confirm('确定上传 ' + file.name + ' 吗？更新中请勿断电。')) return;

   const btn = document.getElementById('ota_btn');
   const bar = document.querySelector('#ota_progress span');
   const txt = document.getElementById('ota_text');
   btn.disabled = true; btn.textContent = '升级中...';
   bar.style.width = '0%'; txt.textContent = '0%';

   const fd = new FormData();
   fd.append('update', file);

   fetch('/update', { method: 'POST', body: fd })
    .then(r => r.text().then(t => ({ ok: r.ok, text: t })))
    .then(({ ok, text }) => {
     if (ok) {
      bar.style.width = '100%'; txt.textContent = '完成！设备正在重启...';
     } else {
      txt.textContent = '失败: ' + text; btn.disabled = false; btn.textContent = '重试';
     }
    })
    .catch(err => { txt.textContent = '网络错误: ' + err; btn.disabled = false; btn.textContent = '重试'; });

   // 轮询进度（如果服务端支持进度查询，这里可以加 /ota_status 轮询）
   let prog = 0;
   const iv = setInterval(() => {
    prog += 2;
    if (prog >= 90 || btn.disabled === false) clearInterval(iv);
    bar.style.width = prog + '%'; txt.textContent = prog + '%';
   }, 800);
  }

  document.addEventListener('DOMContentLoaded', () => {
   loadAPList(false);
   fetch('/version').then(r => r.text()).then(t => document.getElementById('cur_ver').textContent = t).catch(()=>{});
  });
 </script>
</body>
</html>
)HTML";

 webServer->send(200, "text/html", html);
}

void WiFiManager::handleScan() {
 bool rescan = webServer->hasArg("rescan") || !scanComplete;
 if (rescan) {
  ESP_LOGI(TAG, "Scanning networks...");
  scanNetworks();
  ESP_LOGI(TAG, "Scan done: %d networks", networksFound);
 }

 String json = "[";
 for (int i = 0; i < networksFound; i++) {
  if (i > 0) json += ",";
  String ssid = WiFi.SSID(i);
  ssid.replace("\"", "\\\"");
  json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
          ",\"authmode\":" + String((int)WiFi.encryptionType(i)) + "}";
 }
 json += "]";

 webServer->send(200, "application/json", json);
}

void WiFiManager::handleSave() {
 String ssid = webServer->arg("ssid");
 String password = webServer->arg("password");

 ESP_LOGI(TAG, "Saving credentials...");
 ESP_LOGI(TAG, "Saved WiFi credentials, SSID: %s", ssid.c_str());

 saveCredentials(ssid, password);

 webServer->send(200, "text/plain", "OK");

 delay(1000);
 ESP.restart();
}

void WiFiManager::handleForget() {
 String ssid = webServer->arg("ssid");

 ESP_LOGI(TAG, "Forgetting credentials...");
 ESP_LOGI(TAG, "Forgot WiFi credentials, SSID: %s", ssid.c_str());

 forgetCredentials(ssid);

 webServer->send(200, "text/plain", "OK");
}

void WiFiManager::handleNotFound() {
 String url = "http://" + WiFi.softAPIP().toString() + "/";
 webServer->sendHeader("Location", url, true);
 webServer->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
 webServer->send(302, "text/html", "<html><body><a href='" + url + "'>WiFi 配置</a></body></html>");
}

void WiFiManager::connectToWiFi(const char* ssid, const char* password, int timeoutMs) {
 WiFi.mode(WIFI_STA);
 WiFi.begin(ssid, password);

 unsigned long startTime = millis();
 while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
  delay(500);
 }
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
 for (int i = 0; i < credentialCount; i++) {
  if (savedCredentials[i].ssid == ssid) {
   for (int j = i; j > 0; j--) {
    savedCredentials[j] = savedCredentials[j - 1];
   }
   savedCredentials[0].ssid = ssid;
   savedCredentials[0].password = password;
   goto save;
  }
 }

 if (credentialCount < MAX_WIFI_CREDENTIALS) {
  for (int i = credentialCount; i > 0; i--) {
   savedCredentials[i] = savedCredentials[i - 1];
  }
  savedCredentials[0].ssid = ssid;
  savedCredentials[0].password = password;
  credentialCount++;
 } else {
  for (int i = MAX_WIFI_CREDENTIALS - 1; i > 0; i--) {
   savedCredentials[i] = savedCredentials[i - 1];
  }
  savedCredentials[0].ssid = ssid;
  savedCredentials[0].password = password;
 }

save:
 preferences.putInt("count", credentialCount);
 for (int i = 0; i < credentialCount; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  preferences.putString(ssidKey.c_str(), savedCredentials[i].ssid);
  preferences.putString(passKey.c_str(), savedCredentials[i].password);
 }
 ESP_LOGI(TAG, "Saved %d credentials to NVS", credentialCount);
}

bool WiFiManager::loadCredentials() {
 credentialCount = preferences.getInt("count", 0);

 if (credentialCount == 0) {
  String oldSSID = preferences.getString("ssid", "");
  String oldPass = preferences.getString("password", "");
  if (oldSSID.length() > 0) {
   savedCredentials[0].ssid = oldSSID;
   savedCredentials[0].password = oldPass;
   credentialCount = 1;
   ESP_LOGI(TAG, "[WiFi] Migrated old SSID: %s", oldSSID.c_str());
  }
  return credentialCount > 0;
 }

 for (int i = 0; i < credentialCount && i < MAX_WIFI_CREDENTIALS; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  savedCredentials[i].ssid = preferences.getString(ssidKey.c_str(), "");
  savedCredentials[i].password = preferences.getString(passKey.c_str(), "");
 }

 ESP_LOGI(TAG, "Loaded %d credentials", credentialCount);
 return credentialCount > 0;
}

bool WiFiManager::hasSavedCredentials() { return credentialCount > 0; }

IPAddress WiFiManager::getIP() {
 if (configMode) return WiFi.softAPIP();
 return WiFi.localIP();
}

bool WiFiManager::isConfigMode() { return configMode; }

const char* WiFiManager::getConfigSSID() { return apSsid.c_str(); }

unsigned long WiFiManager::getAPStartTime() { return apStartTime; }

const WiFiCredential* WiFiManager::getSavedCredentials() const { return savedCredentials; }
int WiFiManager::getCredentialCount() const { return credentialCount; }

void WiFiManager::forgetCredentials(const String& ssid) {
 for (int i = 0; i < credentialCount; i++) {
  if (savedCredentials[i].ssid == ssid) {
   for (int j = i; j < credentialCount - 1; j++) {
    savedCredentials[j] = savedCredentials[j + 1];
   }
   credentialCount--;
   break;
  }
 }

 preferences.putInt("count", credentialCount);
 for (int i = 0; i < credentialCount; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  preferences.putString(ssidKey.c_str(), savedCredentials[i].ssid);
  preferences.putString(passKey.c_str(), savedCredentials[i].password);
 }

 for (int i = credentialCount; i < MAX_WIFI_CREDENTIALS; i++) {
  String ssidKey = "ssid_" + String(i);
  String passKey = "pass_" + String(i);
  preferences.remove(ssidKey.c_str());
  preferences.remove(passKey.c_str());
 }

 ESP_LOGI(TAG, "Forgetting SSID, %d credentials remaining", credentialCount);
}

void WiFiManager::scanNetworks() {
 networksFound = WiFi.scanNetworks(false, false);
 scanComplete = true;
}
