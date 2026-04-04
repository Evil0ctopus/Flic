#include "webui_engine.h"

#include "../subsystems/sd_manager.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SD.h>

#include <algorithm>
#include <vector>

namespace Flic {
namespace {
// Embedded runtime WebUI is the current source of truth; webui/ is archive/reference only.
constexpr uint32_t kWifiConnectTimeoutMs = 10000;
constexpr uint32_t kBroadcastIntervalMs = 1000;
constexpr const char* kFallbackApSsid = "Flic-Setup";
// Development-only fallback password; change this for real deployments.
constexpr const char* kFallbackApPassword = "flic-dev-only";
constexpr const char* kHostname = "flic";
constexpr uint16_t kDnsPort = 53;

String escapeJson(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < static_cast<size_t>(input.length()); ++i) {
        const char c = input.charAt(i);
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '"') {
            out += "\\\"";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out += c;
        }
    }
    return out;
}

bool looksLikeJsonLiteral(const String& value) {
    if (value.length() == 0) {
        return false;
    }
    const char first = value.charAt(0);
    return first == '{' || first == '[' || first == '"' || first == '-' || (first >= '0' && first <= '9') ||
           value == "true" || value == "false" || value == "null";
}
}

bool WebUiEngine::begin(const char* ssid, const char* password, uint16_t httpPort, uint16_t wsPort) {
    ready_ = false;
    wifiConnected_ = false;
    apMode_ = false;
    setupApActive_ = false;

    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }

    WiFi.mode(WIFI_MODE_NULL);
    WiFi.setHostname(kHostname);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password == nullptr ? "" : password);

    const unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < kWifiConnectTimeoutMs) {
        delay(100);
    }

    wifiConnected_ = (WiFi.status() == WL_CONNECTED);
    if (!wifiConnected_) {
        WiFi.disconnect(true, true);
        delay(120);
        WiFi.mode(WIFI_AP);
        WiFi.softAPsetHostname(kHostname);
        WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
        if (!WiFi.softAP(kFallbackApSsid, kFallbackApPassword)) {
            return false;
        }
        apMode_ = true;
        setupApActive_ = true;
    } else {
        // Keep a setup SSID available even when STA is connected.
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPsetHostname(kHostname);
        setupApActive_ = WiFi.softAP(kFallbackApSsid, kFallbackApPassword);
    }

    if (!MDNS.begin(kHostname)) {
        // Non-fatal: direct IP still works.
    }
    MDNS.addService("http", "tcp", httpPort);

    if (httpServer_ != nullptr) {
        delete httpServer_;
        httpServer_ = nullptr;
    }
    if (wsServer_ != nullptr) {
        delete wsServer_;
        wsServer_ = nullptr;
    }
    if (dnsServer_ != nullptr) {
        delete dnsServer_;
        dnsServer_ = nullptr;
    }

    httpServer_ = new WebServer(httpPort);
    wsServer_ = new WebSocketsServer(wsPort);
    wsPort_ = wsPort;

    configureRoutes();
    httpServer_->begin();

    if (setupApActive_) {
        dnsServer_ = new DNSServer();
        dnsServer_->setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer_->start(kDnsPort, "*", captivePortalIp());
    }

    wsServer_->begin();
    wsServer_->onEvent([this](uint8_t clientId, WStype_t type, uint8_t* payload, size_t length) {
        if (type == WStype_CONNECTED) {
            (void)payload;
            (void)length;
            String state = defaultStateJson();
            wsServer_->sendTXT(clientId, state);
        } else if (type == WStype_TEXT) {
            String text;
            for (size_t i = 0; i < length; ++i) {
                text += static_cast<char>(payload[i]);
            }
            text.trim();
            if (text == "ping") {
                String pong = "{\"ok\":true,\"type\":\"pong\"}";
                wsServer_->sendTXT(clientId, pong);
            } else if (text == "state") {
                String state = defaultStateJson();
                wsServer_->sendTXT(clientId, state);
            }
        }
    });

    ready_ = true;
    lastBroadcastMs_ = millis();
    return true;
}

void WebUiEngine::update() {
    if (!ready_ || httpServer_ == nullptr || wsServer_ == nullptr) {
        return;
    }

    if (dnsServer_ != nullptr) {
        dnsServer_->processNextRequest();
    }

    httpServer_->handleClient();
    wsServer_->loop();

    const uint32_t now = millis();
    if ((now - lastBroadcastMs_) >= kBroadcastIntervalMs) {
        lastBroadcastMs_ = now;
        broadcastJson(dataProvider_ != nullptr ? dataProvider_() : defaultStateJson());
    }
}

void WebUiEngine::setDataProvider(DataProvider provider) {
    dataProvider_ = provider;
}

void WebUiEngine::setStatusProvider(JsonProvider provider) {
    statusProvider_ = provider;
}

void WebUiEngine::setSensorsProvider(JsonProvider provider) {
    sensorsProvider_ = provider;
}

void WebUiEngine::setEnginesProvider(JsonProvider provider) {
    enginesProvider_ = provider;
}

void WebUiEngine::setSettingsHandler(ActionHandler handler) {
    settingsHandler_ = handler;
}

void WebUiEngine::setCommandHandler(ActionHandler handler) {
    commandHandler_ = handler;
}

void WebUiEngine::setFaceSettingsProvider(JsonProvider provider) {
    faceSettingsProvider_ = provider;
}

void WebUiEngine::setFaceSettingsHandler(ActionHandler handler) {
    faceSettingsHandler_ = handler;
}

void WebUiEngine::setFaceStylesProvider(JsonProvider provider) {
    faceStylesProvider_ = provider;
}

void WebUiEngine::setFaceAnimationsCatalogProvider(JsonProvider provider) {
    faceAnimationsCatalogProvider_ = provider;
}

void WebUiEngine::setFaceAnimationsProvider(PathJsonProvider provider) {
    faceAnimationsProvider_ = provider;
}

void WebUiEngine::setFacePreviewHandler(ActionHandler handler) {
    facePreviewHandler_ = handler;
}

void WebUiEngine::setFaceSetStyleHandler(ActionHandler handler) {
    faceSetStyleHandler_ = handler;
}

void WebUiEngine::setFaceSetAnimationHandler(ActionHandler handler) {
    faceSetAnimationHandler_ = handler;
}

void WebUiEngine::setFacePlayHandler(ActionHandler handler) {
    facePlayHandler_ = handler;
}

void WebUiEngine::setFaceSetEmotionHandler(ActionHandler handler) {
    faceSetEmotionHandler_ = handler;
}

void WebUiEngine::setFaceReloadHandler(ActionHandler handler) {
    faceReloadHandler_ = handler;
}

void WebUiEngine::setFaceValidateProvider(JsonProvider provider) {
    faceValidateProvider_ = provider;
}

void WebUiEngine::setFaceTelemetryProvider(JsonProvider provider) {
    faceTelemetryProvider_ = provider;
}

void WebUiEngine::setFaceTelemetryHandler(ActionHandler handler) {
    faceTelemetryHandler_ = handler;
}

void WebUiEngine::setFaceSnapshotPathProvider(DataProvider provider) {
    faceSnapshotPathProvider_ = provider;
}

void WebUiEngine::setLogsProvider(JsonProvider provider) {
    logsProvider_ = provider;
}

void WebUiEngine::broadcastJson(const String& json) {
    if (!ready_ || wsServer_ == nullptr || json.length() == 0) {
        return;
    }

    String payload = json;
    wsServer_->broadcastTXT(payload);
}

void WebUiEngine::sendEvent(const String& type, const String& payload) {
    if (!ready_ || wsServer_ == nullptr || type.length() == 0) {
        return;
    }

    String message = "{\"type\":\"";
    message += escapeJson(type);
    message += "\",\"uptime_ms\":";
    message += String(millis());
    message += ",\"payload\":";

    if (looksLikeJsonLiteral(payload)) {
        message += payload;
    } else {
        message += "\"";
        message += escapeJson(payload);
        message += "\"";
    }

    message += "}";
    broadcastJson(message);
}

void WebUiEngine::publishHeartbeat(uint32_t uptimeMs, const String& emotion, bool animationPlaying) {
    String payload = "{\"type\":\"heartbeat\",\"uptime_ms\":";
    payload += String(uptimeMs);
    payload += ",\"emotion\":\"";
    payload += emotion;
    payload += "\",\"animation_playing\":";
    payload += animationPlaying ? "true" : "false";
    payload += "}";
    broadcastJson(payload);
}

bool WebUiEngine::isReady() const {
    return ready_;
}

bool WebUiEngine::wifiConnected() const {
    return wifiConnected_;
}

bool WebUiEngine::apMode() const {
    return apMode_;
}

IPAddress WebUiEngine::localIp() const {
    if (apMode_) {
        return WiFi.softAPIP();
    }
    return WiFi.localIP();
}

