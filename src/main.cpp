/*
 * @file main.cpp
 * @author Luis Felipe Patrocinio (https://www.github.com/luisfpatrocinio)
 * @brief Firmware for a handheld RFID reader with read and write functionality using ESP32, MFRC522, and FreeRTOS.
 * @version 2.1
 * @date 2025-08-21
 *
 * @copyright Copyright (c) 2025
 *
 * @mainpage Handheld RFID Reader/Writer Project
 *
 * @section intro_sec Introduction
 * This project implements a fully functional handheld RFID reader/writer with two modes:
 * - **Read Mode:** Reads RFID tags when a button is held down, provides audible feedback, and sends data over Bluetooth.
 * - **Write Mode:** Activated via BLE, allows a string (up to 15 characters) to be written to a tag.
 *
 * @section arch_sec Architecture
 * Built on FreeRTOS to handle multiple tasks concurrently:
 * - **rfidTask:** Reads MFRC522 sensor.
 * - **rfidWriteTask:** Writes data to MFRC522 sensor.
 * - **bluetoothTask:** Manages BLE communication.
 * - **ledTask:** Controls status LED.
 * - **buzzerTask:** Provides audible feedback.
 *
 * @section proto_sec Communication Protocol
 * - **Read JSON Example:** {"deviceId":"ESP32_RFID_Reader","uid":"A1:B2:C3:D4","cardType":"MIFARE 1K","data":"123456789012345"}
 * - **Write Data:** Send a string directly (e.g., "123456789012345").
 *
 * @section hardware_sec Hardware
 * - ESP32 Development Board
 * - MFRC522 RFID Module
 * - 2x Push Buttons (Read/Write)
 * - Passive Buzzer
 * - LED
 */

//==============================================================================
// INCLUDES
//==============================================================================
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include "rfid_tasks.h" // Tasks: rfidTask, rfidWriteTask, bluetoothTask, ledTask, buzzerTask
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

//==============================================================================
// DEFINITIONS: BLE Services and Characteristics
//==============================================================================
#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-5678-1234-abcdefabcdef"

#include "config.h"

//==============================================================================
// GLOBAL OBJECTS & CONSTANTS
//==============================================================================
BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;
volatile bool bluetoothConnected = false;
volatile bool writeMode = false;
String dataToRecord = "";
volatile bool soundEnabled = true;

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key; // Authentication key
const char *DEVICE_ID = "PatroRFID-Reader";

//==============================================================================
// FreeRTOS HANDLES
//==============================================================================
QueueHandle_t jsonDataQueue;
SemaphoreHandle_t buzzerSemaphore;
SemaphoreHandle_t writeDataMutex;
TaskHandle_t rfidTaskHandle;
TaskHandle_t rfidWriteTaskHandle;
TaskHandle_t bluetoothTaskHandle;
TaskHandle_t buzzerTaskHandle;
TaskHandle_t ledTaskHandle;

//==============================================================================
// BLE CALLBACKS
//==============================================================================
class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *server) override
    {
        deviceConnected = true;
        bluetoothConnected = true;
        Serial.println("BLE Client Connected.");
    }

    void onDisconnect(BLEServer *server) override
    {
        deviceConnected = false;
        bluetoothConnected = false;
        Serial.println("BLE Client Disconnected.");
        BLEDevice::startAdvertising(); // Keep advertising for new connections
    }
};

