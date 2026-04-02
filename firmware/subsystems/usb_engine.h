#pragma once

#include <Arduino.h>

namespace Flic {

class UsbEngine {
public:
    bool begin(uint32_t baudRate = 115200);
    bool deviceConnected();
    bool connectionJustEstablished();
    void sendMessage(const String& msg);
    void sendBinaryPacket(uint8_t command, const uint8_t* payload, size_t payloadLength);
    String readMessage();
    String connectedDeviceId() const;
    String connectedCapabilitiesCsv() const;
    bool canSendCommand(const String& command) const;
    bool sendApprovedCommand(const String& command);

private:
    void loadPermissions();
    bool isHandshakeMessage(const String& message) const;
    bool markHandshakeFromMessage(const String& message);
    bool parseCapabilitiesFromMessage(const String& message, String& outCsv) const;
    bool isControlAllowed() const;
    bool isApprovalRequired() const;
    bool isCommandApproved(const String& deviceId, const String& command) const;
    void saveConnectedDevice(const String& deviceId, const String& deviceType, const String& capabilitiesCsv = String());
    void negotiateBaudRate(uint32_t baudRate);
    String readBinaryAsText(uint8_t firstByte);

    String readLine_;
    String deviceId_;
    String deviceType_ = "unknown";
    String capabilitiesCsv_;
    uint32_t baudRate_ = 115200;
    bool connected_ = false;
    bool connectionJustEstablished_ = false;
    bool handshakeSent_ = false;
    bool handshakeComplete_ = false;
    uint32_t lastHelloSentMs_ = 0;
    bool controlAllowed_ = false;
    bool approvalRequired_ = true;
};

}  // namespace Flic
