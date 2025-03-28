#include "DeviceHub.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <Preferences.h>

Preferences prefs;
const IPAddress DeviceHub::BROADCAST_IP(255, 255, 255, 255);

DeviceHub::DeviceHub(const char* ssid, const char* password, const char* deviceName)
    : ssid(ssid), password(password), deviceName(deviceName), currentState(DeviceState::Normal) {}

void DeviceHub::begin() {
    Serial.begin(115200);
    connectWiFi();
    
    if (udp.begin(LOCAL_PORT)) {
        Serial.printf("UDP socket bound to port %d\n", LOCAL_PORT);
    } else {
        Serial.println("Failed to bind UDP socket");
    }

    if (emergencyUdp.begin(EMERGENCY_PORT)) {
        Serial.printf("Emergency UDP socket bound to port %d\n", EMERGENCY_PORT);
    } else {
        Serial.println("Failed to bind emergency UDP socket");
    }
    
    otaHelper.start(ssid, password, deviceName, password, 3232, 115200);
}

void DeviceHub::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
    
    handleIncomingPacket();
    handleEmergencyPacket();
    resendPendingMessages();
    sendPeriodicUpdate();
    otaHelper.handle();

    // Perform state-specific actions
    switch (currentState) {
        case DeviceState::Normal:
            // Perform normal state actions
            break;
        case DeviceState::Emergency:
            // Perform continuous emergency state actions
            break;
    }
}

void DeviceHub::connectWiFi() {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
}

void DeviceHub::handleIncomingPacket() {
    int packetSize = udp.parsePacket();
    if (packetSize) {
        char incomingPacket[255];
        int len = udp.read(incomingPacket, 255);
        if (len > 0) {
            incomingPacket[len] = 0;
        }
        Serial.printf("UDP packet received from %s:%d - %s\n", udp.remoteIP().toString().c_str(), udp.remotePort(), incomingPacket);

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, incomingPacket);
        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
        }

        if (doc.containsKey("type")) {
            String messageType = doc["type"].as<String>();

            if (messageType == "REQUEST_DEVICE_INFO") {
                Serial.println("Received REQUEST_DEVICE_INFO");
                sendDeviceInfo();
            }
            else if (messageType == "action_request") {
                if (doc.containsKey("id") && doc.containsKey("action")) {
                    String messageId = doc["id"].as<String>();
                    String actionName = doc["action"].as<String>();
                    Serial.printf("Received action request with ID: %s, Action: %s\n", messageId.c_str(), actionName.c_str());
                    
                    JsonObject payload = doc["payload"].as<JsonObject>();
                    String result = handleAction(actionName, payload);
                }
                else {
                    Serial.println("Received malformed action request");
                }
            }
            else {
                Serial.printf("Received unknown message type: %s\n", messageType.c_str());
            }
        }
        else {
            Serial.println("Received message without type field");
        }
    }
}

void DeviceHub::handleEmergencyPacket() {
    int packetSize = emergencyUdp.parsePacket();
    if (packetSize) {
        char incomingPacket[255];
        int len = emergencyUdp.read(incomingPacket, 255);
        if (len > 0) {
            incomingPacket[len] = 0;
        }
        Serial.printf("Emergency UDP packet received from %s:%d - %s\n", emergencyUdp.remoteIP().toString().c_str(), emergencyUdp.remotePort(), incomingPacket);

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, incomingPacket);
        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
        }

        if (doc["type"] == "emergency_action") {
            handleEmergencyStart();
        } else if (doc["type"] == "emergency_end") {
            handleEmergencyEnd();
        }
    }
}

void DeviceHub::handleEmergencyStart() {
    if (currentState != DeviceState::Emergency) {
        currentState = DeviceState::Emergency;
        Serial.println("Entering emergency state");
        performEmergencyActions();
        sendEmergencyResponse("Emergency state entered");
    }
}

void DeviceHub::handleEmergencyEnd() {
    if (currentState == DeviceState::Emergency) {
        currentState = DeviceState::Normal;
        Serial.println("Exiting emergency state");
        returnToNormalState();
        sendEmergencyResponse("Returned to normal state");
    }
}

void DeviceHub::performEmergencyActions() {
    Serial.println("Attempting emergency protocol...");
    if (emergencyAction) {
        String result = emergencyAction(JsonObject());
        sendMessage("Emergency action performed: " + result);
    }
}

void DeviceHub::returnToNormalState() {
    if (resetAction) {
        String result = resetAction(JsonObject());
        sendMessage("Reset action performed: " + result);
    }
    sendMessage("Returned to normal state");
}

void DeviceHub::sendEmergencyResponse(const char* message) {
    StaticJsonDocument<256> doc;
    doc["type"] = "emergency_action_response";
    doc["deviceName"] = deviceName;
    doc["message"] = message;

    String responseJson;
    serializeJson(doc, responseJson);

    emergencyUdp.beginPacket(emergencyUdp.remoteIP(), emergencyUdp.remotePort());
    emergencyUdp.print(responseJson);
    emergencyUdp.endPacket();

    Serial.printf("Sent emergency response: %s\n", responseJson.c_str());
}