void WebUiEngine::configureRoutes() {
    if (httpServer_ == nullptr) {
        return;
    }

    httpServer_->on("/", HTTP_GET, [this]() { handleRoot(); });
    httpServer_->on("/api/health", HTTP_GET, [this]() { handleApiHealth(); });
    httpServer_->on("/api/state", HTTP_GET, [this]() { handleApiState(); });
    httpServer_->on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
    httpServer_->on("/api/sensors", HTTP_GET, [this]() { handleApiSensors(); });
    httpServer_->on("/api/engines", HTTP_GET, [this]() { handleApiEngines(); });
    httpServer_->on("/api/settings", HTTP_POST, [this]() { handleApiSettings(); });
    httpServer_->on("/api/command", HTTP_POST, [this]() { handleApiCommand(); });
    httpServer_->on("/api/face/settings", HTTP_GET, [this]() { handleApiFaceSettingsGet(); });
    httpServer_->on("/api/face/settings", HTTP_POST, [this]() { handleApiFaceSettingsPost(); });
    httpServer_->on("/api/face/styles", HTTP_GET, [this]() { handleApiFaceStyles(); });
    httpServer_->on("/api/face/animations", HTTP_GET, [this]() { handleApiFaceAnimationsCatalog(); });
    httpServer_->on("/api/face/preview", HTTP_POST, [this]() { handleApiFacePreview(); });
    httpServer_->on("/api/face/set_style", HTTP_POST, [this]() { handleApiFaceSetStyle(); });
    httpServer_->on("/api/face/set_animation", HTTP_POST, [this]() { handleApiFaceSetAnimation(); });
    httpServer_->on("/api/face/play", HTTP_POST, [this]() { handleApiFacePlay(); });
    httpServer_->on("/api/face/set_emotion", HTTP_POST, [this]() { handleApiFaceSetEmotion(); });
    httpServer_->on("/api/face/reload", HTTP_POST, [this]() { handleApiFaceReload(); });
    httpServer_->on("/api/face/validate", HTTP_GET, [this]() { handleApiFaceValidate(); });
    httpServer_->on("/api/face/telemetry", HTTP_GET, [this]() { handleApiFaceTelemetry(); });
    httpServer_->on("/api/face/telemetry", HTTP_POST, [this]() { handleApiFaceTelemetryPost(); });
    httpServer_->on("/api/face/snapshot", HTTP_GET, [this]() { handleApiFaceSnapshot(); });
    httpServer_->on("/api/sd/list", HTTP_GET, [this]() { handleApiSdList(); });
    httpServer_->on("/api/sd/upload", HTTP_POST,
                    [this]() { handleApiSdUpload(); },
                    [this]() {
                        if (httpServer_ == nullptr) {
                            return;
                        }

                        HTTPUpload& upload = httpServer_->upload();
                        if (upload.status == UPLOAD_FILE_START) {
                            uploadRejected_ = false;
                            uploadRejectReason_ = "";
                            uploadBytesReceived_ = 0;
                            const String reqPath = sanitizeSdPath(httpServer_->arg("path"));
                            String fileName = upload.filename;
                            fileName.replace("\\", "_");
                            fileName.replace("/", "_");
                            uploadTargetPath_ = reqPath;
                            if (!uploadTargetPath_.endsWith("/")) {
                                uploadTargetPath_ += "/";
                            }
                            uploadTargetPath_ += fileName;

                            if (!isSafeSdPath(uploadTargetPath_) || !Flic::SdManager::isMounted()) {
                                uploadRejected_ = true;
                                uploadRejectReason_ = "invalid_target_or_sd_missing";
                                return;
                            }

                            uploadFile_ = SD.open(uploadTargetPath_, FILE_WRITE);
                            if (!uploadFile_) {
                                uploadRejected_ = true;
                                uploadRejectReason_ = "failed_to_open_target";
                                return;
                            }
                            logSdOperation("upload_start", uploadTargetPath_, true);
                        } else if (upload.status == UPLOAD_FILE_WRITE) {
                            if (!uploadRejected_ && uploadFile_) {
                                uploadFile_.write(upload.buf, upload.currentSize);
                                uploadBytesReceived_ += upload.currentSize;
                                yield();
                            }
                        } else if (upload.status == UPLOAD_FILE_END) {
                            if (uploadFile_) {
                                uploadFile_.close();
                            }
                            if (!uploadRejected_) {
                                if (uploadTargetPath_.endsWith(".png")) {
                                    String reason;
                                    if (!validatePngUpload(uploadTargetPath_, reason)) {
                                        SD.remove(uploadTargetPath_);
                                        uploadRejected_ = true;
                                        uploadRejectReason_ = reason;
                                    }
                                }
                            }
                            logSdOperation("upload_end", uploadTargetPath_, !uploadRejected_, uploadRejectReason_);
                        } else if (upload.status == UPLOAD_FILE_ABORTED) {
                            if (uploadFile_) {
                                uploadFile_.close();
                            }
                            if (uploadTargetPath_.length() > 0 && SD.exists(uploadTargetPath_)) {
                                SD.remove(uploadTargetPath_);
                            }
                            uploadRejected_ = true;
                            uploadRejectReason_ = "upload_aborted";
                            logSdOperation("upload_abort", uploadTargetPath_, false, uploadRejectReason_);
                        }
                    });
    httpServer_->on("/api/sd/delete", HTTP_POST, [this]() { handleApiSdDelete(); });
    httpServer_->on("/api/sd/mkdir", HTTP_POST, [this]() { handleApiSdMkdir(); });
    httpServer_->on("/api/sd/rename", HTTP_POST, [this]() { handleApiSdRename(); });
    httpServer_->on("/api/sd/download", HTTP_GET, [this]() { handleApiSdDownload(); });
    httpServer_->on("/api/logs", HTTP_GET, [this]() { handleApiLogs(); });
    httpServer_->on("/api/logs/clear", HTTP_POST, [this]() { handleApiLogsClear(); });
    httpServer_->on("/ws", HTTP_GET, [this]() { handleWsInfo(); });
    httpServer_->on("/generate_204", HTTP_GET, [this]() {
        if (!handleCaptivePortalRedirect()) {
            httpServer_->send(204, "text/plain", "");
        }
    });
    httpServer_->on("/hotspot-detect.html", HTTP_GET, [this]() {
        if (!handleCaptivePortalRedirect()) {
            handleRoot();
        }
    });
    httpServer_->on("/ncsi.txt", HTTP_GET, [this]() {
        if (!handleCaptivePortalRedirect()) {
            httpServer_->send(200, "text/plain", "Microsoft NCSI");
        }
    });
    httpServer_->on("/connecttest.txt", HTTP_GET, [this]() {
        if (!handleCaptivePortalRedirect()) {
            httpServer_->send(200, "text/plain", "Microsoft Connect Test");
        }
    });
    httpServer_->on("/redirect", HTTP_GET, [this]() {
        if (!handleCaptivePortalRedirect()) {
            handleRoot();
        }
    });

    httpServer_->onNotFound([this]() {
        if (httpServer_->uri().startsWith("/api/")) {
            if (httpServer_->uri().startsWith("/api/face/animations/")) {
                handleApiFaceAnimations();
                return;
            }
            handleApiWildcard();
            return;
        }
        if (!handleCaptivePortalRedirect()) {
            handleRoot();
        }
    });
}

bool WebUiEngine::handleCaptivePortalRedirect() {
    if (!setupApActive_ || httpServer_ == nullptr) {
        return false;
    }

    const String portalHost = captivePortalIp().toString();
    if (httpServer_->hostHeader() != portalHost && httpServer_->hostHeader() != portalHost + ":80") {
        httpServer_->sendHeader("Location", String("http://") + portalHost, true);
        httpServer_->send(302, "text/plain", "");
        return true;
    }
    return false;
}

IPAddress WebUiEngine::captivePortalIp() const {
    const IPAddress apIp = WiFi.softAPIP();
    if (apIp != IPAddress(0, 0, 0, 0)) {
        return apIp;
    }
    return localIp();
}

