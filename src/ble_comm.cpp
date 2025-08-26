/**
 * @file ble_comm.cpp
 * @author Luis Felipe Patrocinio
 * @brief Implementation of BLE communication setup, callbacks, and task.
 * @date 2025-08-26
 */

#include "ble_comm.h"
#include "rtos_comm.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

//==============================================================================
// DEFINITIONS
//==============================================================================
#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-5678-1234-abcdefabcdef"
extern const char *DEVICE_ID; // Use the device ID from main.cpp

// BLE server pointer
static BLEServer *pServer = nullptr;

//==============================================================================
// BLE SERVER CALLBACKS
//==============================================================================
class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *server) override
    {
        bluetoothConnected = true;
        Serial.println("BLE Client Connected.");
    }

    void onDisconnect(BLEServer *server) override
    {
        bluetoothConnected = false;
        Serial.println("BLE Client Disconnected.");
        BLEDevice::startAdvertising(); // Keep advertising
    }
};

//==============================================================================
// BLE CHARACTERISTIC CALLBACKS
//==============================================================================
class MyCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *characteristic) override
    {
        std::string rxValue = characteristic->getValue();
        if (rxValue.empty())
            return;

        Serial.print("Received over BLE: ");
        Serial.println(rxValue.c_str());

        String received = String(rxValue.c_str());
        received.trim();

        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, received);

        StaticJsonDocument<128> feedbackDoc;
        String feedbackJson;

        if (error)
        {
            feedbackDoc["type"] = "feedback";
            feedbackDoc["content"]["status"] = "error";
            feedbackDoc["content"]["message"] = "Invalid JSON received";
        }
        else
        {
            const char *type = doc["type"];
            const char *content = doc["content"];

            if (xSemaphoreTake(writeDataMutex, portMAX_DELAY) == pdTRUE)
            {
                if (type && strcmp(type, "changeMode") == 0)
                {
                    if (content && strcmp(content, "write") == 0)
                    {
                        writeMode = true;
                        dataToRecord = "";
                        feedbackDoc["content"]["mode"] = "write";
                        feedbackDoc["content"]["message"] = "Write mode activated";
                    }
                    else if (content && strcmp(content, "stop") == 0)
                    {
                        writeMode = false;
                        dataToRecord = "";
                        feedbackDoc["content"]["mode"] = "read";
                        feedbackDoc["content"]["message"] = "Write mode stopped";
                    }
                }
                else if (type && strcmp(type, "writeData") == 0 && writeMode)
                {
                    dataToRecord = String(content);
                    feedbackDoc["content"]["message"] = "Data for writing received";
                    feedbackDoc["content"]["data"] = dataToRecord;
                }
                else if (type && strcmp(type, "toggleSound") == 0)
                {
                    soundEnabled = (content && strcmp(content, "on") == 0);
                    feedbackDoc["content"]["message"] = soundEnabled ? "Sound enabled" : "Sound disabled";
                }
                else
                {
                    feedbackDoc["content"]["status"] = "error";
                    feedbackDoc["content"]["message"] = "Unknown type or not in write mode";
                }

                if (!feedbackDoc["content"]["status"])
                    feedbackDoc["content"]["status"] = "ok";
                feedbackDoc["type"] = "feedback";
                xSemaphoreGive(writeDataMutex);
            }
        }

        serializeJson(feedbackDoc, feedbackJson);
        characteristic->setValue(feedbackJson.c_str());
        characteristic->notify();
        xSemaphoreGive(buzzerSemaphore);
    }
};

//==============================================================================
// BLE SETUP FUNCTION
//==============================================================================
void setupBLE()
{
    BLEDevice::init(DEVICE_ID);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_INDICATE);

    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("BLE service started, waiting for client...");
}

//==============================================================================
// BLUETOOTH SENDER TASK
//==============================================================================
void bluetoothTask(void *parameter)
{
    char receivedJson[256];
    for (;;)
    {
        if (xQueueReceive(jsonDataQueue, &receivedJson, portMAX_DELAY) == pdPASS)
        {
            if (bluetoothConnected && pCharacteristic != nullptr)
            {
                Serial.print("📤 Sending via BLE: ");
                Serial.println(receivedJson);
                pCharacteristic->setValue((uint8_t *)receivedJson, strlen(receivedJson));
                pCharacteristic->notify();
            }
            else
            {
                Serial.println("⚠️ BLE device not connected. JSON discarded.");
            }
        }
    }
}