void DeviceHub::sendDeviceInfo() {
    String deviceInfo = getDeviceInfo();
    udp.beginPacket(BROADCAST_IP, HUB_PORT);
    udp.print(deviceInfo);
    udp.endPacket();
    Serial.printf("Broadcast device info to %s:%d - %s\n", BROADCAST_IP.toString().c_str(), HUB_PORT, deviceInfo.c_str());
}

String DeviceHub::getDeviceInfo() {
    StaticJsonDocument<512> doc;
    doc["type"] = "device_info";
    doc["name"] = deviceName;
    doc["ip"] = WiFi.localIP().toString();
    doc["port"] = LOCAL_PORT;
    doc["mac"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();

    // Instead of an array of strings, create an array of action objects.
    JsonArray actionsArray = doc.createNestedArray("actions");
    for (const auto& actionName : actionNames) {
        JsonObject actionObj = actionsArray.createNestedObject();
        actionObj["name"] = actionName;
        // If allowed fields were defined for this action, add them.
        if (allowedFieldsMap.find(actionName) != allowedFieldsMap.end()) {
            JsonArray allowedArray = actionObj.createNestedArray("allowedFields");
            for (const auto& field : allowedFieldsMap[actionName]) {
                allowedArray.add(field);
            }
        }
    }

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

void DeviceHub::sendMessage(const String& message, const String& type) {
    StaticJsonDocument<256> doc;
    doc["type"] = type;
    doc["deviceName"] = deviceName;
    doc["message"] = message;

    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.printf("Sending message of type %s: %s\n", type.c_str(), message.c_str());
    sendUdpMessage(jsonString);
}



void DeviceHub::sendEmergency(const String& message) {
    udp.beginPacket(BROADCAST_IP, EMERGENCY_NOTIFICATION_PORT);
    udp.print(message);
    udp.endPacket();
    
}


void DeviceHub::sendUdpMessage(const String& payload) {
    udp.beginPacket(BROADCAST_IP, HUB_PORT);
    udp.print(payload);
    udp.endPacket();
}

void DeviceHub::resendPendingMessages() {
    unsigned long currentTime = millis();
    for (auto it = pendingAcks.begin(); it != pendingAcks.end(); ) {
        if (currentTime - it->second.timestamp > 5000) {  // 5 seconds timeout
            if (it->second.retries < 3) {  // Maximum 3 retries
                sendMessage(it->second.payload);
                it->second.timestamp = currentTime;
                it->second.retries++;
                ++it;
            } else {
                it = pendingAcks.erase(it);
            }
        } else {
            ++it;
        }
    }
}

String DeviceHub::handleAction(const String& actionName, const JsonObject& payload) {
    Serial.printf("Handling action: %s\n", actionName.c_str());
    auto it = actions.find(actionName);
    if (it != actions.end()) {
        try {
            String result = it->second(payload);
            
            // Send action response
            StaticJsonDocument<256> responseDoc;
            responseDoc["result"] = result;
            String responseJson;
            serializeJson(responseDoc, responseJson);
            sendMessage(responseJson, "action_response");
            
            return result;
        } catch (const std::exception& e) {
            Serial.printf("Error handling action %s: %s\n", actionName.c_str(), e.what());
            String errorMsg = String("Error: ") + e.what();
            sendMessage(errorMsg, "action_response");
            return errorMsg;
        }
    }
    String notFoundMsg = "Action not found";
    sendMessage(notFoundMsg, "action_response");
    return notFoundMsg;
}

void DeviceHub::savePersistentData(const char* key, const String &value) {
    // Open a namespace (here "device-hub") in read/write mode
    prefs.begin("device-hub", false);
    prefs.putString(key, value);
    prefs.end();
}

String DeviceHub::loadPersistentData(const char* key, const String &defaultValue) {
    prefs.begin("device-hub", false);
    String value = prefs.getString(key, defaultValue);
    prefs.end();
    return value;
}

// Original registerAction (no allowed fields)
void DeviceHub::registerAction(const String& actionName, ActionCallback callback) {
    registerAction(actionName, callback, std::vector<String>()); // Call the overload with an empty list.
}

// New registerAction overload with allowed fields.
void DeviceHub::registerAction(const String& actionName, ActionCallback callback, const std::vector<String>& allowedFields) {
    actions[actionName] = callback;
    actionNames.push_back(actionName);
    allowedFieldsMap[actionName] = allowedFields;
}

void DeviceHub::registerEmergencyAction(ActionCallback callback) {
    emergencyAction = callback;
}

void DeviceHub::registerResetAction(ActionCallback callback) {
    resetAction = callback;
}

void DeviceHub::sendPeriodicUpdate() {
    static unsigned long lastUpdate = 0;
    unsigned long currentMillis = millis();
    if (currentMillis - lastUpdate >= 5000) {  // Send update every 5 seconds
        lastUpdate = currentMillis;
        sendDeviceInfo();
    }
}