void WebUiEngine::handleRoot() {
    String html;
    html.reserve(9000);
    html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Flic Pager UI</title><style>";
    html += ":root{--bg:#05060A;--card:#0B0F1A;--line:#151B2C;--txt:#E5F0FF;--muted:#8fa2d8;--accent:#4FC3F7;--warn:#ffd166}";
    html += "*{box-sizing:border-box}body{margin:0;background:#05060A;color:var(--txt);font-family:system-ui,Segoe UI,sans-serif}";
    html += "header{display:flex;justify-content:space-between;align-items:center;padding:14px 16px;border-bottom:1px solid #233154;background:#0d1320cc;position:sticky;top:0;z-index:5}";
    html += "h1{font-size:16px;margin:0;letter-spacing:.4px}.pill{padding:4px 10px;border:1px solid var(--line);border-radius:999px;color:var(--muted);font-size:12px}";
    html += ".app-shell,.layout{display:grid;grid-template-columns:220px minmax(0,1fr);min-height:calc(100vh - 56px)}.sidebar{border-right:1px solid #151827;background:#080A10;padding:10px;position:sticky;top:56px;height:calc(100vh - 56px);overflow:auto}";
    html += ".logo{font-weight:700;letter-spacing:.4px;color:#E5F0FF;padding:8px 6px 12px}.sidebtn{position:relative;width:100%;text-align:left;margin:0 0 8px 0;padding:10px;border-radius:10px;border:1px solid #151827;background:#111524;color:#dbe6ff}.sidebtn:hover{background:#111524}.sidebtn.active{background:#1A2238;border-color:#2e3d64}.sidebtn.active::before{content:'';position:absolute;left:-1px;top:6px;bottom:6px;width:3px;background:#4FC3F7;border-radius:3px}";
    html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px;padding:12px}.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px}";
    html += "h2{font-size:14px;margin:0 0 10px;color:#a8b8e6}.stat{font-family:Consolas,monospace;font-size:14px;line-height:1.45}.mono{font-family:Consolas,monospace}";
    html += "button{cursor:pointer;border:1px solid #2f4476;background:#151B2C;color:#dfe8ff;border-radius:10px;padding:9px 10px;font-weight:600}button:hover{filter:brightness(1.08)}.btn-primary{background:#4FC3F7;color:#04131f;border-color:#4FC3F7}.btn-primary:hover{background:#6FD4FF}.btn-secondary{background:#151B2C}.btn-danger{background:#EF5350;border-color:#EF5350;color:#fff}";
    html += ".btns{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.btn-main{background:#17424b;border-color:#2a6f7d;color:#d9fff9}";
    html += "label{display:block;font-size:12px;color:var(--muted);margin:8px 0 4px}input[type=range],input[type=text]{width:100%}";
    html += "input[type=text]{background:#0a1120;color:#dbe6ff;border:1px solid #2a3a60;border-radius:8px;padding:8px}";
    html += "pre{white-space:pre-wrap;word-break:break-word;background:#060b16;padding:10px;border-radius:8px;min-height:130px;border:1px solid #1f2d4d}.main-panel{background:#05060A}.panel,.panelGroup{display:none}.panel.active,.panelGroup.active{display:grid}";
    html += ".row{display:flex;gap:8px;align-items:center}.chip{padding:3px 8px;border-radius:999px;border:1px solid #31456f;font-size:12px;color:#cfe3ff}.chip.ok{border-color:#2f875f;color:#93ffd0}.chip.warn{border-color:#8b7a30;color:#ffe58f}";
    html += ".filelist{display:grid;gap:6px}.fitem{display:grid;grid-template-columns:22px 1fr auto;gap:8px;align-items:center;padding:7px;border:1px solid #243350;border-radius:8px;background:#0b1323}.tiny{font-size:11px;color:#8fa2d8}.crumbs{display:flex;gap:6px;flex-wrap:wrap}.crumb{padding:3px 7px;border:1px solid #30466f;border-radius:999px;background:#0f1d35;font-size:12px;cursor:pointer}";
    html += ".toast{position:fixed;right:12px;bottom:12px;max-width:300px;background:#101a2d;border:1px solid #2d4e7f;padding:10px;border-radius:10px;display:none}.toast.show{display:block}.toast.err{border-color:#8a3e56}.collapser{cursor:pointer;display:flex;justify-content:space-between;align-items:center}";
    html += "@media(max-width:900px){.app-shell,.layout{grid-template-columns:1fr}.sidebar{position:static;height:auto;border-right:none;border-bottom:1px solid #233154}}";
    html += "small{color:var(--muted)}.ok{color:var(--accent)}.warn{color:var(--warn)}.bad{color:#ff7a7a}</style></head><body>";
    html += "<header><h1>Flic Pager UI</h1><div class='pill' id='connPill'>connecting...</div></header><div class='app-shell'><aside class='sidebar'><div class='logo'>Flic</div><nav><button data-panel='home' class='sidebtn active' id='nav-home' onclick=\"showPanel('home')\">Home</button><button data-panel='sd' class='sidebtn' id='nav-sd' onclick=\"showPanel('sd')\">SD Card Manager</button><button data-panel='face' class='sidebtn' id='nav-face' onclick=\"showPanel('face')\">Face Animation Tools</button><button data-panel='logs' class='sidebtn' id='nav-logs' onclick=\"showPanel('logs')\">Logs & Diagnostics</button></nav></aside><main class='main-panel'>";
    html += "<section class='grid panel active' id='panel-home'>";
    html += "<section class='card'><h2>Connection</h2><div class='row'><span id='sdMountedChip' class='chip'>SD: unknown</span><span id='sdSpaceChip' class='chip'>space: --</span></div><div id='netInfo' class='stat mono'>Loading...</div><small>If AP mode, connect to SSID <b>Flic-Setup</b> (PW: <b>flic-dev-only</b>) then open <b>http://192.168.4.1</b>.</small></section>";
    html += "<section class='card'><h2>Quick Pager Commands</h2><div class='btns'><button class='btn-main' onclick='sendCmd(\"show webui info\")'>Show WebUI Info</button><button onclick='sendCmd(\"hello\")'>Hello</button><button onclick='sendCmd(\"what can you do\")'>Capabilities</button><button onclick='sendCmd(\"what is your name\")'>Name</button></div><label>Custom command</label><input id='cmd' type='text' placeholder='Type a message for Flic'><button style='margin-top:8px;width:100%' onclick='sendCustom()'>Send</button></section>";
    html += "<section class='card'><h2>Device Mic</h2><button onclick='sendCmd(\"listen\")'>Trigger Listen</button><div id='speech' class='stat' style='margin-top:8px'>Using built-in device mic</div><small>Browser speech is optional and may be blocked by browser permissions.</small><button style='margin-top:8px' onclick='speechOnce()'>Try Browser Speech (Optional)</button></section>";
    html += "<section class='card'><h2>Settings</h2><label>Brightness <span id='brightnessValue'>20</span></label><input id='brightness' type='range' min='0' max='100' value='20' oninput='brightnessValue.textContent=this.value'>";
    html += "<label>Volume <span id='volumeValue'>180</span></label><input id='volume' type='range' min='0' max='255' value='180' oninput='volumeValue.textContent=this.value'>";
    html += "<label>Voice style</label><select id='voiceStyle'><option value='natural'>Natural</option><option value='clear'>Clear</option><option value='bright'>Bright</option><option value='deep'>Deep</option><option value='warm'>Warm</option></select>";
    html += "<label>Voice model (.tflite)</label><select id='voiceModel'></select><button style='margin-top:8px;width:100%' onclick='previewVoice()'>Preview Voice</button>";
    html += "<label>Voice speed <span id='voiceSpeedValue'>1.00</span></label><input id='voiceSpeed' type='range' min='0.5' max='2.0' step='0.05' value='1.0' oninput='voiceSpeedValue.textContent=Number(this.value).toFixed(2)'>";
    html += "<label>Voice pitch <span id='voicePitchValue'>1.00</span></label><input id='voicePitch' type='range' min='0.5' max='2.0' step='0.05' value='1.0' oninput='voicePitchValue.textContent=Number(this.value).toFixed(2)'>";
    html += "<label>Voice clarity <span id='voiceClarityValue'>1.00</span></label><input id='voiceClarity' type='range' min='0.5' max='2.0' step='0.05' value='1.0' oninput='voiceClarityValue.textContent=Number(this.value).toFixed(2)'>";
    html += "<label><input id='fallbackVoice' type='checkbox' checked> Use fallback voice when neural model unavailable</label>";
    html += "<button style='margin-top:10px;width:100%' onclick='saveSettings()'>Apply Settings</button></section>";
    html += "<section class='card'><h2>Face Settings</h2><label>Face style</label><select id='faceStyle' onchange='refreshFaceAnimations()'></select>";
    html += "<label>Preview animation</label><select id='facePreviewAnim'><option value='idle'>idle</option></select><button style='margin-top:8px;width:100%' onclick='previewFace()'>Preview Animation</button><button style='margin-top:8px;width:100%' onclick='setFaceAnimation()'>Set Animation</button>";
    html += "<label>Emotion animation mapping</label><div id='emotionMapGrid' class='filelist' style='max-height:220px;overflow:auto'></div><small>Select which animation each emotion should use. Leave blank for automatic behavior.</small>";
    html += "<label>Blink speed <span id='faceBlinkValue'>1.00</span></label><input id='faceBlink' type='range' min='0.25' max='4.0' step='0.05' value='1.0' oninput='faceBlinkValue.textContent=Number(this.value).toFixed(2)'>";
    html += "<label>Glow intensity <span id='faceGlowValue'>0.80</span></label><input id='faceGlow' type='range' min='0.0' max='1.0' step='0.05' value='0.8' oninput='faceGlowValue.textContent=Number(this.value).toFixed(2)'>";
    html += "<label>Eye color</label><input id='faceEyeColor' type='color' value='#AEE6FF'>";
    html += "<label><input id='faceIdleEnabled' type='checkbox' checked> Idle animation enabled</label>";
    html += "<label><input id='faceAiModify' type='checkbox'> AI can modify existing frames</label>";
    html += "<label><input id='faceAiCreate' type='checkbox'> AI can create new styles/animations</label>";
    html += "<button style='margin-top:10px;width:100%' onclick='saveFaceSettings()'>Save Face Settings</button></section>";
    html += "<section class='card'><h2>Face Telemetry</h2><div id='faceTelemetryOut' class='mono' style='white-space:pre-line;background:#060b16;padding:10px;border-radius:8px;min-height:130px;border:1px solid #1f2d4d'>Loading face telemetry...</div><div style='margin-top:10px'><label>FPS warn / bad</label><div class='row'><input id='ftFpsWarn' type='text' value='20' oninput='faceTelemetryEditing=true'><input id='ftFpsBad' type='text' value='12' oninput='faceTelemetryEditing=true'></div><label>Draw ms warn / bad</label><div class='row'><input id='ftDrawWarn' type='text' value='20' oninput='faceTelemetryEditing=true'><input id='ftDrawBad' type='text' value='30' oninput='faceTelemetryEditing=true'></div><label>Blend draw ms warn / bad</label><div class='row'><input id='ftBlendWarn' type='text' value='24' oninput='faceTelemetryEditing=true'><input id='ftBlendBad' type='text' value='36' oninput='faceTelemetryEditing=true'></div><label>Over-budget % warn / bad</label><div class='row'><input id='ftBudgetWarn' type='text' value='5' oninput='faceTelemetryEditing=true'><input id='ftBudgetBad' type='text' value='15' oninput='faceTelemetryEditing=true'></div><label>Fallback count warn / bad</label><div class='row'><input id='ftFallbackWarn' type='text' value='1' oninput='faceTelemetryEditing=true'><input id='ftFallbackBad' type='text' value='3' oninput='faceTelemetryEditing=true'></div><div class='btns' style='margin-top:8px'><button class='btn-primary' onclick='saveFaceTelemetryThresholds()'>Save Thresholds</button><button onclick='resetFaceTelemetryThresholds()'>Reset Thresholds</button></div><small id='faceTelemetryThresholdStatus'>Loaded from device</small></div></section>";
    html += "<label><input id='rtVoiceInput' type='checkbox' checked> Voice input enabled</label>";
    html += "<label><input id='rtAutonomy' type='checkbox' checked> Autonomy enabled</label>";
    html += "<label><input id='rtHeartbeat' type='checkbox' checked> Web heartbeat enabled</label>";
    html += "<label><input id='rtImu' type='checkbox' checked> IMU events enabled</label>";
    html += "<label><input id='rtUsb' type='checkbox' checked> USB events enabled</label>";
    html += "<label><input id='browserTts' type='checkbox' checked> Speak replies in browser (high quality)</label>";
    html += "<label>Browser voice</label><select id='browserVoice'></select>";
    html += "<section class='card' style='grid-column:1/-1'><h2>Live Console</h2><pre id='out'>connecting...</pre></section></section>";
    html += "<section class='grid panel' id='panel-sd'><section class='card' style='grid-column:1/-1'><h2>SD Card Manager</h2><div id='sdCrumbs' class='crumbs'></div><label>Path</label><input id='sdPath' type='text' value='/Flic'><div class='btns'><button onclick='loadSdList()'>Refresh</button><button onclick='goUpSd()'>Up</button><button onclick='mkdirSd()'>Create Folder</button><button onclick='renameSd()'>Rename</button><button onclick='deleteSd()'>Delete</button></div><label>Upload files</label><input id='sdUpload' type='file' multiple><div id='sdDrop' style='margin-top:8px;padding:10px;border:1px dashed #2f4476;border-radius:8px;text-align:center'>Drag and drop files here</div><progress id='sdProgress' max='100' value='0' style='width:100%;margin-top:8px'></progress><div id='sdFileList' class='filelist' style='margin-top:8px'></div><pre id='sdList' style='min-height:120px'></pre></section></section>";
    html += "<section class='grid panel' id='panel-face'><section class='card'><h2>Face Animation Tools</h2><label>Animation</label><select id='faceToolAnim'></select><div class='btns'><button onclick='playFaceAnimation()'>Play on Device</button><button onclick='previewFace()'>Preview Animation</button><button onclick='reloadFaceAnimations()'>Reload Animations</button><button onclick='validateFaceAnimations()'>Validate Set</button></div><label>Live Snapshot</label><img id='faceSnapshot' style='width:240px;height:240px;object-fit:contain;border-radius:16px;border:1px solid #2a3a60;background:radial-gradient(circle,#1e2e52 0,#0b1323 65%,#05060A 100%)' alt='snapshot'></section><section class='card'><h2>Emotion Triggers</h2><div class='btns'><button onclick=\"setEmotionBtn('neutral')\">Neutral</button><button onclick=\"setEmotionBtn('listening')\">Listening</button><button onclick=\"setEmotionBtn('thinking')\">Thinking</button><button onclick=\"setEmotionBtn('speaking')\">Speaking</button><button onclick=\"setEmotionBtn('happy')\">Happy</button><button onclick=\"setEmotionBtn('sad')\">Sad</button><button onclick=\"setEmotionBtn('surprised')\">Surprise</button><button onclick=\"setEmotionBtn('tired')\">Tired</button></div><label>Custom Emotion</label><input id='faceToolEmotion' type='text' placeholder='happy, sad, thinking...'><button style='margin-top:8px;width:100%' onclick='setFaceEmotion()'>Set Emotion</button></section><section class='card'><h2>Frame Actions</h2><label>Frame path</label><input id='faceFramePath' type='text' placeholder='/Flic/animations/face/default/idle/frame_000.png'><input id='faceFrameReplaceFile' type='file' accept='.png'><div class='btns'><button onclick='viewFrameFile()'>View</button><button onclick='replaceFrameFile()'>Replace</button><button onclick='deleteFrameFile()'>Delete</button></div></section><section class='card' style='grid-column:1/-1'><h2>Frame Diagnostics</h2><pre id='faceValidateOut'>Run validation to inspect frame counts/missing frames.</pre></section></section>";
    html += "<section class='grid panel' id='panel-logs'><section class='card' style='grid-column:1/-1'><h2>Logs & Diagnostics</h2><div class='btns'><button onclick=\"setLogFilter('face')\">Face</button><button onclick=\"setLogFilter('sd')\">SD</button><button onclick=\"setLogFilter('web')\">Web</button><button onclick=\"setLogFilter('emotion')\">Emotion</button><button onclick=\"setLogFilter('audio')\">Audio</button><button onclick=\"setLogFilter('')\">All</button></div><label>Subsystem filter</label><input id='logFilter' type='text' placeholder='face,sd,web,emotion,audio'><label><input id='logsAuto' type='checkbox' checked> Auto refresh</label><div class='btns'><button onclick='loadLogs()'>Refresh Logs</button><button onclick='downloadLogs()'>Download Full Log</button><button onclick='clearLogs()'>Clear Log</button></div><pre id='logsOut' style='min-height:220px'></pre></section></section><div id='toast' class='toast'></div><div id='pngModal' class='toast' style='left:50%;transform:translateX(-50%);right:auto;bottom:auto;top:70px;max-width:90vw;display:none'><div class='row' style='justify-content:space-between'><b>PNG Preview</b><button onclick='closePngPreview()'>Close</button></div><img id='pngModalImg' style='max-width:80vw;max-height:70vh;border-radius:8px;border:1px solid #2a3a60'></div></main></div>";
    html += "<script>const out=document.getElementById('out');const wsPort=";
    html += String(wsPort_);
    html += ";const netInfo=document.getElementById('netInfo');const connPill=document.getElementById('connPill');const logLines=[];";
    html += "function log(x){const line=(typeof x==='string'?x:JSON.stringify(x));logLines.push(line);while(logLines.length>40){logLines.shift();}out.textContent=logLines.join('\\n');}async function api(path,opt){const r=await fetch(path,opt);const t=await r.text();try{return JSON.parse(t);}catch{return {ok:r.ok,raw:t,status:r.status};}}";
    html += "function toast(msg,isErr){const el=document.getElementById('toast');if(!el)return;el.textContent=String(msg||'');el.className='toast show'+(isErr?' err':'');setTimeout(()=>{el.className='toast';},2200);}function showPanel(name){const ids=['home','sd','face','logs'];for(const id of ids){const p=document.getElementById('panel-'+id);const b=document.getElementById('nav-'+id);if(p)p.classList.toggle('active',id===name);if(b)b.classList.toggle('active',id===name);}}function initNavDataPanels(){const buttons=document.querySelectorAll('button[data-panel]');buttons.forEach((btn)=>{btn.addEventListener('click',()=>{const panel=String(btn.getAttribute('data-panel')||'home');showPanel(panel);});});}";
    html += "function openPngPreview(path){if(!pngModal||!pngModalImg)return;pngModal.style.display='block';pngModal.className='toast show';pngModalImg.src='/api/sd/download?path='+encodeURIComponent(String(path||''));}function closePngPreview(){if(!pngModal)return;pngModal.style.display='none';pngModal.className='toast';}";
    html += "function setLogFilter(v){if(logFilter){logFilter.value=v;}loadLogs();}function prettyBytes(v){const n=Number(v||0);if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(1)+' MB';}";
    html += "let voiceLoaded=false;function loadVoices(){if(!window.speechSynthesis||!browserVoice)return;const voices=window.speechSynthesis.getVoices()||[];if(!voices.length)return;const current=browserVoice.value;browserVoice.innerHTML='';for(let i=0;i<voices.length;i++){const v=voices[i];const o=document.createElement('option');o.value=v.name;o.textContent=v.name+' ('+v.lang+')';browserVoice.appendChild(o);}let pref='';for(const v of voices){if((v.name||'').toLowerCase().includes('google')){pref=v.name;break;}}if(current&&[...browserVoice.options].some(o=>o.value===current)){browserVoice.value=current;}else if(pref){browserVoice.value=pref;}voiceLoaded=true;}";
    html += "function speakBrowser(text){if(!text||!browserTts||!browserTts.checked)return;if(!window.speechSynthesis||!window.SpeechSynthesisUtterance){log('Browser TTS unavailable');return;}if(!voiceLoaded)loadVoices();try{const u=new SpeechSynthesisUtterance(String(text));const voices=window.speechSynthesis.getVoices()||[];let selected=null;if(browserVoice&&browserVoice.value){selected=voices.find(v=>v.name===browserVoice.value)||null;}if(!selected){selected=voices.find(v=>(v.name||'').toLowerCase().includes('google'))||voices[0]||null;}if(selected)u.voice=selected;u.rate=1.0;u.pitch=1.0;u.volume=1.0;window.speechSynthesis.cancel();window.speechSynthesis.speak(u);}catch(e){log('Browser TTS error: '+String(e));}}";
    html += "const emotionMapKeys=['neutral','listening','thinking','speaking','happy','sad','surprised','angry','sleepy','error','idle'];let faceMapSelections={};let faceCatalog={styles:[]};function refreshFaceAnimations(){if(!faceStyle||!facePreviewAnim)return;const selected=String(faceStyle.value||'default');const style=(faceCatalog.styles||[]).find(s=>String(s.name||'')===selected);facePreviewAnim.innerHTML='';const items=(style&&Array.isArray(style.animations))?style.animations:[{name:'idle'}];for(const it of items){const n=String(it.name||'idle');const o=document.createElement('option');o.value=n;o.textContent=n;facePreviewAnim.appendChild(o);}if(facePreviewAnim.options.length===0){const o=document.createElement('option');o.value='idle';o.textContent='idle';facePreviewAnim.appendChild(o);}renderEmotionMapControls();}function renderEmotionMapControls(preset){const grid=document.getElementById('emotionMapGrid');if(!grid||!faceStyle)return;const selectedStyle=String(faceStyle.value||'default');const style=(faceCatalog.styles||[]).find(s=>String(s.name||'')===selectedStyle);const items=(style&&Array.isArray(style.animations))?style.animations:[];if(preset&&typeof preset==='object'){faceMapSelections={...preset};}grid.innerHTML='';for(const emotion of emotionMapKeys){const row=document.createElement('div');row.className='row';row.style.justifyContent='space-between';const label=document.createElement('span');label.className='tiny';label.textContent=emotion;const select=document.createElement('select');select.id='emap_'+emotion;select.style.flex='1';select.style.marginLeft='8px';const auto=document.createElement('option');auto.value='';auto.textContent='auto';select.appendChild(auto);for(const it of items){const n=String(it.name||'idle');const opt=document.createElement('option');opt.value=n;opt.textContent=n;select.appendChild(opt);}const selectedValue=String(faceMapSelections[emotion]||'');if(selectedValue&&[...select.options].some(o=>o.value===selectedValue)){select.value=selectedValue;}select.onchange=()=>{faceMapSelections[emotion]=String(select.value||'');};row.appendChild(label);row.appendChild(select);grid.appendChild(row);}}function getEmotionMapPayload(){const payload={};for(const emotion of emotionMapKeys){const key='emap_'+emotion;const sel=document.getElementById(key);const value=String((sel&&sel.value)||'').trim();if(value.length>0){payload[emotion]=value;}}return payload;}";
    html += "function syncFaceToolOptions(){if(!faceToolAnim||!facePreviewAnim)return;faceToolAnim.innerHTML='';for(const o of facePreviewAnim.options){const c=document.createElement('option');c.value=o.value;c.textContent=o.textContent;faceToolAnim.appendChild(c);}if(faceToolAnim.options.length===0){const c=document.createElement('option');c.value='idle';c.textContent='idle';faceToolAnim.appendChild(c);}}";
    html += "async function refresh(){try{const h=await api('/api/health');const s=await api('/api/status');const fs=await api('/api/face/settings');const styles=await api('/api/face/styles');const faceMeta=await api('/api/face/animations');const sdMeta=await api('/api/sd/list?path=%2FFlic');if(faceMeta&&Array.isArray(faceMeta.styles)){faceCatalog=faceMeta;}const ip=(h&&h.ip)?h.ip:'-';const apFallback=!!(h&&h.ap_mode);const setupAp=!!(h&&h.setup_ap);const apIp=(h&&h.ap_ip)?h.ap_ip:'192.168.4.1';connPill.textContent=apFallback?'AP MODE':(setupAp?'ONLINE + AP':'ONLINE');connPill.className='pill '+((apFallback||setupAp)?'warn':'ok');netInfo.innerHTML='Mode: <b>'+(apFallback?'AP fallback':'WiFi STA')+'</b><br>IP: <b>'+ip+'</b><br>Setup AP: <b>'+(setupAp?'ON':'OFF')+'</b><br>AP IP: <b>'+apIp+'</b><br>HTTP: <b>80</b><br>WS: <b>'+wsPort+'</b>';if(sdMountedChip){const mounted=!!(sdMeta&&sdMeta.ok);sdMountedChip.textContent='SD: '+(mounted?'mounted':'missing');sdMountedChip.className='chip '+(mounted?'ok':'warn');}if(sdSpaceChip&&sdMeta&&sdMeta.ok){const free=Number(sdMeta.free_bytes||0);sdSpaceChip.textContent='free: '+prettyBytes(free);}if(s&&s.settings){brightness.value=s.settings.brightness??brightness.value;brightnessValue.textContent=brightness.value;volume.value=s.settings.volume??volume.value;volumeValue.textContent=volume.value;voiceStyle.value=s.settings.voice_style??voiceStyle.value;const availableVoices=(s.settings.available_voices||[]);voiceModel.innerHTML='';for(const v of availableVoices){const o=document.createElement('option');o.value=String(v);o.textContent=String(v);voiceModel.appendChild(o);}if(availableVoices.length===0){const o=document.createElement('option');o.value='';o.textContent='(no .tflite models found)';voiceModel.appendChild(o);}if((s.settings.voice_model||'').length>0){voiceModel.value=s.settings.voice_model;}voiceSpeed.value=Number(s.settings.voice_speed??voiceSpeed.value);voiceSpeedValue.textContent=Number(voiceSpeed.value).toFixed(2);voicePitch.value=Number(s.settings.voice_pitch??voicePitch.value);voicePitchValue.textContent=Number(voicePitch.value).toFixed(2);voiceClarity.value=Number(s.settings.voice_clarity??voiceClarity.value);voiceClarityValue.textContent=Number(voiceClarity.value).toFixed(2);fallbackVoice.checked=!!s.settings.fallback_voice;}if(fs&&fs.settings){faceStyle.innerHTML='';const faceStyles=(styles&&Array.isArray(styles))?styles:(fs.styles||[]);for(const st of faceStyles){const o=document.createElement('option');o.value=String(st);o.textContent=String(st);faceStyle.appendChild(o);}if((fs.settings.active_style||'').length>0){faceStyle.value=fs.settings.active_style;}refreshFaceAnimations();syncFaceToolOptions();renderEmotionMapControls(fs.settings.emotion_animation_map||{});faceBlink.value=Number(fs.settings.blink_speed??faceBlink.value);faceBlinkValue.textContent=Number(faceBlink.value).toFixed(2);faceGlow.value=Number(fs.settings.glow_intensity??faceGlow.value);faceGlowValue.textContent=Number(faceGlow.value).toFixed(2);faceEyeColor.value=String(fs.settings.eye_color||faceEyeColor.value);faceIdleEnabled.checked=!!fs.settings.idle_enabled;faceAiModify.checked=!!fs.settings.ai_can_modify;faceAiCreate.checked=!!fs.settings.ai_can_create;}if(s&&s.runtime){rtVoiceInput.checked=!!s.runtime.voice_input;rtAutonomy.checked=!!s.runtime.autonomy;rtHeartbeat.checked=!!s.runtime.web_heartbeat;rtImu.checked=!!s.runtime.imu_events;rtUsb.checked=!!s.runtime.usb_events;}}catch(e){connPill.textContent='offline';netInfo.textContent='Unable to fetch API.';log(String(e));}}";
    html += "async function sendCmd(text){const body={text,source:'web_pager'};const r=await api('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});log('cmd:'+text+' => '+JSON.stringify(r));if(r&&r.reply){speakBrowser(r.reply);}}function sendCustom(){const el=document.getElementById('cmd');const v=(el.value||'').trim();if(!v)return;sendCmd(v);el.value='';}";
    html += "async function saveSettings(){const style=String(voiceStyle.value||'natural');const payload={brightness:Number(brightness.value),volume:Number(volume.value),voice_style:style,voice_model:String(voiceModel.value||''),voice_speed:Number(voiceSpeed.value),voice_pitch:Number(voicePitch.value),voice_clarity:Number(voiceClarity.value),fallback_voice:!!fallbackVoice.checked,runtime:{voice_input:!!rtVoiceInput.checked,autonomy:!!rtAutonomy.checked,web_heartbeat:!!rtHeartbeat.checked,imu_events:!!rtImu.checked,usb_events:!!rtUsb.checked}};const r=await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});log('settings => '+JSON.stringify(r));if(r&&r.ok){const vr=await api('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text:'voice style '+style,source:'web_pager'})});log('voice style confirm => '+JSON.stringify(vr));if(vr&&vr.reply){speakBrowser(vr.reply);}}refresh();}";
    html += "async function saveFaceSettings(){const payload={active_style:String(faceStyle.value||'default'),blink_speed:Number(faceBlink.value),idle_enabled:!!faceIdleEnabled.checked,glow_intensity:Number(faceGlow.value),eye_color:String(faceEyeColor.value||'#AEE6FF'),emotion_animation_map:getEmotionMapPayload(),ai_can_modify:!!faceAiModify.checked,ai_can_create:!!faceAiCreate.checked};const r=await api('/api/face/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});log('face settings => '+JSON.stringify(r));refresh();}";
    html += "async function previewFace(){const style=String(faceStyle.value||'default');const animation=String(facePreviewAnim.value||'idle');const r=await api('/api/face/preview',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({style,animation})});log('face preview => '+JSON.stringify(r));}";
    html += "async function setFaceAnimation(){const style=String(faceStyle.value||'default');const animation=String(facePreviewAnim.value||'idle');const r=await api('/api/face/set_animation',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({style,animation})});log('face set animation => '+JSON.stringify(r));}";
    html += "async function playFaceAnimation(){const style=String(faceStyle.value||'default');const animation=String((faceToolAnim&&faceToolAnim.value)||facePreviewAnim.value||'idle');const r=await api('/api/face/play',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({style,animation})});log('face play => '+JSON.stringify(r));}";
    html += "function setEmotionBtn(e){if(faceToolEmotion)faceToolEmotion.value=e;setFaceEmotion();}async function setFaceEmotion(){const emotion=String((faceToolEmotion&&faceToolEmotion.value)||'neutral').trim();if(!emotion)return;const r=await api('/api/face/set_emotion',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({emotion})});log('face emotion => '+JSON.stringify(r));toast(r&&r.ok?('Emotion: '+emotion):'Emotion set failed',!(r&&r.ok));}";
    html += "async function reloadFaceAnimations(){const r=await api('/api/face/reload',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});log('face reload => '+JSON.stringify(r));refresh();}";
    html += "async function validateFaceAnimations(){const r=await api('/api/face/validate');log('face validate => '+JSON.stringify(r));if(faceValidateOut){faceValidateOut.textContent=JSON.stringify(r,null,2);}toast(r&&r.ok?'Validation complete':'Validation failed',!(r&&r.ok));}";
    html += "function clsHigh(v,warn,bad){const n=Number(v||0);if(n>=bad)return 'bad';if(n>=warn)return 'warn';return 'ok';}function clsLow(v,warn,bad){const n=Number(v||0);if(n<=bad)return 'bad';if(n<=warn)return 'warn';return 'ok';}";
    html += "let faceTelemetryDefaults=null;let faceTelemetryEditing=false;function telemetryStatus(msg,isErr){const el=document.getElementById('faceTelemetryThresholdStatus');if(!el)return;el.textContent=String(msg||'');el.className=isErr?'bad':'';}function writeFaceTelemetryThresholdInputs(thr){if(!thr)return;const set=(id,val)=>{const el=document.getElementById(id);if(el)el.value=Number(val).toFixed(2);};set('ftFpsWarn',thr.fps_warn);set('ftFpsBad',thr.fps_bad);set('ftDrawWarn',thr.draw_warn_ms);set('ftDrawBad',thr.draw_bad_ms);set('ftBlendWarn',thr.blend_draw_warn_ms);set('ftBlendBad',thr.blend_draw_bad_ms);set('ftBudgetWarn',thr.over_budget_warn_pct);set('ftBudgetBad',thr.over_budget_bad_pct);set('ftFallbackWarn',thr.fallback_warn_count);set('ftFallbackBad',thr.fallback_bad_count);}function readFaceTelemetryThresholdInputs(){const get=(id,fallback)=>{const el=document.getElementById(id);const v=Number(el&&el.value!==''?el.value:fallback);return Number.isFinite(v)?v:Number(fallback);};return {fps_warn:get('ftFpsWarn',20),fps_bad:get('ftFpsBad',12),draw_warn_ms:get('ftDrawWarn',20),draw_bad_ms:get('ftDrawBad',30),blend_draw_warn_ms:get('ftBlendWarn',24),blend_draw_bad_ms:get('ftBlendBad',36),over_budget_warn_pct:get('ftBudgetWarn',5),over_budget_bad_pct:get('ftBudgetBad',15),fallback_warn_count:get('ftFallbackWarn',1),fallback_bad_count:get('ftFallbackBad',3)};}async function saveFaceTelemetryThresholds(){const payload={thresholds:readFaceTelemetryThresholdInputs()};const r=await api('/api/face/telemetry',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(r&&r.ok){faceTelemetryEditing=false;telemetryStatus('Saved to device',false);if(r.thresholds){writeFaceTelemetryThresholdInputs(r.thresholds);}refreshFaceTelemetry();}else{telemetryStatus((r&&r.error)||'save_failed',true);}log('face telemetry save => '+JSON.stringify(r));}async function resetFaceTelemetryThresholds(){if(!faceTelemetryDefaults){telemetryStatus('No defaults loaded yet',true);return;}writeFaceTelemetryThresholdInputs(faceTelemetryDefaults);faceTelemetryEditing=false;await saveFaceTelemetryThresholds();telemetryStatus('Reset to startup defaults',false);}async function refreshFaceTelemetry(){const outEl=document.getElementById('faceTelemetryOut');if(!outEl)return;try{const t=await api('/api/face/telemetry');if(!t||t.ok===false){outEl.textContent=JSON.stringify(t||{ok:false,error:'telemetry_unavailable'},null,2);return;}const thr=t.thresholds||{};const fpsWarn=Number(thr.fps_warn??20);const fpsBad=Number(thr.fps_bad??12);const drawWarn=Number(thr.draw_warn_ms??20);const drawBad=Number(thr.draw_bad_ms??30);const blendWarn=Number(thr.blend_draw_warn_ms??24);const blendBad=Number(thr.blend_draw_bad_ms??36);const budgetWarn=Number(thr.over_budget_warn_pct??5);const budgetBad=Number(thr.over_budget_bad_pct??15);const fallbackWarn=Number(thr.fallback_warn_count??1);const fallbackBad=Number(thr.fallback_bad_count??3);const liveThr={fps_warn:fpsWarn,fps_bad:fpsBad,draw_warn_ms:drawWarn,draw_bad_ms:drawBad,blend_draw_warn_ms:blendWarn,blend_draw_bad_ms:blendBad,over_budget_warn_pct:budgetWarn,over_budget_bad_pct:budgetBad,fallback_warn_count:fallbackWarn,fallback_bad_count:fallbackBad};if(!faceTelemetryDefaults){faceTelemetryDefaults={...liveThr};}if(!faceTelemetryEditing){writeFaceTelemetryThresholdInputs(liveThr);}const fps=Number(t.fps||0);const avgDraw=Number(t.avg_draw_ms||0);const avgBlend=Number(t.avg_blend_draw_ms||0);const overBudget=Number(t.over_budget_frames||0);const fallbacks=Number(t.blend_fallbacks||0);const frameCount=Number(t.frames||0);const blendFrames=Number(t.blend_frames||0);const overBudgetRate=frameCount>0?(overBudget*100.0/frameCount):0.0;const fpsCls=clsLow(fps,fpsWarn,fpsBad);const drawCls=clsHigh(avgDraw,drawWarn,drawBad);const blendCls=clsHigh(avgBlend,blendWarn,blendBad);const budgetCls=clsHigh(overBudgetRate,budgetWarn,budgetBad);const fallbackCls=clsHigh(fallbacks,fallbackWarn,fallbackBad);outEl.innerHTML='fps: <span class=\''+fpsCls+'\'>'+fps.toFixed(2)+'</span>\\nframes: '+frameCount+' (blend: '+blendFrames+')\\navg draw ms: <span class=\''+drawCls+'\'>'+avgDraw.toFixed(2)+'</span>\\navg blend draw ms: <span class=\''+blendCls+'\'>'+avgBlend.toFixed(2)+'</span>\\nover budget: <span class=\''+budgetCls+'\'>'+overBudget+' ('+overBudgetRate.toFixed(1)+'%)</span>\\nblend fallbacks: <span class=\''+fallbackCls+'\'>'+fallbacks+'</span>\\nemotion: '+String(t.active_emotion||'-')+'\\npersonality: '+String(t.personality||'-')+'\\nblending: '+(t.is_blending?'yes':'no')+'\\nthresholds: fps('+fpsWarn+'/'+fpsBad+'), draw('+drawWarn+'/'+drawBad+'ms), blend('+blendWarn+'/'+blendBad+'ms)';}catch(e){outEl.textContent='telemetry error: '+String(e);}}";
    html += "function viewFrameFile(){const p=String((faceFramePath&&faceFramePath.value)||'').trim();if(!p)return;window.open('/api/sd/download?path='+encodeURIComponent(p),'_blank');}async function deleteFrameFile(){const p=String((faceFramePath&&faceFramePath.value)||'').trim();if(!p)return;const r=await api('/api/sd/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path:p})});toast(r&&r.ok?'Frame deleted':'Frame delete failed',!(r&&r.ok));}async function replaceFrameFile(){const p=String((faceFramePath&&faceFramePath.value)||'').trim();const f=(faceFrameReplaceFile&&faceFrameReplaceFile.files&&faceFrameReplaceFile.files[0])?faceFrameReplaceFile.files[0]:null;if(!p||!f){toast('Select frame path and file',true);return;}const folder=p.substring(0,p.lastIndexOf('/'));const targetName=p.substring(p.lastIndexOf('/')+1);const prev=String(sdPath&&sdPath.value||'/Flic');if(sdPath){sdPath.value=folder;}const blob=new File([f],targetName,{type:f.type||'image/png'});await uploadOne(blob);if(sdPath){sdPath.value=prev;}}";
    html += "function refreshSnapshot(){if(!faceSnapshot)return;faceSnapshot.src='/api/face/snapshot?ts='+Date.now();}";
    html += "function buildCrumbs(path){if(!sdCrumbs)return;sdCrumbs.innerHTML='';const parts=String(path||'/').split('/').filter(Boolean);let current='';const root=document.createElement('button');root.className='crumb';root.textContent='/';root.onclick=()=>{sdPath.value='/Flic';loadSdList();};sdCrumbs.appendChild(root);for(const p of parts){current+='/'+p;const b=document.createElement('button');b.className='crumb';b.textContent=p;b.onclick=(()=>{const target=current;return ()=>{sdPath.value=target;loadSdList();};})();sdCrumbs.appendChild(b);}}";
    html += "function renderSdEntries(entries){if(!sdFileList)return;sdFileList.innerHTML='';for(const e of (entries||[])){const row=document.createElement('div');row.className='fitem';const icon=document.createElement('div');icon.textContent=e.dir?'📁':'📄';const ts=(e.timestamp&&Number(e.timestamp)>0)?(' · '+new Date(Number(e.timestamp)*1000).toLocaleString()):'';const name=document.createElement('div');name.innerHTML='<div>'+String(e.name||'')+'</div><div class=\"tiny\">'+prettyBytes(e.size||0)+ts+'</div>';const actions=document.createElement('div');if(e.dir){const o=document.createElement('button');o.textContent='Open';o.onclick=()=>{sdPath.value=String(e.path||'/Flic');loadSdList();};actions.appendChild(o);}else{const dl=document.createElement('button');dl.textContent='Download';dl.onclick=()=>{window.open('/api/sd/download?path='+encodeURIComponent(String(e.path||'')),'_blank');};actions.appendChild(dl);if(String(e.name||'').toLowerCase().endsWith('.png')){const pv=document.createElement('button');pv.textContent='Preview';pv.style.marginLeft='6px';pv.onclick=()=>{openPngPreview(String(e.path||''));};actions.appendChild(pv);}}row.appendChild(icon);row.appendChild(name);row.appendChild(actions);sdFileList.appendChild(row);}}";
    html += "async function loadSdList(){const p=encodeURIComponent(String(sdPath.value||'/Flic'));const r=await api('/api/sd/list?path='+p);if(sdList)sdList.textContent=JSON.stringify(r,null,2);if(r&&r.ok){buildCrumbs(r.path);renderSdEntries(r.entries);toast('SD listing updated');}else{toast((r&&r.error)||'SD list failed',true);}}";
    html += "function uploadOne(file){return new Promise((resolve)=>{const path=encodeURIComponent(String(sdPath.value||'/Flic'));const xhr=new XMLHttpRequest();xhr.open('POST','/api/sd/upload?path='+path,true);xhr.upload.onprogress=(e)=>{if(e.lengthComputable&&sdProgress){sdProgress.value=Math.round((e.loaded/e.total)*100);}};xhr.onload=()=>{try{log('sd upload => '+xhr.responseText);const js=JSON.parse(xhr.responseText||'{}');toast(js&&js.ok?('Uploaded '+file.name):('Upload failed: '+(js.error||file.name)),!(js&&js.ok));}catch{toast('Upload complete');}if(sdProgress)sdProgress.value=0;loadSdList();resolve();};const fd=new FormData();fd.append('file',file,file.name);xhr.send(fd);});}";
    html += "async function uploadSdFiles(files){if(!files||!files.length)return;for(const f of files){await uploadOne(f);}}";
    html += "async function goUpSd(){const p=String(sdPath.value||'/Flic');if(p==='/'||p==='/Flic')return;const idx=p.lastIndexOf('/');sdPath.value=(idx>0?p.substring(0,idx):'/Flic');loadSdList();}";
    html += "async function deleteSd(){const path=String(prompt('Delete path',sdPath.value||'/Flic')||'').trim();if(!path)return;const r=await api('/api/sd/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path})});log('sd delete => '+JSON.stringify(r));toast(r&&r.ok?'Deleted':'Delete failed',!(r&&r.ok));loadSdList();}";
    html += "async function mkdirSd(){const path=String(prompt('New folder path',sdPath.value||'/Flic/new_folder')||'').trim();if(!path)return;const r=await api('/api/sd/mkdir',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path})});log('sd mkdir => '+JSON.stringify(r));toast(r&&r.ok?'Folder created':'Create folder failed',!(r&&r.ok));loadSdList();}";
    html += "async function renameSd(){const from=String(prompt('Rename from',sdPath.value||'/Flic')||'').trim();if(!from)return;const to=String(prompt('Rename to',from)||'').trim();if(!to)return;const r=await api('/api/sd/rename',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({from,to})});log('sd rename => '+JSON.stringify(r));toast(r&&r.ok?'Renamed':'Rename failed',!(r&&r.ok));loadSdList();}";
    html += "async function loadLogs(){const f=encodeURIComponent(String((logFilter&&logFilter.value)||''));const r=await api('/api/logs?limit=500&subsystem='+f);if(logsOut)logsOut.textContent=JSON.stringify(r,null,2);}";
    html += "async function clearLogs(){const r=await api('/api/logs/clear',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});toast(r&&r.ok?'Logs cleared':'Clear logs failed',!(r&&r.ok));loadLogs();}";
    html += "function downloadLogs(){window.open('/api/sd/download?path=/Flic/logs/webui_sd.log','_blank');}";
    html += "async function previewVoice(){const r=await api('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text:'preview voice',source:'web_pager'})});log('preview voice => '+JSON.stringify(r));if(r&&r.reply){speakBrowser(r.reply);}}";
    html += "async function ensureMicPermission(){if(!navigator.mediaDevices||!navigator.mediaDevices.getUserMedia){return true;}try{const s=await navigator.mediaDevices.getUserMedia({audio:true});s.getTracks().forEach(t=>t.stop());return true;}catch(e){return false;}}";
    html += "async function speechOnce(){const SR=window.SpeechRecognition||window.webkitSpeechRecognition;const info=document.getElementById('speech');if(!SR){info.textContent='speech API unavailable in this browser';return;}const micOk=await ensureMicPermission();if(!micOk){info.textContent='microphone permission denied';return;}if(location.hostname==='192.168.4.1'){info.textContent='AP mode: speech may fail; prefer home Wi-Fi IP';}const rec=new SR();rec.lang='en-US';rec.interimResults=false;rec.maxAlternatives=1;info.textContent='listening...';rec.onresult=(ev)=>{const t=(ev.results&&ev.results[0]&&ev.results[0][0]&&ev.results[0][0].transcript)||'';info.textContent=t||'no speech';if(t){sendCmd(t);}};rec.onerror=(ev)=>{const err=(ev&&ev.error)?ev.error:'unknown';if(err==='not-allowed'){info.textContent='speech blocked: allow mic in browser site settings, then reload';}else{info.textContent='speech error: '+err;}};rec.onend=()=>{if(info.textContent==='listening...')info.textContent='idle';};try{rec.start();}catch(e){info.textContent='speech start failed';}}";
    html += "if(sdUpload){sdUpload.addEventListener('change',(e)=>uploadSdFiles(e.target.files||[]));}if(sdDrop){sdDrop.addEventListener('dragover',(e)=>{e.preventDefault();sdDrop.style.borderColor='#5fffe0';});sdDrop.addEventListener('dragleave',()=>{sdDrop.style.borderColor='#2f4476';});sdDrop.addEventListener('drop',(e)=>{e.preventDefault();sdDrop.style.borderColor='#2f4476';const files=e.dataTransfer&&e.dataTransfer.files?e.dataTransfer.files:[];uploadSdFiles(files);});}if(window.speechSynthesis){window.speechSynthesis.onvoiceschanged=loadVoices;setTimeout(loadVoices,300);}initNavDataPanels();showPanel('home');setInterval(refresh,5000);setInterval(refreshFaceTelemetry,2000);setInterval(refreshSnapshot,100);setInterval(()=>{if(logsAuto&&logsAuto.checked)loadLogs();},3000);refresh();refreshFaceTelemetry();loadSdList();loadLogs();try{const ws=new WebSocket('ws://'+location.hostname+':'+wsPort+'/');ws.onmessage=(e)=>{log('ws:'+e.data);};ws.onopen=()=>{connPill.textContent='live';};}catch(_e){}</script></body></html>";

    if (httpServer_ != nullptr) {
        httpServer_->send(200, "text/html", html);
    }
}

