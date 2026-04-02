#include "usb_engine.h"

#include "sd_manager.h"

#include <ArduinoJson.h>
#include <M5Unified.h>

namespace Flic {
namespace {
constexpr const char* kPermissionsPath = "/permissions.json";
constexpr const char* kConnectedDevicesPath = "/Flic/memory/connected_devices.json";
constexpr const char* kDeviceIdPrefix = "DEVICE_ID:";
constexpr size_t kDeviceIdPrefixLength = 10;
constexpr const char* kDeviceTypePrefix = "DEVICE_TYPE:";
constexpr size_t kDeviceTypePrefixLength = 12;
constexpr const char* kCapabilitiesPrefix = "CAPABILITIES:";
constexpr size_t kCapabilitiesPrefixLength = 13;
constexpr const char* kCapsShortPrefix = "CAPS:";
constexpr size_t kCapsShortPrefixLength = 5;
constexpr const char* kFeaturesPrefix = "FEATURES:";
constexpr size_t kFeaturesPrefixLength = 9;
constexpr const char* kBaudPrefix = "BAUD:";
constexpr size_t kBaudPrefixLength = 5;
constexpr uint32_t kHelloRetryIntervalMs = 1200;

void appendCapabilities(const String& capabilitiesCsv, JsonArray capabilities) {
    capabilities.clear();
    if (capabilitiesCsv.length() == 0) {
        return;
    }

    int start = 0;
    while (start < capabilitiesCsv.length()) {
        int comma = capabilitiesCsv.indexOf(',', start);
        if (comma < 0) {
            comma = capabilitiesCsv.length();
        }
        String token = capabilitiesCsv.substring(start, comma);
        token.trim();
        if (token.length() > 0) {
            capabilities.add(token);
        }
        start = comma + 1;
    }
}

bool isSafeUsbCommand(const String& command) {
    if (command.length() == 0) {
        return false;
    }

    String check = command;
    check.toUpperCase();
    if (check.indexOf("FLASH") >= 0 || check.indexOf("ERASE") >= 0 || check.indexOf("FORMAT") >= 0 ||
        check.indexOf("DELETE") >= 0 || check.indexOf("RM ") >= 0 || check.indexOf("FACTORY_RESET") >= 0) {
        return false;
    }

    return true;
}
}

bool UsbEngine::begin(uint32_t baudRate) {
    baudRate_ = baudRate == 0 ? 115200 : baudRate;
    Serial.begin(baudRate_);
    Serial.setTimeout(10);
    loadPermissions();
    connected_ = false;
    connectionJustEstablished_ = false;
    handshakeSent_ = false;
    handshakeComplete_ = false;
    lastHelloSentMs_ = 0;
    return true;
}

bool UsbEngine::deviceConnected() {
    const bool serialConnected = static_cast<bool>(Serial);
    connectionJustEstablished_ = serialConnected && !connected_;
    if (!serialConnected && connected_) {
        handshakeSent_ = false;
        handshakeComplete_ = false;
        lastHelloSentMs_ = 0;
        deviceId_ = String();
        deviceType_ = "unknown";
        capabilitiesCsv_ = String();
        readLine_ = String();
    }
    connected_ = serialConnected;

    if (connectionJustEstablished_) {
        handshakeSent_ = false;
        handshakeComplete_ = false;
    }

    if (connected_ && !handshakeSent_) {
        sendMessage("FLIC_HELLO");
        handshakeSent_ = true;
        lastHelloSentMs_ = millis();
    } else if (connected_ && !handshakeComplete_ && (millis() - lastHelloSentMs_ >= kHelloRetryIntervalMs)) {
        sendMessage("FLIC_HELLO");
        lastHelloSentMs_ = millis();
    }

    return connected_;
}

bool UsbEngine::connectionJustEstablished() {
    const bool result = connectionJustEstablished_;
    connectionJustEstablished_ = false;
    return result;
}

void UsbEngine::sendMessage(const String& msg) {
    if (!connected_) {
        return;
    }

    if (!isHandshakeMessage(msg) && !canSendCommand(msg)) {
        Serial.println("Flic: USB command blocked by permissions or approval.");
        return;
    }

    Serial.println(msg);
}

void UsbEngine::sendBinaryPacket(uint8_t command, const uint8_t* payload, size_t payloadLength) {
    if (!connected_ || payloadLength > 240 || payload == nullptr) {
        return;
    }

    if (!canSendCommand(String("BIN_") + command)) {
        Serial.println("Flic: binary USB packet blocked by permissions or approval.");
        return;
    }

    Serial.write(0x7E);
    Serial.write(static_cast<uint8_t>(payloadLength + 1));
    Serial.write(command);
    for (size_t i = 0; i < payloadLength; ++i) {
        Serial.write(payload[i]);
    }
}

String UsbEngine::readMessage() {
    if (!connected_) {
        return String();
    }

    while (Serial.available() > 0) {
        const char incoming = static_cast<char>(Serial.read());
        const uint8_t incomingByte = static_cast<uint8_t>(incoming);

        if (incomingByte == 0x7E) {
            return readBinaryAsText(incomingByte);
        }

        if (incomingByte < 0x20 && incomingByte != '\r' && incomingByte != '\n' && incomingByte != '\t') {
            return readBinaryAsText(incomingByte);
        }

        if (incoming == '\r') {
            continue;
        }
        if (incoming == '\n') {
            String message = readLine_;
            readLine_ = String();
            message.trim();

            if (markHandshakeFromMessage(message)) {
                return String();
            } else if (message.startsWith(kDeviceIdPrefix)) {
                deviceId_ = message.substring(kDeviceIdPrefixLength);
                deviceId_.trim();
                if (deviceId_.length() > 0) {
                    deviceType_ = deviceId_;
                    saveConnectedDevice(deviceId_, deviceType_, capabilitiesCsv_);
                }
            } else if (message.startsWith(kDeviceTypePrefix)) {
                deviceType_ = message.substring(kDeviceTypePrefixLength);
                deviceType_.trim();
            } else if (parseCapabilitiesFromMessage(message, capabilitiesCsv_)) {
                if (deviceId_.length() > 0) {
                    saveConnectedDevice(deviceId_, deviceType_, capabilitiesCsv_);
                }
            } else if (message.startsWith(kBaudPrefix)) {
                const uint32_t requestedBaud = static_cast<uint32_t>(message.substring(kBaudPrefixLength).toInt());
                negotiateBaudRate(requestedBaud);
            }

            return message;
        }
        readLine_ += incoming;
        if (readLine_.length() > 240) {
            readLine_.remove(0, readLine_.length() - 240);
        }
    }

    return String();
}

String UsbEngine::connectedDeviceId() const {
    return deviceId_;
}

String UsbEngine::connectedCapabilitiesCsv() const {
    return capabilitiesCsv_;
}

bool UsbEngine::canSendCommand(const String& command) const {
    if (!connected_ || command.length() == 0) {
        return false;
    }

    if (!isHandshakeMessage(command) && (!handshakeComplete_ || deviceId_.length() == 0)) {
        return false;
    }

    if (!isControlAllowed()) {
        return false;
    }

    if (!isApprovalRequired()) {
        return true;
    }

    return isCommandApproved(deviceId_, command);
}

bool UsbEngine::sendApprovedCommand(const String& command) {
    if (!canSendCommand(command)) {
        return false;
    }

    sendMessage(command);
    return true;
}

void UsbEngine::loadPermissions() {
    JsonDocument document;
    if (!SdManager::readJSON(kPermissionsPath, document)) {
        controlAllowed_ = false;
        approvalRequired_ = true;
        return;
    }

    controlAllowed_ = document["allow_usb_device_control"] | false;
    approvalRequired_ = document["require_approval_for_usb_commands"] | true;
}

bool UsbEngine::isHandshakeMessage(const String& message) const {
    return message == "FLIC_HELLO" || message == "HELLO" || message == "HELLO_ACK" ||
           message == "FLIC_HELLO_ACK" || message.startsWith(kDeviceIdPrefix) ||
           message.startsWith(kDeviceTypePrefix) || message.startsWith(kCapabilitiesPrefix) ||
           message.startsWith(kCapsShortPrefix) || message.startsWith(kFeaturesPrefix) ||
           message.startsWith("USB_PING") || message.startsWith("USB_PONG") || message.startsWith(kBaudPrefix);
}

bool UsbEngine::markHandshakeFromMessage(const String& message) {
    if (message == "HELLO" || message == "HELLO_ACK" || message == "FLIC_HELLO" ||
        message == "FLIC_HELLO_ACK" || message == "USB_PONG" || message == "USB_PING") {
        handshakeComplete_ = true;
        return true;
    }

    if (message.startsWith(kDeviceIdPrefix)) {
        handshakeComplete_ = true;
    }

    return false;
}

bool UsbEngine::parseCapabilitiesFromMessage(const String& message, String& outCsv) const {
    if (message.startsWith(kCapabilitiesPrefix)) {
        outCsv = message.substring(kCapabilitiesPrefixLength);
    } else if (message.startsWith(kCapsShortPrefix)) {
        outCsv = message.substring(kCapsShortPrefixLength);
    } else if (message.startsWith(kFeaturesPrefix)) {
        outCsv = message.substring(kFeaturesPrefixLength);
    } else {
        return false;
    }

    outCsv.trim();
    return true;
}

bool UsbEngine::isControlAllowed() const {
    return controlAllowed_;
}

bool UsbEngine::isApprovalRequired() const {
    return approvalRequired_;
}

bool UsbEngine::isCommandApproved(const String& deviceId, const String& command) const {
    JsonDocument proposalsDocument;
    if (!SdManager::readJSON("/Flic/memory/proposals.json", proposalsDocument)) {
        return false;
    }

    if (!proposalsDocument.is<JsonObject>()) {
        return false;
    }

    JsonArray proposals = proposalsDocument["proposals"].as<JsonArray>();
    if (proposals.isNull()) {
        return false;
    }

    for (JsonVariant value : proposals) {
        JsonObject entry = value.as<JsonObject>();
        const String entryType = entry["type"] | "";
        const String entryDevice = entry["device"] | "";
        const String entryCommand = entry["command"] | "";
        const String pathScope = entry["path_scope"] | "";
        const bool approved = entry["approved"] | false;
        const bool sent = entry["sent"] | false;
        const bool requiresApproval = entry["requires_approval"] | false;
        if (entryType == "usb_command" && approved && !sent && requiresApproval && pathScope == "/Flic" &&
            entryDevice == deviceId && entryCommand == command && isSafeUsbCommand(entryCommand)) {
            return true;
        }
    }

    return false;
}

void UsbEngine::saveConnectedDevice(const String& deviceId, const String& deviceType, const String& capabilitiesCsv) {
    if (deviceId.length() == 0) {
        return;
    }

    JsonDocument document;
    SdManager::readJSON(kConnectedDevicesPath, document);
    if (!document.is<JsonObject>()) {
        document.clear();
        document.to<JsonObject>();
    }

    JsonArray devices = document["devices"].to<JsonArray>();
    bool updated = false;
    for (JsonVariant value : devices) {
        JsonObject entry = value.as<JsonObject>();
        if (String(entry["device_id"] | "") == deviceId) {
            entry["device_type"] = deviceType;
            entry["last_seen"] = millis();
            entry["baud_rate"] = baudRate_;
            entry["transport"] = "usb_cdc";
            JsonArray capabilities = entry["capabilities"].to<JsonArray>();
            appendCapabilities(capabilitiesCsv, capabilities);
            updated = true;
            break;
        }
    }

    if (!updated) {
        JsonObject entry = devices.add<JsonObject>();
        entry["device_id"] = deviceId;
        entry["device_type"] = deviceType;
        entry["last_seen"] = millis();
        entry["baud_rate"] = baudRate_;
        entry["transport"] = "usb_cdc";
        JsonArray capabilities = entry["capabilities"].to<JsonArray>();
        appendCapabilities(capabilitiesCsv, capabilities);
    }

    SdManager::writeJSON(kConnectedDevicesPath, document);
}

void UsbEngine::negotiateBaudRate(uint32_t baudRate) {
    if (baudRate == 0 || baudRate == baudRate_) {
        return;
    }

    baudRate_ = baudRate;
    Serial.end();
    delay(20);
    Serial.begin(baudRate_);
    Serial.setTimeout(10);
    Serial.println(String("BAUD_OK:") + baudRate_);
}

String UsbEngine::readBinaryAsText(uint8_t firstByte) {
    if (firstByte == 0x7E) {
        if (Serial.available() < 2) {
            return String();
        }

        const uint8_t packetLen = static_cast<uint8_t>(Serial.read());
        if (packetLen == 0 || packetLen > 240) {
            return String("BINARY_INVALID");
        }

        unsigned long start = millis();
        while (Serial.available() < packetLen && millis() - start < 25) {
            delay(1);
        }
        if (Serial.available() < packetLen) {
            return String("BINARY_TIMEOUT");
        }

        const uint8_t cmd = static_cast<uint8_t>(Serial.read());
        String payloadHex;
        for (uint8_t i = 1; i < packetLen; ++i) {
            const uint8_t b = static_cast<uint8_t>(Serial.read());
            if (b < 16) {
                payloadHex += '0';
            }
            payloadHex += String(b, HEX);
        }
        payloadHex.toUpperCase();
        return String("BINARY:") + String(cmd, HEX) + ":" + payloadHex;
    }

    String payloadHex;
    if (firstByte < 16) {
        payloadHex += '0';
    }
    payloadHex += String(firstByte, HEX);
    while (Serial.available() > 0) {
        const uint8_t b = static_cast<uint8_t>(Serial.read());
        if (b < 16) {
            payloadHex += '0';
        }
        payloadHex += String(b, HEX);
    }
    payloadHex.toUpperCase();
    return String("BINARY_RAW:") + payloadHex;
}

}  // namespace Flic
