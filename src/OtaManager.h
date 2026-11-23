#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include <HTTPClient.h>

class OtaManager {
public:
    OtaManager();
    void begin(WebServer* server);
    void checkAndUpdate();
    void maintain();
    bool isBusy() const { return _busy; }

private:
    WebServer* _server;
    bool _busy;
    unsigned long _lastCheckMs;
    int _httpProgress;

    void handleUpdate();
    void handleUpdateEnd();
    void handleVersion();

    bool doAutoUpdate(const String& binUrl);
    String httpGetString(const String& url);
    static String extractField(const String& json, const String& key);
    int compareVersions(const String& a, const String& b);
};

#endif
