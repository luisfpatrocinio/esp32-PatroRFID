/**
 * @file ble_comm.cpp
 * @author Luis Felipe Patrocinio
 * @brief Implementation of BLE communication setup, callbacks, task and OTA.
 * @date 2025-09-01
 */

//==============================================================================
// Arduino & ESP32 Includes
//==============================================================================
#include <ArduinoJson.h>
#include <Update.h>
#include "FS.h"
#include "SPIFFS.h"

//==============================================================================
// Project Header Includes
//==============================================================================
#include "config.h"
#include "ble_comm.h"
#include "rtos_comm.h"

//==============================================================================
// BLE Library Includes
//==============================================================================
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

//==============================================================================
// APP DEFINITIONS
//==============================================================================
#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-5678-1234-abcdefabcdef"
#define VERSION_CHARACTERISTIC_UUID "E04A98F1-3B9A-4692-9122-57031CA11EE0"

//==============================================================================
// OTA DEFINITIONS & VARS
//==============================================================================
#define OTA_SERVICE_UUID           "fb1e4001-54ae-4a28-9f74-dfccb248601d"
#define OTA_CHARACTERISTIC_UUID_RX "fb1e4002-54ae-4a28-9f74-dfccb248601d"
#define OTA_CHARACTERISTIC_UUID_TX "fb1e4003-54ae-4a28-9f74-dfccb248601d"

#define NORMAL_MODE   0
#define UPDATE_MODE   1
#define OTA_MODE      2

// OTA Globals
static BLECharacteristic *pOtaCharacteristicTX;
static BLECharacteristic *pOtaCharacteristicRX;
static bool sendMode = false, sendSize = true;
static bool writeFile = false, request = false;
static int writeLen = 0, writeLen2 = 0;
static bool current = true;
static int parts = 0, next = 0, cur = 0, MTU = 0;
static int OTA_STATE = NORMAL_MODE;
unsigned long rParts, tParts;
uint8_t updater[16384];
uint8_t updater2[16384];

#define FLASH SPIFFS
#define FASTMODE false 

extern const char *DEVICE_ID;
static BLEServer *pServer = nullptr;

//==============================================================================
// HELPERS FOR OTA
//==============================================================================
static void rebootEspWithReason(String reason) {
    Serial.println(reason);
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
}

static void writeBinary(fs::FS &fs, const char *path, uint8_t *dat, int len) {
    File file = fs.open(path, FILE_APPEND);
    if (!file) {
        Serial.println("- failed to open file for writing");
        return;
    }
    file.write(dat, len);
    file.close();
    writeFile = false;
    rParts += len;
}

void sendOtaResult(String result) {
    pOtaCharacteristicTX->setValue(result.c_str());
    pOtaCharacteristicTX->notify();
    vTaskDelay(pdMS_TO_TICKS(200));
}

void performUpdate(Stream &updateSource, size_t updateSize) {
    String result = "0F"; // Hex string simulation
    if (Update.begin(updateSize)) {
        size_t written = Update.writeStream(updateSource);
        if (written == updateSize) {
            Serial.println("Written : " + String(written) + " successfully");
        } else {
            Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
        }
        if (Update.end()) {
            Serial.println("OTA done!");
            if (Update.isFinished()) {
                Serial.println("Update successfully completed. Rebooting...");
                result = "Success!";
            } else {
                Serial.println("Update not finished? Something went wrong!");
                result = "Failed!";
            }
        } else {
            Serial.println("Error Occurred. Error #: " + String(Update.getError()));
            result = "Error #: " + String(Update.getError());
        }
    } else {
        Serial.println("Not enough space to begin OTA");
        result = "Not enough space for OTA";
    }
    if (bluetoothConnected) {
        sendOtaResult(result);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void updateFromFS(fs::FS &fs) {
    File updateBin = fs.open("/update.bin");
    if (updateBin) {
        if (updateBin.isDirectory()) {
            Serial.println("Error, update.bin is not a file");
            updateBin.close();
            return;
        }
        size_t updateSize = updateBin.size();
        if (updateSize > 0) {
            Serial.println("Trying to start update");
            performUpdate(updateBin, updateSize);
        } else {
            Serial.println("Error, file is empty");
        }
        updateBin.close();
        Serial.println("Removing update file");
        fs.remove("/update.bin");
        rebootEspWithReason("Rebooting to complete OTA update");
    } else {
        Serial.println("Could not load update.bin from spiffs root");
    }
}

//==============================================================================
// BLE SERVER CALLBACKS (APP + OTA)
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
        BLEDevice::startAdvertising(); 
    }
};

