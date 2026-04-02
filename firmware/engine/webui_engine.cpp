#include "webui_engine.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>

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
    html += ":root{--bg:#07090f;--card:#101522;--line:#2b3552;--txt:#d8e1ff;--muted:#8fa2d8;--accent:#5fffe0;--warn:#ffd166}";
    html += "*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 10% 10%,#101933 0,#07090f 50%);color:var(--txt);font-family:Segoe UI,system-ui,sans-serif}";
    html += "header{display:flex;justify-content:space-between;align-items:center;padding:14px 16px;border-bottom:1px solid #233154;background:#0d1320cc;position:sticky;top:0}";
    html += "h1{font-size:16px;margin:0;letter-spacing:.4px}.pill{padding:4px 10px;border:1px solid var(--line);border-radius:999px;color:var(--muted);font-size:12px}";
    html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px;padding:12px}.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px}";
    html += "h2{font-size:14px;margin:0 0 10px;color:#a8b8e6}.stat{font-family:Consolas,monospace;font-size:14px;line-height:1.45}.mono{font-family:Consolas,monospace}";
    html += "button{cursor:pointer;border:1px solid #2f4476;background:#13203e;color:#dfe8ff;border-radius:10px;padding:9px 10px;font-weight:600}button:hover{filter:brightness(1.1)}";
    html += ".btns{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.btn-main{background:#17424b;border-color:#2a6f7d;color:#d9fff9}";
    html += "label{display:block;font-size:12px;color:var(--muted);margin:8px 0 4px}input[type=range],input[type=text]{width:100%}";
    html += "input[type=text]{background:#0a1120;color:#dbe6ff;border:1px solid #2a3a60;border-radius:8px;padding:8px}";
    html += "pre{white-space:pre-wrap;word-break:break-word;background:#060b16;padding:10px;border-radius:8px;min-height:130px;border:1px solid #1f2d4d}";
    html += "small{color:var(--muted)}.ok{color:var(--accent)}.warn{color:var(--warn)}</style></head><body>";
    html += "<header><h1>Flic Pager UI</h1><div class='pill' id='connPill'>connecting...</div></header><main class='grid'>";
    html += "<section class='card'><h2>Connection</h2><div id='netInfo' class='stat mono'>Loading...</div><small>If AP mode, connect to SSID <b>Flic-Setup</b> (PW: <b>flic1234</b>) then open <b>http://192.168.4.1</b>.</small></section>";
    html += "<section class='card'><h2>Quick Pager Commands</h2><div class='btns'><button class='btn-main' onclick='sendCmd(\"show webui info\")'>Show WebUI Info</button><button onclick='sendCmd(\"hello\")'>Hello</button><button onclick='sendCmd(\"what can you do\")'>Capabilities</button><button onclick='sendCmd(\"what is your name\")'>Name</button></div><label>Custom command</label><input id='cmd' type='text' placeholder='Type a message for Flic'><button style='margin-top:8px;width:100%' onclick='sendCustom()'>Send</button></section>";
    html += "<section class='card'><h2>Device Mic</h2><button onclick='sendCmd(\"listen\")'>Trigger Listen</button><div id='speech' class='stat' style='margin-top:8px'>Using built-in device mic</div><small>Browser speech is optional and may be blocked by browser permissions.</small><button style='margin-top:8px' onclick='speechOnce()'>Try Browser Speech (Optional)</button></section>";
    html += "<section class='card'><h2>Settings</h2><label>Brightness <span id='brightnessValue'>20</span></label><input id='brightness' type='range' min='0' max='100' value='20' oninput='brightnessValue.textContent=this.value'>";
    html += "<label>Volume <span id='volumeValue'>180</span></label><input id='volume' type='range' min='0' max='255' value='180' oninput='volumeValue.textContent=this.value'>";
    html += "<label>Voice style</label><select id='voiceStyle'><option value='natural'>Natural</option><option value='clear'>Clear</option><option value='bright'>Bright</option><option value='deep'>Deep</option><option value='warm'>Warm</option></select>";
    html += "<label><input id='rtVoiceInput' type='checkbox' checked> Voice input enabled</label>";
    html += "<label><input id='rtAutonomy' type='checkbox' checked> Autonomy enabled</label>";
    html += "<label><input id='rtHeartbeat' type='checkbox' checked> Web heartbeat enabled</label>";
    html += "<label><input id='rtImu' type='checkbox' checked> IMU events enabled</label>";
    html += "<label><input id='rtUsb' type='checkbox' checked> USB events enabled</label>";
    html += "<label><input id='browserTts' type='checkbox' checked> Speak replies in browser (high quality)</label>";
    html += "<label>Browser voice</label><select id='browserVoice'></select>";
    html += "<button style='margin-top:10px;width:100%' onclick='saveSettings()'>Apply Settings</button></section>";
    html += "<section class='card' style='grid-column:1/-1'><h2>Live Console</h2><pre id='out'>connecting...</pre></section></main>";
    html += "<script>const out=document.getElementById('out');const wsPort=";
    html += String(wsPort_);
    html += ";const netInfo=document.getElementById('netInfo');const connPill=document.getElementById('connPill');const logLines=[];";
    html += "function log(x){const line=(typeof x==='string'?x:JSON.stringify(x));logLines.push(line);while(logLines.length>40){logLines.shift();}out.textContent=logLines.join('\\n');}async function api(path,opt){const r=await fetch(path,opt);const t=await r.text();try{return JSON.parse(t);}catch{return {ok:r.ok,raw:t,status:r.status};}}";
    html += "let voiceLoaded=false;function loadVoices(){if(!window.speechSynthesis||!browserVoice)return;const voices=window.speechSynthesis.getVoices()||[];if(!voices.length)return;const current=browserVoice.value;browserVoice.innerHTML='';for(let i=0;i<voices.length;i++){const v=voices[i];const o=document.createElement('option');o.value=v.name;o.textContent=v.name+' ('+v.lang+')';browserVoice.appendChild(o);}let pref='';for(const v of voices){if((v.name||'').toLowerCase().includes('google')){pref=v.name;break;}}if(current&&[...browserVoice.options].some(o=>o.value===current)){browserVoice.value=current;}else if(pref){browserVoice.value=pref;}voiceLoaded=true;}";
    html += "function speakBrowser(text){if(!text||!browserTts||!browserTts.checked)return;if(!window.speechSynthesis||!window.SpeechSynthesisUtterance){log('Browser TTS unavailable');return;}if(!voiceLoaded)loadVoices();try{const u=new SpeechSynthesisUtterance(String(text));const voices=window.speechSynthesis.getVoices()||[];let selected=null;if(browserVoice&&browserVoice.value){selected=voices.find(v=>v.name===browserVoice.value)||null;}if(!selected){selected=voices.find(v=>(v.name||'').toLowerCase().includes('google'))||voices[0]||null;}if(selected)u.voice=selected;u.rate=1.0;u.pitch=1.0;u.volume=1.0;window.speechSynthesis.cancel();window.speechSynthesis.speak(u);}catch(e){log('Browser TTS error: '+String(e));}}";
    html += "async function refresh(){try{const h=await api('/api/health');const s=await api('/api/status');const ip=(h&&h.ip)?h.ip:'-';const apFallback=!!(h&&h.ap_mode);const setupAp=!!(h&&h.setup_ap);const apIp=(h&&h.ap_ip)?h.ap_ip:'192.168.4.1';connPill.textContent=apFallback?'AP MODE':(setupAp?'ONLINE + AP':'ONLINE');connPill.className='pill '+((apFallback||setupAp)?'warn':'ok');netInfo.innerHTML='Mode: <b>'+(apFallback?'AP fallback':'WiFi STA')+'</b><br>IP: <b>'+ip+'</b><br>Setup AP: <b>'+(setupAp?'ON':'OFF')+'</b><br>AP IP: <b>'+apIp+'</b><br>HTTP: <b>80</b><br>WS: <b>'+wsPort+'</b>';if(s&&s.settings){brightness.value=s.settings.brightness??brightness.value;brightnessValue.textContent=brightness.value;volume.value=s.settings.volume??volume.value;volumeValue.textContent=volume.value;voiceStyle.value=s.settings.voice_style??voiceStyle.value;}if(s&&s.runtime){rtVoiceInput.checked=!!s.runtime.voice_input;rtAutonomy.checked=!!s.runtime.autonomy;rtHeartbeat.checked=!!s.runtime.web_heartbeat;rtImu.checked=!!s.runtime.imu_events;rtUsb.checked=!!s.runtime.usb_events;}}catch(e){connPill.textContent='offline';netInfo.textContent='Unable to fetch API.';log(String(e));}}";
    html += "async function sendCmd(text){const body={text,source:'web_pager'};const r=await api('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});log('cmd:'+text+' => '+JSON.stringify(r));if(r&&r.reply){speakBrowser(r.reply);}}function sendCustom(){const el=document.getElementById('cmd');const v=(el.value||'').trim();if(!v)return;sendCmd(v);el.value='';}";
    html += "async function saveSettings(){const style=String(voiceStyle.value||'natural');const payload={brightness:Number(brightness.value),volume:Number(volume.value),voice_style:style,runtime:{voice_input:!!rtVoiceInput.checked,autonomy:!!rtAutonomy.checked,web_heartbeat:!!rtHeartbeat.checked,imu_events:!!rtImu.checked,usb_events:!!rtUsb.checked}};const r=await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});log('settings => '+JSON.stringify(r));if(r&&r.ok){const vr=await api('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text:'voice style '+style,source:'web_pager'})});log('voice style confirm => '+JSON.stringify(vr));if(vr&&vr.reply){speakBrowser(vr.reply);}}refresh();}";
    html += "async function ensureMicPermission(){if(!navigator.mediaDevices||!navigator.mediaDevices.getUserMedia){return true;}try{const s=await navigator.mediaDevices.getUserMedia({audio:true});s.getTracks().forEach(t=>t.stop());return true;}catch(e){return false;}}";
    html += "async function speechOnce(){const SR=window.SpeechRecognition||window.webkitSpeechRecognition;const info=document.getElementById('speech');if(!SR){info.textContent='speech API unavailable in this browser';return;}const micOk=await ensureMicPermission();if(!micOk){info.textContent='microphone permission denied';return;}if(location.hostname==='192.168.4.1'){info.textContent='AP mode: speech may fail; prefer home Wi-Fi IP';}const rec=new SR();rec.lang='en-US';rec.interimResults=false;rec.maxAlternatives=1;info.textContent='listening...';rec.onresult=(ev)=>{const t=(ev.results&&ev.results[0]&&ev.results[0][0]&&ev.results[0][0].transcript)||'';info.textContent=t||'no speech';if(t){sendCmd(t);}};rec.onerror=(ev)=>{const err=(ev&&ev.error)?ev.error:'unknown';if(err==='not-allowed'){info.textContent='speech blocked: allow mic in browser site settings, then reload';}else{info.textContent='speech error: '+err;}};rec.onend=()=>{if(info.textContent==='listening...')info.textContent='idle';};try{rec.start();}catch(e){info.textContent='speech start failed';}}";
    html += "if(window.speechSynthesis){window.speechSynthesis.onvoiceschanged=loadVoices;setTimeout(loadVoices,300);}setInterval(refresh,5000);refresh();try{const ws=new WebSocket('ws://'+location.hostname+':'+wsPort+'/');ws.onmessage=(e)=>{log('ws:'+e.data);};ws.onopen=()=>{connPill.textContent='live';};}catch(_e){}</script></body></html>";

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
