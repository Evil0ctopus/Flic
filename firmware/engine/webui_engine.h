#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

namespace Flic {

class WebUiEngine {
public:
    using DataProvider = String (*)();
    using JsonProvider = String (*)();
    using ActionHandler = bool (*)(const String& requestJson, String& responseJson);

    bool begin(const char* ssid, const char* password, uint16_t httpPort = 80, uint16_t wsPort = 81);
    void update();

    void setDataProvider(DataProvider provider);
    void setStatusProvider(JsonProvider provider);
    void setSensorsProvider(JsonProvider provider);
    void setEnginesProvider(JsonProvider provider);
    void setSettingsHandler(ActionHandler handler);
    void setCommandHandler(ActionHandler handler);

    void broadcastJson(const String& json);
    void sendEvent(const String& type, const String& payload);
    void publishHeartbeat(uint32_t uptimeMs, const String& emotion, bool animationPlaying);

    bool isReady() const;
    bool wifiConnected() const;
    bool apMode() const;
    IPAddress localIp() const;

private:
    void configureRoutes();
    void handleRoot();
    void handleApiHealth();
    void handleApiState();
    void handleApiStatus();
    void handleApiSensors();
    void handleApiEngines();
    void handleApiSettings();
    void handleApiCommand();
    void handleApiWildcard();
    void handleWsInfo();
    bool handleCaptivePortalRedirect();
    IPAddress captivePortalIp() const;
    String defaultStateJson() const;
    String defaultStatusJson() const;
    String defaultSensorsJson() const;
    String defaultEnginesJson() const;
    String readRequestBody() const;

    DNSServer* dnsServer_ = nullptr;
    WebServer* httpServer_ = nullptr;
    WebSocketsServer* wsServer_ = nullptr;

    DataProvider dataProvider_ = nullptr;
    JsonProvider statusProvider_ = nullptr;
    JsonProvider sensorsProvider_ = nullptr;
    JsonProvider enginesProvider_ = nullptr;
    ActionHandler settingsHandler_ = nullptr;
    ActionHandler commandHandler_ = nullptr;

    bool ready_ = false;
    bool wifiConnected_ = false;
    bool apMode_ = false;
    bool setupApActive_ = false;
    uint32_t lastBroadcastMs_ = 0;
    uint16_t wsPort_ = 81;
};

}  // namespace Flic