void WebUiEngine::handleApiHealth() {
    String payload = "{\"ok\":true,\"wifi\":";
    payload += wifiConnected_ ? "true" : "false";
    payload += ",\"ap_mode\":";
    payload += apMode_ ? "true" : "false";
    payload += ",\"setup_ap\":";
    payload += setupApActive_ ? "true" : "false";
    payload += ",\"ip\":\"";
    payload += localIp().toString();
    payload += "\",\"ap_ip\":\"";
    payload += WiFi.softAPIP().toString();
    payload += "\",\"uptime_ms\":";
    payload += String(millis());
    payload += "}";

    if (httpServer_ != nullptr) {
        httpServer_->send(200, "application/json", payload);
    }
}

void WebUiEngine::handleApiState() {
    if (httpServer_ != nullptr) {
        httpServer_->send(200, "application/json", dataProvider_ != nullptr ? dataProvider_() : defaultStateJson());
    }
}

void WebUiEngine::handleApiStatus() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String payload = statusProvider_ != nullptr ? statusProvider_() : defaultStatusJson();
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiSensors() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String payload = sensorsProvider_ != nullptr ? sensorsProvider_() : defaultSensorsJson();
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiEngines() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String payload = enginesProvider_ != nullptr ? enginesProvider_() : defaultEnginesJson();
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiSettings() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String request = readRequestBody();
    JsonDocument validationDocument;
    const DeserializationError validationError = deserializeJson(validationDocument, request);
    if (validationError || !validationDocument.is<JsonObject>()) {
        String payload = "{\"ok\":false,\"error\":\"invalid_settings_json\"}";
        if (validationError) {
            payload = String("{\"ok\":false,\"error\":\"invalid_settings_json\",\"message\":\"") + validationError.c_str() + "\"}";
        }
        httpServer_->send(400, "application/json", payload);
        return;
    }

    String response = "{\"ok\":false,\"error\":\"settings_handler_not_configured\",\"read_only\":true}";
    int code = 501;

    if (settingsHandler_ != nullptr) {
        const bool accepted = settingsHandler_(request, response);
        code = accepted ? 200 : 400;
    }

    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiCommand() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"command_handler_not_configured\",\"read_only\":true}";
    int code = 501;

    if (commandHandler_ != nullptr) {
        const bool accepted = commandHandler_(request, response);
        code = accepted ? 200 : 400;
    }

    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceSettingsGet() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String payload = faceSettingsProvider_ != nullptr ? faceSettingsProvider_() : String("{\"ok\":true}");
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiFaceSettingsPost() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String request = readRequestBody();
    JsonDocument validationDocument;
    const DeserializationError validationError = deserializeJson(validationDocument, request);
    if (validationError || !validationDocument.is<JsonObject>()) {
        String payload = "{\"ok\":false,\"error\":\"invalid_face_settings_json\"}";
        if (validationError) {
            payload = String("{\"ok\":false,\"error\":\"invalid_face_settings_json\",\"message\":\"") + validationError.c_str() + "\"}";
        }
        httpServer_->send(400, "application/json", payload);
        return;
    }

    String response = "{\"ok\":false,\"error\":\"face_settings_handler_not_configured\"}";
    int code = 501;
    if (faceSettingsHandler_ != nullptr) {
        const bool accepted = faceSettingsHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceStyles() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String payload = faceStylesProvider_ != nullptr ? faceStylesProvider_() : String("[]");
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiFaceAnimationsCatalog() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String payload = faceAnimationsCatalogProvider_ != nullptr
                               ? faceAnimationsCatalogProvider_()
                               : String("{\"ok\":true,\"styles\":[]}");
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiFaceAnimations() {
    if (httpServer_ == nullptr) {
        return;
    }

    if (faceAnimationsProvider_ == nullptr) {
        httpServer_->send(501, "application/json", "{\"ok\":false,\"error\":\"face_animations_provider_not_configured\"}");
        return;
    }

    String uri = httpServer_->uri();
    const String prefix = "/api/face/animations/";
    String style = uri.startsWith(prefix) ? uri.substring(prefix.length()) : String();
    style.trim();
    if (style.length() == 0) {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_face_style\"}");
        return;
    }

    httpServer_->send(200, "application/json", faceAnimationsProvider_(style));
}

