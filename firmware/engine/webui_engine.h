#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SD.h>

namespace Flic {

class WebUiEngine {
public:
    using DataProvider = String (*)();
    using JsonProvider = String (*)();
    using PathJsonProvider = String (*)(const String& pathArg);
    using ActionHandler = bool (*)(const String& requestJson, String& responseJson);

    bool begin(const char* ssid, const char* password, uint16_t httpPort = 80, uint16_t wsPort = 81);
    void update();

    void setDataProvider(DataProvider provider);
    void setStatusProvider(JsonProvider provider);
    void setSensorsProvider(JsonProvider provider);
    void setEnginesProvider(JsonProvider provider);
    void setSettingsHandler(ActionHandler handler);
    void setCommandHandler(ActionHandler handler);
    void setFaceSettingsProvider(JsonProvider provider);
    void setFaceSettingsHandler(ActionHandler handler);
    void setFaceStylesProvider(JsonProvider provider);
    void setFaceAnimationsCatalogProvider(JsonProvider provider);
    void setFaceAnimationsProvider(PathJsonProvider provider);
    void setFacePreviewHandler(ActionHandler handler);
    void setFaceSetStyleHandler(ActionHandler handler);
    void setFaceSetAnimationHandler(ActionHandler handler);
    void setFacePlayHandler(ActionHandler handler);
    void setFaceSetEmotionHandler(ActionHandler handler);
    void setFaceReloadHandler(ActionHandler handler);
    void setFaceValidateProvider(JsonProvider provider);
    void setFaceTelemetryProvider(JsonProvider provider);
    void setFaceTelemetryHandler(ActionHandler handler);
    void setFaceSnapshotPathProvider(DataProvider provider);
    void setLogsProvider(JsonProvider provider);

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
    void handleApiFaceSettingsGet();
    void handleApiFaceSettingsPost();
    void handleApiFaceStyles();
    void handleApiFaceAnimationsCatalog();
    void handleApiFaceAnimations();
    void handleApiFacePreview();
    void handleApiFaceSetStyle();
    void handleApiFaceSetAnimation();
    void handleApiFacePlay();
    void handleApiFaceSetEmotion();
    void handleApiFaceReload();
    void handleApiFaceValidate();
    void handleApiFaceTelemetry();
    void handleApiFaceTelemetryPost();
    void handleApiFaceSnapshot();
    void handleApiSdList();
    void handleApiSdUpload();
    void handleApiSdDelete();
    void handleApiSdMkdir();
    void handleApiSdRename();
    void handleApiSdDownload();
    void handleApiLogs();
    void handleApiLogsClear();
    void handleApiWildcard();
    void handleWsInfo();
    bool handleCaptivePortalRedirect();
    IPAddress captivePortalIp() const;
    String defaultStateJson() const;
    String defaultStatusJson() const;
    String defaultSensorsJson() const;
    String defaultEnginesJson() const;
    String readRequestBody() const;
    String sanitizeSdPath(const String& input) const;
    bool isSafeSdPath(const String& path) const;
    String buildSdTreeJson(const String& path, uint8_t depth) const;
    bool validatePngUpload(const String& path, String& reason) const;
    void logSdOperation(const String& op, const String& path, bool ok, const String& detail = String()) const;

    DNSServer* dnsServer_ = nullptr;
    WebServer* httpServer_ = nullptr;
    WebSocketsServer* wsServer_ = nullptr;

    DataProvider dataProvider_ = nullptr;
    JsonProvider statusProvider_ = nullptr;
    JsonProvider sensorsProvider_ = nullptr;
    JsonProvider enginesProvider_ = nullptr;
    ActionHandler settingsHandler_ = nullptr;
    ActionHandler commandHandler_ = nullptr;
    JsonProvider faceSettingsProvider_ = nullptr;
    ActionHandler faceSettingsHandler_ = nullptr;
    JsonProvider faceStylesProvider_ = nullptr;
    JsonProvider faceAnimationsCatalogProvider_ = nullptr;
    PathJsonProvider faceAnimationsProvider_ = nullptr;
    ActionHandler facePreviewHandler_ = nullptr;
    ActionHandler faceSetStyleHandler_ = nullptr;
    ActionHandler faceSetAnimationHandler_ = nullptr;
    ActionHandler facePlayHandler_ = nullptr;
    ActionHandler faceSetEmotionHandler_ = nullptr;
    ActionHandler faceReloadHandler_ = nullptr;
    JsonProvider faceValidateProvider_ = nullptr;
    JsonProvider faceTelemetryProvider_ = nullptr;
    ActionHandler faceTelemetryHandler_ = nullptr;
    DataProvider faceSnapshotPathProvider_ = nullptr;
    JsonProvider logsProvider_ = nullptr;

    String uploadTargetPath_;
    File uploadFile_;
    size_t uploadBytesReceived_ = 0;
    bool uploadRejected_ = false;
    String uploadRejectReason_;

    bool ready_ = false;
    bool wifiConnected_ = false;
    bool apMode_ = false;
    bool setupApActive_ = false;
    uint32_t lastBroadcastMs_ = 0;
    uint16_t wsPort_ = 81;
};

}  // namespace Flic
