#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

#define MAX_WIFI_CREDENTIALS 5

typedef struct {
    String ssid;
    String password;
} WiFiCredential;

class WiFiManager {
public:
    WiFiManager();
    bool connect();
    void maintainConnection();
    bool isConnected();
    int getReconnectCount();
    int getRSSI();
    const char* getLocalIP();
    const char* getSSID();
    bool isAPMode();
    void handleClient();
    void startAPMode();
    void stopAPMode();
    bool isAPStarted();
    bool hasInitialConnected() const;

    void begin();
    bool autoConnect();
    void startConfigPortal();
    void saveCredentials(const String& ssid, const String& password);
    bool loadCredentials();
    bool hasSavedCredentials();
    IPAddress getIP();
    bool isConfigMode();
    const char* getConfigSSID();
    unsigned long getAPStartTime();
    const WiFiCredential* getSavedCredentials() const;
    int getCredentialCount() const;
    void forgetCredentials(const String& ssid);

    WebServer* getWebServer() { return webServer; }
    void scanNetworks();

private:
    void startWebServer();
    void stopWebServer();
    void startPortalDns();
    void stopPortalDns();
    void updateApSsid();
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleForget();
    void handleNotFound();
    void connectToWiFi(const char* ssid, const char* password, int timeoutMs);
    void handleSmartConfig();
    void startConfigMode();
    void handleConfigMode();
    void handleReconnect();
    bool connectSavedIndex(int idx, unsigned long timeoutMs);
    void onConnected();

    Preferences preferences;
    WebServer* webServer;
    DNSServer dnsServer;
    bool dnsStarted;
    String apSsid;
    bool configMode;
    WiFiCredential savedCredentials[MAX_WIFI_CREDENTIALS];
    int credentialCount;
    bool scanComplete;
    int8_t networksFound;
    bool apStarted;
    unsigned long apStartTime;
    int reconnectCount;
    bool initialized;
    bool _initialConnected;
    bool smartConfigStarted;
    bool smartConfigDone;
    unsigned long smartConfigStartTime;

    static const unsigned long SMART_CONFIG_TIMEOUT_MS = 30 * 1000UL;
    static const unsigned long CONFIG_MODE_TIMEOUT_MS = 10 * 60 * 1000UL;
    static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20 * 1000UL;
    unsigned long _configModeStartTime;
    bool _configDisabled;

    static const int RECONNECT_RETRY_MAX = 5;
    static const unsigned long RECONNECT_RETRY_INTERVAL_MS = 10 * 1000UL;
    static const unsigned long RECONNECT_WAIT_MS = 5 * 60 * 1000UL;
    int _reconnectAttempts;
    unsigned long _reconnectWaitUntil;

    bool _netReady;
    int _activeCredIndex;
};

#endif