void WebUiEngine::handleApiFacePreview() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"face_preview_handler_not_configured\"}";
    int code = 501;
    if (facePreviewHandler_ != nullptr) {
        const bool accepted = facePreviewHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceSetStyle() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"face_set_style_handler_not_configured\"}";
    int code = 501;
    if (faceSetStyleHandler_ != nullptr) {
        const bool accepted = faceSetStyleHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceSetAnimation() {
    if (httpServer_ == nullptr) {
        return;
    }

    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"face_set_animation_handler_not_configured\"}";
    int code = 501;
    if (faceSetAnimationHandler_ != nullptr) {
        const bool accepted = faceSetAnimationHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFacePlay() {
    if (httpServer_ == nullptr) {
        return;
    }
    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"face_play_handler_not_configured\"}";
    int code = 501;
    if (facePlayHandler_ != nullptr) {
        const bool accepted = facePlayHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceSetEmotion() {
    if (httpServer_ == nullptr) {
        return;
    }
    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"face_set_emotion_handler_not_configured\"}";
    int code = 501;
    if (faceSetEmotionHandler_ != nullptr) {
        const bool accepted = faceSetEmotionHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceReload() {
    if (httpServer_ == nullptr) {
        return;
    }
    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"face_reload_handler_not_configured\"}";
    int code = 501;
    if (faceReloadHandler_ != nullptr) {
        const bool accepted = faceReloadHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceValidate() {
    if (httpServer_ == nullptr) {
        return;
    }
    const String payload = faceValidateProvider_ != nullptr
                               ? faceValidateProvider_()
                               : String("{\"ok\":false,\"error\":\"face_validate_provider_not_configured\"}");
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiFaceTelemetry() {
    if (httpServer_ == nullptr) {
        return;
    }
    const String payload = faceTelemetryProvider_ != nullptr
                               ? faceTelemetryProvider_()
                               : String("{\"ok\":false,\"error\":\"face_telemetry_provider_not_configured\"}");
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiFaceTelemetryPost() {
    if (httpServer_ == nullptr) {
        return;
    }
    const String request = readRequestBody();
    String response = "{\"ok\":false,\"error\":\"face_telemetry_handler_not_configured\"}";
    int code = 501;
    if (faceTelemetryHandler_ != nullptr) {
        const bool accepted = faceTelemetryHandler_(request, response);
        code = accepted ? 200 : 400;
    }
    httpServer_->send(code, "application/json", response);
}

void WebUiEngine::handleApiFaceSnapshot() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }

    const String path = sanitizeSdPath(faceSnapshotPathProvider_ != nullptr ? faceSnapshotPathProvider_() : String());
    if (!isSafeSdPath(path) || path.length() == 0 || !SD.exists(path)) {
        httpServer_->send(404, "application/json", "{\"ok\":false,\"error\":\"snapshot_unavailable\"}");
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        httpServer_->send(500, "application/json", "{\"ok\":false,\"error\":\"snapshot_open_failed\"}");
        return;
    }
    httpServer_->streamFile(file, "image/png");
    file.close();
}

void WebUiEngine::handleApiSdList() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }
    String path = sanitizeSdPath(httpServer_->arg("path"));
    if (path.length() == 0) {
        path = "/Flic";
    }
    if (!isSafeSdPath(path)) {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_path\"}");
        return;
    }
    const String payload = buildSdTreeJson(path, 5);
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiSdUpload() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (uploadRejected_) {
        httpServer_->send(400,
                          "application/json",
                          String("{\"ok\":false,\"error\":\"") + uploadRejectReason_ + "\"}");
        return;
    }
    httpServer_->send(200,
                      "application/json",
                      String("{\"ok\":true,\"path\":\"") + uploadTargetPath_ + "\",\"bytes\":" +
                          String(uploadBytesReceived_) + "}");
}

void WebUiEngine::handleApiSdDelete() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, readRequestBody()) || !doc.is<JsonObject>()) {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
        return;
    }
    String path = sanitizeSdPath(String(doc["path"] | ""));
    if (!isSafeSdPath(path) || path == "/Flic") {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_path\"}");
        return;
    }
    bool ok = false;
    File target = SD.open(path);
    if (target && target.isDirectory()) {
        ok = SD.rmdir(path);
    } else {
        ok = SD.remove(path);
    }
    if (target) {
        target.close();
    }
    logSdOperation("delete", path, ok);
    httpServer_->send(ok ? 200 : 400,
                      "application/json",
                      String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void WebUiEngine::handleApiSdMkdir() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, readRequestBody()) || !doc.is<JsonObject>()) {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
        return;
    }
    const String path = sanitizeSdPath(String(doc["path"] | ""));
    if (!isSafeSdPath(path) || path.length() == 0) {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_path\"}");
        return;
    }
    const bool ok = SD.mkdir(path);
    logSdOperation("mkdir", path, ok);
    httpServer_->send(ok ? 200 : 400, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void WebUiEngine::handleApiSdRename() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, readRequestBody()) || !doc.is<JsonObject>()) {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
        return;
    }
    const String from = sanitizeSdPath(String(doc["from"] | ""));
    const String to = sanitizeSdPath(String(doc["to"] | ""));
    if (!isSafeSdPath(from) || !isSafeSdPath(to) || from.length() == 0 || to.length() == 0) {
        httpServer_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_path\"}");
        return;
    }
    const bool ok = SD.rename(from, to);
    logSdOperation("rename", from, ok, to);
    httpServer_->send(ok ? 200 : 400, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void WebUiEngine::handleApiSdDownload() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }
    const String path = sanitizeSdPath(httpServer_->arg("path"));
    if (!isSafeSdPath(path) || path.length() == 0 || !SD.exists(path)) {
        httpServer_->send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        httpServer_->send(500, "application/json", "{\"ok\":false,\"error\":\"open_failed\"}");
        return;
    }
    httpServer_->sendHeader("Content-Disposition", String("attachment; filename=") + path.substring(path.lastIndexOf('/') + 1));
    httpServer_->streamFile(file, "application/octet-stream");
    file.close();
}