//==============================================================================
// BLE CHARACTERISTIC CALLBACKS (APP - JSON)
//==============================================================================
class AppCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *characteristic) override
    {
        std::string rxValue = characteristic->getValue();
        if (rxValue.empty()) return;

        Serial.print("Received over BLE: ");
        Serial.println(rxValue.c_str());

        String received = String(rxValue.c_str());
        received.trim();

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, received);

        JsonDocument feedbackDoc;
        String feedbackJson;

        if (error) {
            feedbackDoc["type"] = "feedback";
            feedbackDoc["content"]["status"] = "error";
            feedbackDoc["content"]["message"] = "Invalid JSON received";
        } else {
            const char *type = doc["type"];
            const char *content = doc["content"];

            if (xSemaphoreTake(writeDataMutex, portMAX_DELAY) == pdTRUE) {
                if (type && strcmp(type, "changeMode") == 0) {
                    if (content && strcmp(content, "write") == 0) {
                        writeMode = true;
                        dataToRecord = "";
                        feedbackDoc["content"]["mode"] = "write";
                        feedbackDoc["content"]["message"] = "Write mode activated";
                    } else if (content && strcmp(content, "stop") == 0) {
                        writeMode = false;
                        dataToRecord = "";
                        feedbackDoc["content"]["mode"] = "read";
                        feedbackDoc["content"]["message"] = "Write mode stopped";
                    }
                } else if (type && strcmp(type, "writeData") == 0 && writeMode) {
                    dataToRecord = String(content);
                    feedbackDoc["content"]["message"] = "Data for writing received";
                    feedbackDoc["content"]["data"] = dataToRecord;
                } else if (type && strcmp(type, "toggleSound") == 0) {
                    soundEnabled = (content && strcmp(content, "on") == 0);
                    feedbackDoc["content"]["message"] = soundEnabled ? "Sound enabled" : "Sound disabled";
                } else {
                    feedbackDoc["content"]["status"] = "error";
                    feedbackDoc["content"]["message"] = "Unknown type";
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
// BLE CHARACTERISTIC CALLBACKS (OTA)
//==============================================================================
class OtaCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic) override
    {
        uint8_t *pData;
        std::string value = pCharacteristic->getValue();
        int len = value.length();
        pData = pCharacteristic->getData();

        if (pData != NULL)
        {
            if (pData[0] == 0xFB)
            {
                int pos = pData[1];
                for (int x = 0; x < len - 2; x++)
                {
                    if (current)
                    {
                        updater[(pos * MTU) + x] = pData[x + 2];
                    }
                    else
                    {
                        updater2[(pos * MTU) + x] = pData[x + 2];
                    }
                }
            }
            else if (pData[0] == 0xFC)
            {
                if (current)
                {
                    writeLen = (pData[1] * 256) + pData[2];
                }
                else
                {
                    writeLen2 = (pData[1] * 256) + pData[2];
                }
                current = !current;
                cur = (pData[3] * 256) + pData[4];
                writeFile = true;
                if (cur < parts - 1)
                {
                    request = !FASTMODE;
                }
            }
            else if (pData[0] == 0xFD)
            {
                sendMode = true;
                if (FLASH.exists("/update.bin"))
                {
                    FLASH.remove("/update.bin");
                }
            }
            else if (pData[0] == 0xFE)
            {
                rParts = 0;
                tParts = (pData[1] * 256 * 256 * 256) + (pData[2] * 256 * 256) + (pData[3] * 256) + pData[4];
                Serial.print("Available space: ");
                Serial.println(FLASH.totalBytes() - FLASH.usedBytes());
                Serial.print("File Size: ");
                Serial.println(tParts);
            }
            else if (pData[0] == 0xFF)
            {
                parts = (pData[1] * 256) + pData[2];
                MTU = (pData[3] * 256) + pData[4];
                OTA_STATE = UPDATE_MODE;
            }
            else if (pData[0] == 0xEF)
            {
                FLASH.format();
                sendSize = true;
            }
        }
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

    // --- APP SERVICE ---
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE);
    
    versionCharacteristic = pService->createCharacteristic(
        VERSION_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
    versionCharacteristic->setValue(FIRMWARE_VERSION.c_str());

    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setCallbacks(new AppCallbacks());
    pService->start();

    // --- OTA SERVICE ---
    BLEService *pOtaService = pServer->createService(OTA_SERVICE_UUID);
    pOtaCharacteristicTX = pOtaService->createCharacteristic(
        OTA_CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pOtaCharacteristicRX = pOtaService->createCharacteristic(
        OTA_CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    
    pOtaCharacteristicRX->setCallbacks(new OtaCallbacks());
    pOtaCharacteristicTX->addDescriptor(new BLE2902());
    pOtaCharacteristicTX->setNotifyProperty(true);
    pOtaService->start();

    // --- ADVERTISING ---
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->addServiceUUID(OTA_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("BLE services started (App + OTA).");
}

//==============================================================================
// TASKS
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
        }
    }
}

void otaTask(void *parameter)
{
    for (;;)
    {
        switch (OTA_STATE)
        {
        case NORMAL_MODE:
            if (bluetoothConnected)
            {
                if (sendMode)
                {
                    uint8_t fMode[] = {0xAA, FASTMODE};
                    pOtaCharacteristicTX->setValue(fMode, 2);
                    pOtaCharacteristicTX->notify();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    sendMode = false;
                }
                if (sendSize)
                {
                    unsigned long x = FLASH.totalBytes();
                    unsigned long y = FLASH.usedBytes();
                    uint8_t fSize[] = {0xEF, (uint8_t)(x >> 16), (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(y >> 16), (uint8_t)(y >> 8), (uint8_t)y};
                    pOtaCharacteristicTX->setValue(fSize, 7);
                    pOtaCharacteristicTX->notify();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    sendSize = false;
                }
            }
            break;

        case UPDATE_MODE:
            if (request)
            {
                uint8_t rq[] = {0xF1, (uint8_t)((cur + 1) / 256), (uint8_t)((cur + 1) % 256)};
                pOtaCharacteristicTX->setValue(rq, 3);
                pOtaCharacteristicTX->notify();
                vTaskDelay(pdMS_TO_TICKS(50));
                request = false;
            }

            if (cur + 1 == parts)
            { // Received complete file
                uint8_t com[] = {0xF2, (uint8_t)((cur + 1) / 256), (uint8_t)((cur + 1) % 256)};
                pOtaCharacteristicTX->setValue(com, 3);
                pOtaCharacteristicTX->notify();
                vTaskDelay(pdMS_TO_TICKS(50));
                OTA_STATE = OTA_MODE;
            }

            if (writeFile)
            {
                if (!current)
                {
                    writeBinary(FLASH, "/update.bin", updater, writeLen);
                }
                else
                {
                    writeBinary(FLASH, "/update.bin", updater2, writeLen2);
                }
            }
            break;

        case OTA_MODE:
            if (writeFile)
            {
                if (!current)
                {
                    writeBinary(FLASH, "/update.bin", updater, writeLen);
                }
                else
                {
                    writeBinary(FLASH, "/update.bin", updater2, writeLen2);
                }
            }

            if (rParts == tParts)
            {
                Serial.println("OTA Download Complete");
                vTaskDelay(pdMS_TO_TICKS(5000));
                updateFromFS(FLASH);
            }
            else
            {
                writeFile = true;
                Serial.println("Incomplete");
                Serial.print("Expected: "); Serial.print(tParts);
                Serial.print(" Received: "); Serial.println(rParts);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            break;
        }
        
        // Delay para evitar Watchdog trigger se nada estiver acontecendo
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}