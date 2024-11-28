#ifndef DEVICEHUB_H
#define DEVICEHUB_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include "OtaHelper.h"

enum class DeviceState {
    Normal,
    Emergency
};

class DeviceHub {
public:
    using ActionCallback = std::function<String(const JsonObject&)>;

    DeviceHub(const char* ssid, const char* password, const char* deviceName);
    void begin();
    void loop();
    
    void registerAction(const String& actionName, ActionCallback callback);
    void registerEmergencyAction(ActionCallback callback);
    void registerResetAction(ActionCallback callback);
    void sendMessage(const String& message, const String& type = "device_message");
    void sendEmergency(const String& message);

private:
    static const uint16_t LOCAL_PORT = 8888;
    static const uint16_t HUB_PORT = 8889;
    static const uint16_t EMERGENCY_PORT = 8890;
    static const uint16_t EMERGENCY_NOTIFICATION_PORT = 8891;
    static const IPAddress BROADCAST_IP;  // New constant for broadcast IP

    const char* ssid;
    const char* password;
    const char* deviceName;
    
    WiFiUDP udp;
    WiFiUDP emergencyUdp;
    
    OtaHelper otaHelper;

    std::map<String, ActionCallback> actions;
    std::vector<String> actionNames;
    ActionCallback emergencyAction;
    ActionCallback resetAction;

    DeviceState currentState;

    struct PendingMessage {
        String payload;
        unsigned long timestamp;
        int retries;
    };
    std::map<String, PendingMessage> pendingAcks;

    void connectWiFi();
    void handleIncomingPacket();
    void handleEmergencyPacket();
    void handleEmergencyStart();
    void handleEmergencyEnd();
    void performEmergencyActions();
    void returnToNormalState();
    void sendEmergencyResponse(const char* message);
    void sendDeviceInfo();
    String getDeviceInfo();
    void resendPendingMessages();
    String handleAction(const String& actionName, const JsonObject& payload);
    void sendAck(const String& messageId);
    void sendPeriodicUpdate();
    void sendUdpMessage(const String& payload);
    
};

#endif // DEVICEHUB_H