void WebUiEngine::handleApiLogs() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (logsProvider_ != nullptr) {
        httpServer_->send(200, "application/json", logsProvider_());
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }
    const String subsystem = String(httpServer_->arg("subsystem"));
    const int limit = httpServer_->arg("limit").toInt() > 0 ? httpServer_->arg("limit").toInt() : 500;
    File file = SD.open("/Flic/logs/webui_sd.log", FILE_READ);
    if (!file) {
        httpServer_->send(200, "application/json", "{\"ok\":true,\"lines\":[]}");
        return;
    }
    std::vector<String> lines;
    String current;
    while (file.available()) {
        char c = static_cast<char>(file.read());
        if (c == '\n') {
            current.trim();
            if (current.length() > 0 && (subsystem.length() == 0 || current.indexOf(subsystem) >= 0)) {
                lines.push_back(current);
                if (static_cast<int>(lines.size()) > limit) {
                    lines.erase(lines.begin());
                }
            }
            current = "";
        } else {
            current += c;
        }
    }
    file.close();
    String payload = "{\"ok\":true,\"lines\":[";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            payload += ",";
        }
        payload += "\"";
        payload += escapeJson(lines[i]);
        payload += "\"";
    }
    payload += "]}";
    httpServer_->send(200, "application/json", payload);
}

