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
#include "rfid_tasks.h"     // Tasks: rfidTask, rfidWriteTask, bluetoothTask, ledTask, buzzerTask
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

//==============================================================================
// DEFINITIONS: BLE Services and Characteristics
//==============================================================================
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-5678-1234-abcdefabcdef"

//==============================================================================
// PIN DEFINITIONS
//==============================================================================
#define SS_PIN              5   // MFRC522 SDA/SS
#define RST_PIN             4   // MFRC522 RST
#define BUZZER_PIN          22  // Buzzer
#define READ_BUTTON_PIN     21  // Button for reading
#define LED_PIN             2   // Status LED

//==============================================================================
// GLOBAL OBJECTS & CONSTANTS
//==============================================================================
BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;
volatile bool bluetoothConnected = false;
volatile bool writeMode = false;
String dataToRecord = "";

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;                  // Authentication key
const char *DEVICE_ID = "PatroRFID-Reader";

//==============================================================================
// FreeRTOS HANDLES
//==============================================================================
QueueHandle_t jsonDataQueue;
SemaphoreHandle_t buzzerSemaphore;
SemaphoreHandle_t writeDataMutex; // <-- Declaração da variável mutex
TaskHandle_t rfidTaskHandle;
TaskHandle_t rfidWriteTaskHandle;
TaskHandle_t bluetoothTaskHandle;
TaskHandle_t buzzerTaskHandle;
TaskHandle_t ledTaskHandle;

//==============================================================================
// BLE CALLBACKS
//==============================================================================
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override {
        deviceConnected = true;
        bluetoothConnected = true;
        Serial.println("BLE Client Connected.");
    }

    void onDisconnect(BLEServer *server) override {
        deviceConnected = false;
        bluetoothConnected = false;
        Serial.println("BLE Client Disconnected.");
        BLEDevice::startAdvertising(); // Keep advertising for new connections
    }
};


class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override {
        std::string rxValue = characteristic->getValue();
        if (!rxValue.empty()) {
            Serial.print("Received over BLE: ");
            Serial.println(rxValue.c_str());

            // Garanta que a string não tem espaços ou caracteres de nova linha
            String received = String(rxValue.c_str());
            received.trim();

            if (xSemaphoreTake(writeDataMutex, portMAX_DELAY) == pdTRUE) {
                if (received == "*WriteMode") {
                    writeMode = true;
                    dataToRecord = ""; 
                    Serial.println("🔴 Write mode ACTIVATED via BLE. Waiting for data...");
                } else if (received == "*StopWrite") {
                    writeMode = false;
                    dataToRecord = "";
                    Serial.println("🔵 Write mode STOPPED via BLE.");
                } else if (writeMode) { // Só entra aqui se writeMode já for TRUE
                    dataToRecord = received;
                    Serial.print("📥 Data for writing received: ");
                    Serial.println(dataToRecord);
                } else { // Nenhum dos casos acima se aplica
                    Serial.println("⚠️ Data received but not in write mode. Discarding.");
                }
                xSemaphoreGive(writeDataMutex);
            }
        }
    }
};

//==============================================================================
// SETUP FUNCTION
//==============================================================================
void setup() {
    Serial.begin(115200);
    while (!Serial);

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

    if (!jsonDataQueue || !buzzerSemaphore || !writeDataMutex) { // <-- Verificação de falha
        Serial.println("Error creating RTOS primitives! Restarting...");
        ESP.restart();
    }

    // Initialize SPI + MFRC522
    SPI.begin();
    mfrc522.PCD_Init();

    // Prepare default key (factory default 0xFF)
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    // Initialize BLE
    BLEDevice::init(DEVICE_ID);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_INDICATE
    );

    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setCallbacks(new MyCallbacks());

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
void loop() {
    // FreeRTOS scheduler handles all tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}