//==============================================================================
// BLE Characteristic Write Callback: Handles incoming BLE data for mode switching and writing
//==============================================================================
class MyCallbacks : public BLECharacteristicCallbacks
{
    // Called when a BLE client writes to the characteristic
    void onWrite(BLECharacteristic *characteristic) override
    {
        std::string rxValue = characteristic->getValue();
        if (!rxValue.empty())
        {
            Serial.print("Received over BLE: ");
            Serial.println(rxValue.c_str());

            // Convert received value to String and trim whitespace
            String received = String(rxValue.c_str());
            received.trim();

            // Attempt to parse received string as JSON
            StaticJsonDocument<128> doc;
            DeserializationError error = deserializeJson(doc, received);

            // Prepare feedback JSON document for response
            StaticJsonDocument<128> feedbackDoc;
            String feedbackJson;

            if (error)
            {
                // If JSON is invalid, notify client and discard
                Serial.println("⚠️ Invalid JSON received. Discarding.");

                feedbackDoc["type"] = "feedback";
                feedbackDoc["content"]["status"] = "error";
                feedbackDoc["content"]["message"] = "Invalid JSON received";
                serializeJson(feedbackDoc, feedbackJson);
                pCharacteristic->setValue(feedbackJson.c_str());
                pCharacteristic->notify();

                return;
            }

            // Extract "type" and "content" fields from JSON
            const char *type = doc["type"];
            const char *content = doc["content"];

            // Protect writeMode and dataToRecord with mutex
            if (xSemaphoreTake(writeDataMutex, portMAX_DELAY) == pdTRUE)
            {
                // Handle mode change requests
                if (type && strcmp(type, "changeMode") == 0)
                {
                    if (content && strcmp(content, "write") == 0)
                    {
                        // Activate write mode
                        writeMode = true;
                        dataToRecord = "";
                        Serial.println("🔴 Write mode ACTIVATED via BLE. Waiting for data...");

                        feedbackDoc["type"] = "feedback";
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["mode"] = "write";
                        feedbackDoc["content"]["message"] = "Write mode activated";
                    }
                    else if (content && strcmp(content, "stop") == 0)
                    {
                        // Deactivate write mode
                        writeMode = false;
                        dataToRecord = "";
                        Serial.println("🔵 Write mode STOPPED via BLE.");

                        feedbackDoc["type"] = "feedback";
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["mode"] = "read";
                        feedbackDoc["content"]["message"] = "Write mode stopped";
                    }
                }
                // Handle data to be written to RFID tag
                else if (type && strcmp(type, "writeData") == 0 && writeMode)
                {
                    if (content)
                    {
                        // Store data for writing
                        dataToRecord = String(content);
                        Serial.print("📥 Data for writing received: ");
                        Serial.println(dataToRecord);

                        feedbackDoc["type"] = "feedback";
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["message"] = "Data for writing received";
                        feedbackDoc["content"]["data"] = dataToRecord;
                    }
                }
                else if (type && strcmp(type, "toggleSound") == 0)
                {
                    if (content && strcmp(content, "on") == 0)
                    {
                        soundEnabled = true;
                        Serial.println("🔊 Sound ENABLED via BLE.");
                        feedbackDoc["type"] = "feedback";
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["message"] = "Sound enabled";
                    }
                    else if (content && strcmp(content, "off") == 0)
                    {
                        soundEnabled = false;
                        Serial.println("🔇 Sound DISABLED via BLE.");
                        feedbackDoc["type"] = "feedback";
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["message"] = "Sound disabled";
                    }
                }
                else
                {
                    // Unknown type or not in write mode
                    Serial.println("⚠️ JSON received but not in write mode or unknown type. Discarding.");

                    feedbackDoc["type"] = "feedback";
                    feedbackDoc["content"]["status"] = "error";
                    feedbackDoc["content"]["message"] = "Unknown type or not in write mode";
                }
                xSemaphoreGive(writeDataMutex);

                // Send feedback to BLE client
                serializeJson(feedbackDoc, feedbackJson);
                pCharacteristic->setValue(feedbackJson.c_str());
                pCharacteristic->notify();

                // Play sound
                xSemaphoreGive(buzzerSemaphore);
            }
        }
    }
};

//==============================================================================
// SETUP FUNCTION
//==============================================================================
void setup()
{
    Serial.begin(115200);
    while (!Serial)
        ;

    Serial.println("System Initializing...");

    // Configure hardware pins
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    pinMode(READ_BUTTON_PIN, INPUT_PULLUP);

    // Create FreeRTOS primitives
    jsonDataQueue = xQueueCreate(5, sizeof(char[256]));
    buzzerSemaphore = xSemaphoreCreateBinary();
    writeDataMutex = xSemaphoreCreateMutex(); // <-- Inicialização do mutex

    // Check if FreeRTOS primitives were created successfully
    if (!jsonDataQueue || !buzzerSemaphore || !writeDataMutex)
    {
        Serial.println("Error creating RTOS primitives! Restarting...");
        ESP.restart(); // Restart ESP32 if any primitive creation failed
    }

    // Initialize SPI + MFRC522
    SPI.begin();
    mfrc522.PCD_Init();

    // Prepare default key (factory default 0xFF)
    for (byte i = 0; i < 6; i++)
        key.keyByte[i] = 0xFF;

    // Initialize BLE stack with device name
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

    // Add BLE2902 descriptor for enabling notifications/indications on client side
    pCharacteristic->addDescriptor(new BLE2902());

    // Set custom callbacks for BLE characteristic write events
    pCharacteristic->setCallbacks(new MyCallbacks());

    // Start BLE service so it becomes available for clients
    pService->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE service started, waiting for client...");
    Serial.println("Peripherals initialized.");
    Serial.println("Hold the read button to scan tags. Use BLE (*WriteMode / *StopWrite) for writing.");

    // Create and pin tasks to Core 1
    xTaskCreatePinnedToCore(rfidTask, "RFID_Task", 4096, NULL, 2, &rfidTaskHandle, 1);
    xTaskCreatePinnedToCore(rfidWriteTask, "RFID_Write_Task", 4096, NULL, 2, &rfidWriteTaskHandle, 1);
    xTaskCreatePinnedToCore(bluetoothTask, "Bluetooth_Task", 4096, NULL, 1, &bluetoothTaskHandle, 1);
    xTaskCreatePinnedToCore(buzzerTask, "Buzzer_Task", 1024, NULL, 1, &buzzerTaskHandle, 1);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 2048, NULL, 1, &ledTaskHandle, 1);

    Serial.println("FreeRTOS tasks created. System is running.");
}

//==============================================================================
// LOOP (FreeRTOS managed)
//==============================================================================
void loop()
{
    // FreeRTOS scheduler handles all tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}