void WebUiEngine::handleApiLogsClear() {
    if (httpServer_ == nullptr) {
        return;
    }
    if (!Flic::SdManager::isMounted()) {
        httpServer_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_mounted\"}");
        return;
    }

    const char* logPath = "/Flic/logs/webui_sd.log";
    if (SD.exists(logPath)) {
        SD.remove(logPath);
    }
    File file = SD.open(logPath, FILE_WRITE);
    if (file) {
        file.println(String(millis()) + " [web] logs cleared");
        file.close();
    }
    httpServer_->send(200, "application/json", "{\"ok\":true,\"cleared\":true}");
}

String WebUiEngine::sanitizeSdPath(const String& input) const {
    String path = input;
    path.trim();
    path.replace("\\", "/");
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    if (path.length() == 0) {
        return String();
    }
    if (!path.startsWith("/")) {
        path = "/" + path;
    }
    return path;
}

bool WebUiEngine::isSafeSdPath(const String& path) const {
    if (path.length() == 0 || !path.startsWith("/")) {
        return false;
    }
    if (path.indexOf("..") >= 0) {
        return false;
    }
    return path.startsWith("/Flic") || path.startsWith("/ai");
}

String WebUiEngine::buildSdTreeJson(const String& path, uint8_t depth) const {
    String safePath = sanitizeSdPath(path);
    if (!isSafeSdPath(safePath)) {
        return "{\"ok\":false,\"error\":\"invalid_path\"}";
    }
    File root = SD.open(safePath);
    if (!root) {
        return "{\"ok\":false,\"error\":\"not_found\"}";
    }
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    if (Flic::SdManager::isMounted()) {
        totalBytes = SD.totalBytes();
        usedBytes = SD.usedBytes();
    }

    String payload = "{\"ok\":true,\"mounted\":true,\"path\":\"" + safePath + "\",\"total_bytes\":";
    payload += String(static_cast<unsigned long long>(totalBytes));
    payload += ",\"used_bytes\":";
    payload += String(static_cast<unsigned long long>(usedBytes));
    payload += ",\"free_bytes\":";
    payload += String(static_cast<unsigned long long>(totalBytes > usedBytes ? (totalBytes - usedBytes) : 0));
    payload += ",\"entries\":[";
    bool first = true;
    File entry = root.openNextFile();
    while (entry) {
        if (!first) {
            payload += ",";
        }
        first = false;
        const String name = String(entry.name());
        const String fullPath = safePath.endsWith("/") ? (safePath + name) : (safePath + "/" + name);
        payload += "{\"name\":\"" + name + "\",\"path\":\"" + fullPath + "\",\"dir\":";
        payload += entry.isDirectory() ? "true" : "false";
        const uint32_t ts = static_cast<uint32_t>(entry.getLastWrite());
        payload += ",\"size\":" + String(entry.size()) + ",\"timestamp\":" + String(ts);
        if (entry.isDirectory() && depth > 1) {
            payload += ",\"children\":";
            payload += buildSdTreeJson(fullPath, static_cast<uint8_t>(depth - 1));
        }
        payload += "}";
        entry = root.openNextFile();
        yield();
    }
    root.close();
    payload += "]}";
    return payload;
}

