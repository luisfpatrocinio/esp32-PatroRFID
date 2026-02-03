/**
 * @file ble_comm.cpp
 * @author Luis Felipe Patrocinio
 * @brief Implementation of BLE communication setup, callbacks, and task.
 * @date 2025-09-01
 */

//==============================================================================
// Arduino & ESP32 Includes
//==============================================================================
#include <ArduinoJson.h>

//==============================================================================
// Project Header Includes
//==============================================================================
#include "config.h"

//==============================================================================
// BLE Library Includes
//==============================================================================
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

//==============================================================================
// Project Header Includes
//==============================================================================
#include "ble_comm.h"
#include "rtos_comm.h"

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
        // Set the global flag to indicate BLE client is connected
        bluetoothConnected = true;
        Serial.println("BLE Client Connected.");
    }

    void onDisconnect(BLEServer *server) override
    {
        // Clear the global flag when BLE client disconnects
        bluetoothConnected = false;
        Serial.println("BLE Client Disconnected.");
        // Restart advertising so new clients can connect
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

        // Parse the received JSON string
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, received);

        JsonDocument feedbackDoc;
        String feedbackJson;

        if (error)
        {
            // If JSON is invalid, prepare error feedback
            feedbackDoc["type"] = "feedback";
            feedbackDoc["content"]["status"] = "error";
            feedbackDoc["content"]["message"] = "Invalid JSON received";
        }
        else
        {
            const char *type = doc["type"];
            const char *content = doc["content"];

            // Protect shared state with mutex before handling commands
            if (xSemaphoreTake(writeDataMutex, portMAX_DELAY) == pdTRUE)
            {
                // Handle mode change command
                if (type && strcmp(type, "changeMode") == 0)
                {
                    if (content && strcmp(content, "write") == 0)
                    {
                        // Enable write mode and clear previous data
                        writeMode = true;
                        dataToRecord = "";
                        feedbackDoc["content"]["mode"] = "write";
                        feedbackDoc["content"]["message"] = "Write mode activated";
                    }
                    else if (content && strcmp(content, "stop") == 0)
                    {
                        // Disable write mode and clear previous data
                        writeMode = false;
                        dataToRecord = "";
                        feedbackDoc["content"]["mode"] = "read";
                        feedbackDoc["content"]["message"] = "Write mode stopped";
                    }
                }
                // Handle data to be written to RFID tag
                else if (type && strcmp(type, "writeData") == 0 && writeMode)
                {
                    dataToRecord = String(content);
                    feedbackDoc["content"]["message"] = "Data for writing received";
                    feedbackDoc["content"]["data"] = dataToRecord;
                }
                // Handle sound toggle command
                else if (type && strcmp(type, "toggleSound") == 0)
                {
                    // Update global soundEnabled flag based on received content
                    soundEnabled = (content && strcmp(content, "on") == 0);
                    feedbackDoc["content"]["message"] = soundEnabled ? "Sound enabled" : "Sound disabled";
                }
                else
                {
                    // Unknown command
                    feedbackDoc["content"]["status"] = "error";
                    feedbackDoc["content"]["message"] = "Unknown type";
                }

                // If no error, set status to ok
                if (!feedbackDoc["content"]["status"])
                    feedbackDoc["content"]["status"] = "ok";
                feedbackDoc["type"] = "feedback";
                // Release mutex after handling command
                xSemaphoreGive(writeDataMutex);
            }
        }

        // Send feedback JSON to BLE client
        serializeJson(feedbackDoc, feedbackJson);
        characteristic->setValue(feedbackJson.c_str());
        characteristic->notify();
        // Signal buzzer task to provide feedback
        xSemaphoreGive(buzzerSemaphore);
    }
};

//==============================================================================
// BLE SETUP FUNCTION
//==============================================================================
void setupBLE()
{
    // Initialize BLE device with custom name
    BLEDevice::init(DEVICE_ID);
    // Create BLE server and set connection callbacks
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    // Create BLE service with custom UUID
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Create BLE characteristic with read, write, notify, and indicate properties
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_INDICATE);

    // Add descriptor for BLE notifications
    pCharacteristic->addDescriptor(new BLE2902());
    // Set custom callbacks for BLE characteristic
    pCharacteristic->setCallbacks(new MyCallbacks());
    // Start BLE service
    pService->start();

    // Configure BLE advertising parameters
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    // Start advertising so clients can discover the device
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
        // Wait for JSON data from the queue (produced by other tasks)
        if (xQueueReceive(jsonDataQueue, &receivedJson, portMAX_DELAY) == pdPASS)
        {
            // Only send data if BLE client is connected and characteristic is valid
            if (bluetoothConnected && pCharacteristic != nullptr)
            {
                Serial.print("Sending via BLE: ");
                Serial.println(receivedJson);
                // Set characteristic value and notify client
                pCharacteristic->setValue((uint8_t *)receivedJson, strlen(receivedJson));
                pCharacteristic->notify();
            }
            // If not connected, discard the message (no notification sent)
        }
    }
}