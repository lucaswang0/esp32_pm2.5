#include "OtaManager.h"
#include "config.h"
#include <esp_log.h>

static const char* TAG = "OTA";
static const unsigned long OTA_CHECK_INTERVAL_MS = 12 * 60 * 60 * 1000UL;  // 12 小时

OtaManager::OtaManager() : _server(nullptr), _busy(false), _lastCheckMs(0), _httpProgress(0) {}

void OtaManager::begin(WebServer* server) {
    _server = server;
    if (!server) return;

    server->on("/version", HTTP_GET, [this]() { handleVersion(); });
    server->on("/update", HTTP_POST,
        [this]() { handleUpdateEnd(); },
        [this]() { handleUpdate(); });

    ESP_LOGI(TAG, "OTA routes registered: /version, /update");
}

void OtaManager::handleVersion() {
    if (!_server) return;
    _server->send(200, "text/plain", FIRMWARE_VERSION);
}

void OtaManager::handleUpdate() {
    HTTPUpload& up = _server->upload();

    if (up.status == UPLOAD_FILE_START) {
        ESP_LOGI(TAG, "[手动OTA] 开始上传: %s (%u bytes)", up.filename.c_str(), up.totalSize);
        if (!Update.begin(up.totalSize)) {
            Update.printError(Serial);
        }
        _busy = true;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (_busy && !Update.write(up.buf, up.currentSize)) {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (_busy) {
            _busy = false;
            if (Update.end(true)) {
                ESP_LOGI(TAG, "[手动OTA] 写入完成，重启中...");
            } else {
                Update.printError(Serial);
            }
        }
    }
}

void OtaManager::handleUpdateEnd() {
    if (!_server) return;

    if (Update.hasError()) {
        String err = Update.errorString();
        ESP_LOGE(TAG, "[手动OTA] 失败: %s", err.c_str());
        _server->send(500, "text/plain", "OTA failed: " + err);
    } else {
        _server->send(200, "text/plain", "OK - rebooting");
        delay(1500);
        ESP.restart();
    }
}

// ---------- 自动升级 ----------

void OtaManager::maintain() {
    if (_busy) return;
    unsigned long now = millis();
    if (_lastCheckMs == 0) {
        // 开机后首次检查：延迟 30s 等 WiFi 稳定
        _lastCheckMs = now + 30000 - OTA_CHECK_INTERVAL_MS;
    }
    if (now - _lastCheckMs >= OTA_CHECK_INTERVAL_MS) {
        _lastCheckMs = now;
        checkAndUpdate();
    }
}

void OtaManager::checkAndUpdate() {
    if (_busy) return;
    if (!WiFi.isConnected()) return;

    ESP_LOGI(TAG, "[自动OTA] 检查更新: %s", OTA_CHECK_URL);

    String json = httpGetString(OTA_CHECK_URL);
    if (json.length() == 0) {
        ESP_LOGW(TAG, "[自动OTA] 获取 version.json 失败");
        return;
    }

    String newVer = extractField(json, "version");
    String binUrl = extractField(json, "url");

    if (newVer.length() == 0 || binUrl.length() == 0) {
        ESP_LOGW(TAG, "[自动OTA] version.json 字段缺失: %s", json.c_str());
        return;
    }

    ESP_LOGI(TAG, "[自动OTA] 远程版本: %s, 当前: %s", newVer.c_str(), FIRMWARE_VERSION);

    int cmp = compareVersions(newVer, FIRMWARE_VERSION);
    if (cmp <= 0) {
        ESP_LOGI(TAG, "[自动OTA] 已是最新或相同版本，跳过");
        return;
    }

    ESP_LOGW(TAG, "[自动OTA] 发现新版本 %s -> 开始下载 %s", newVer.c_str(), binUrl.c_str());
    _busy = true;
    doAutoUpdate(binUrl);
}

bool OtaManager::doAutoUpdate(const String& binUrl) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!http.begin(binUrl)) {
        ESP_LOGE(TAG, "[自动OTA] 无法打开: %s", binUrl.c_str());
        _busy = false;
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        ESP_LOGE(TAG, "[自动OTA] HTTP %d", code);
        http.end();
        _busy = false;
        return false;
    }

    int totalSize = http.getSize();
    WiFiClient* stream = http.getStreamPtr();

    ESP_LOGI(TAG, "[自动OTA] 固件大小: %d bytes, 开始刷写", totalSize);

    if (!Update.begin(totalSize)) {
        Update.printError(Serial);
        http.end();
        _busy = false;
        return false;
    }

    uint8_t buf[2048];
    size_t written = 0;
    unsigned long lastReport = 0;

    while (stream->available()) {
        size_t n = stream->readBytes(buf, sizeof(buf));
        if (n > 0) {
            Update.write(buf, n);
            written += n;

            unsigned long now = millis();
            if (now - lastReport >= 2000) {
                int pct = totalSize > 0 ? (int)(written * 100 / totalSize) : 0;
                ESP_LOGI(TAG, "[自动OTA] 进度: %d%% (%u/%d)", pct, (unsigned)written, totalSize);
                lastReport = now;
            }
        }
    }

    if (Update.end(true)) {
        ESP_LOGI(TAG, "[自动OTA] ✅ 升级成功，3 秒后重启");
        http.end();
        delay(3000);
        ESP.restart();
        return true;
    } else {
        ESP_LOGE(TAG, "[自动OTA] ❌ Update.end 失败");
        Update.printError(Serial);
        http.end();
        _busy = false;
        return false;
    }
}

String OtaManager::httpGetString(const String& url) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!http.begin(url)) return "";
    int code = http.GET();
    if (code != 200) { http.end(); return ""; }
    String result = http.getString();
    http.end();
    return result;
}

String OtaManager::extractField(const String& json, const String& key) {
    // 查找 "key":"value" 格式
    String search = "\"" + key + "\"";
    int idx = json.indexOf(search);
    if (idx < 0) return "";

    int colon = json.indexOf(':', idx + search.length());
    if (colon < 0) return "";

    // 跳过冒号后的空格
    int start = colon + 1;
    while (start < (int)json.length() && json[start] == ' ') start++;

    // value 以引号开头
    if (start >= (int)json.length() || json[start] != '"') return "";

    int valStart = start + 1;
    int valEnd = json.indexOf('"', valStart);
    if (valEnd < 0) return "";

    return json.substring(valStart, valEnd);
}

int OtaManager::compareVersions(const String& a, const String& b) {
    // YYYY.MM.DD 格式，字符串字典序 == 时间序
    if (a == b) return 0;
    return a > b ? 1 : -1;
}
