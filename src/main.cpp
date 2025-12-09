/**
 * @file main.cpp
 * @author Luis Felipe Patrocinio (https://www.github.com/luisfpatrocinio)
 * @brief Main firmware file. Initializes modules, FS, and starts FreeRTOS tasks.
 * @version 3.1
 * @date 2025-08-26
 */

//==============================================================================
// Arduino & ESP32 Includes
//==============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h> // Adicionado para OTA

//==============================================================================
// Project Header Includes
//==============================================================================
#include "config.h"
#include "rtos_comm.h"
#include "ble_comm.h"
#include "rfid_handler.h"
#include "ui_handler.h"

//==============================================================================
// GLOBAL OBJECTS & CONSTANTS
//==============================================================================
const char *DEVICE_ID = "Pistola-SSRFID";
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
BLECharacteristic *pCharacteristic = nullptr;
BLECharacteristic *versionCharacteristic = nullptr;

//==============================================================================
// GLOBAL STATE VARIABLES
//==============================================================================
volatile bool bluetoothConnected = false;
volatile bool writeMode = false;
String dataToRecord = "";
volatile bool soundEnabled = true;

//==============================================================================
// FreeRTOS HANDLES
//==============================================================================
QueueHandle_t jsonDataQueue;
SemaphoreHandle_t buzzerSemaphore;
SemaphoreHandle_t writeDataMutex;

//==============================================================================
// SETUP FUNCTION
//==============================================================================
void setup()
{
    Serial.begin(115200);
    Serial.println("System Initializing...");

    // --- Filesystem Initialization for OTA ---
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
    } else {
        Serial.println("SPIFFS Mounted Successfully");
    }

    // --- Hardware and Peripheral Initialization ---
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(READ_BUTTON_PIN, INPUT_PULLUP);
    SPI.begin();
    mfrc522.PCD_Init();
    for (byte i = 0; i < 6; i++)
        key.keyByte[i] = 0xFF;
    Serial.println("Peripherals initialized.");

    // --- RTOS Primitives Initialization ---
    jsonDataQueue = xQueueCreate(5, sizeof(char[256]));
    buzzerSemaphore = xSemaphoreCreateBinary();
    writeDataMutex = xSemaphoreCreateMutex();
    if (!jsonDataQueue || !buzzerSemaphore || !writeDataMutex)
    {
        Serial.println("Error creating RTOS primitives! Restarting...");
        ESP.restart();
    }
    Serial.println("RTOS primitives created.");

    // --- Module Initialization ---
    setupBLE(); // Initializes BLE services (App + OTA)

    // --- Task Creation ---
    xTaskCreatePinnedToCore(rfidTask, "RFID_Task", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(rfidWriteTask, "RFID_Write_Task", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(bluetoothTask, "Bluetooth_Task", 4096, NULL, 1, NULL, 1);
    // Task dedicada ao OTA (gerenciamento de escrita na flash)
    xTaskCreatePinnedToCore(otaTask, "OTA_Task", 8192, NULL, 1, NULL, 1); 
    xTaskCreatePinnedToCore(buzzerTask, "Buzzer_Task", 1024, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 2048, NULL, 1, NULL, 1);
    
    Serial.println("FreeRTOS tasks created. System is running.");
}

//==============================================================================
// LOOP (FreeRTOS managed)
//==============================================================================
void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}