bool WebUiEngine::validatePngUpload(const String& path, String& reason) const {
    reason = "";
    if (!path.endsWith(".png")) {
        return true;
    }
    const String name = path.substring(path.lastIndexOf('/') + 1);
    if (!name.startsWith("frame_") || !name.endsWith(".png") || name.length() != 13) {
        reason = "invalid_png_name_pattern_frame_XXX_png";
        return false;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        reason = "png_open_failed";
        return false;
    }
    uint8_t header[33] = {};
    const size_t n = file.read(header, sizeof(header));
    file.close();
    if (n < sizeof(header)) {
        reason = "png_header_too_small";
        return false;
    }
    static constexpr uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (uint8_t i = 0; i < 8; ++i) {
        if (header[i] != kSig[i]) {
            reason = "invalid_png_signature";
            return false;
        }
    }
    const uint32_t width = (static_cast<uint32_t>(header[16]) << 24) | (static_cast<uint32_t>(header[17]) << 16) |
                           (static_cast<uint32_t>(header[18]) << 8) | static_cast<uint32_t>(header[19]);
    const uint32_t height = (static_cast<uint32_t>(header[20]) << 24) | (static_cast<uint32_t>(header[21]) << 16) |
                            (static_cast<uint32_t>(header[22]) << 8) | static_cast<uint32_t>(header[23]);
    const uint8_t colorType = header[25];
    if (width != 240 || height != 240) {
        reason = "png_must_be_240x240";
        return false;
    }
    if (colorType != 6) {
        reason = "png_must_be_rgba_with_alpha";
        return false;
    }
    return true;
}

void WebUiEngine::logSdOperation(const String& op, const String& path, bool ok, const String& detail) const {
    if (!Flic::SdManager::isMounted()) {
        return;
    }
    File file = SD.open("/Flic/logs/webui_sd.log", FILE_APPEND);
    if (!file) {
        return;
    }
    String line = String(millis()) + " [sd] " + op + " path=" + path + " ok=" + (ok ? "1" : "0");
    if (detail.length() > 0) {
        line += " detail=" + detail;
    }
    file.println(line);
    file.close();
}

void WebUiEngine::handleApiWildcard() {
    String payload = "{\"ok\":false,\"error\":\"unknown_api\",\"path\":\"";
    payload += (httpServer_ != nullptr ? httpServer_->uri() : String("/api/unknown"));
    payload += "\"}";

    if (httpServer_ != nullptr) {
        httpServer_->send(404, "application/json", payload);
    }
}

void WebUiEngine::handleWsInfo() {
    String payload = "{\"ok\":true,\"ws_port\":";
    payload += String(wsPort_);
    payload += ",\"note\":\"Connect using WebSocket to ws://<device-ip>:<ws_port>/\"}";
    if (httpServer_ != nullptr) {
        httpServer_->send(200, "application/json", payload);
    }
}

String WebUiEngine::defaultStateJson() const {
    String payload = "{\"type\":\"state\",\"uptime_ms\":";
    payload += String(millis());
    payload += ",\"wifi\":";
    payload += wifiConnected_ ? "true" : "false";
    payload += ",\"ip\":\"";
    payload += localIp().toString();
    payload += "\",\"ap_mode\":";
    payload += apMode_ ? "true" : "false";
    payload += ",\"setup_ap\":";
    payload += setupApActive_ ? "true" : "false";
    payload += "}";
    return payload;
}

String WebUiEngine::defaultStatusJson() const {
    String payload = "{\"ok\":true,\"type\":\"status\",\"ready\":";
    payload += ready_ ? "true" : "false";
    payload += ",\"wifi\":";
    payload += wifiConnected_ ? "true" : "false";
    payload += ",\"ip\":\"";
    payload += localIp().toString();
    payload += "\",\"ap_mode\":";
    payload += apMode_ ? "true" : "false";
    payload += ",\"setup_ap\":";
    payload += setupApActive_ ? "true" : "false";
    payload += ",\"uptime_ms\":";
    payload += String(millis());
    payload += "}";
    return payload;
}

String WebUiEngine::defaultSensorsJson() const {
    return "{\"ok\":true,\"type\":\"sensors\",\"imu\":null,\"light\":null,\"touch\":null}";
}

String WebUiEngine::defaultEnginesJson() const {
    return "{\"ok\":true,\"type\":\"engines\",\"touch\":null,\"imu\":null,\"light\":null,\"camera\":null,\"audio\":null,\"usb\":null,\"communication\":null,\"emotion\":null,\"animation\":null,\"milestone\":null,\"learning\":null}";
}

String WebUiEngine::readRequestBody() const {
    if (httpServer_ == nullptr) {
        return String();
    }

    if (httpServer_->hasArg("plain")) {
        return httpServer_->arg("plain");
    }

    // Fallback for form-style POSTs.
    String body = "{";
    for (uint8_t i = 0; i < httpServer_->args(); ++i) {
        if (i > 0) {
            body += ',';
        }
        body += '"';
        body += httpServer_->argName(i);
        body += "\":\"";
        body += httpServer_->arg(i);
        body += '"';
    }
    body += '}';
    return body;
}

}  // namespace Flic
