/**
 * @file main.cpp
 * @author Luis Felipe Patrocinio (https://www.github.com/luisfpatrocinio)
 * @brief Firmware for a handheld RFID reader with read and write functionality using ESP32, MFRC522, and FreeRTOS.
 * @version 2.0
 * @date 2025-08-12
 *
 * @copyright Copyright (c) 2025
 *
 * @mainpage Handheld RFID Reader/Writer Project
 *
 * @section intro_sec Introduction
 * This project implements a fully functional handheld RFID reader/writer. It has two modes of operation:
 * - **Read Mode:** Reads RFID tags when a button is held down, provides audible feedback, and sends data over Bluetooth.
 * - **Write Mode:** Activates when a second button is held, allowing a simple string (up to 15 characters) to be sent via Bluetooth and written to a tag.
 *
 * @section arch_sec Architecture
 * The firmware is built upon the FreeRTOS real-time operating system to handle
 * multiple tasks concurrently and in a non-blocking fashion.
 * - **rfidTask:** Manages reading the MFRC522 sensor, including authentication and reading of custom data.
 * - **rfidWriteTask:** Manages writing data to the MFRC522 sensor, including authentication.
 * - **bluetoothTask:** Handles sending and receiving data over Bluetooth Serial.
 * - **ledTask:** Controls a status LED to indicate Bluetooth connection and activity.
 * - **buzzerTask:** Provides audible feedback upon successful tag operations.
 *
 * @section proto_sec Communication Protocol
 * Data is transmitted in a simplified string format for writing (e.g., "123456789012345") and a JSON format for reading.
 * - **Read JSON Example:** `{"deviceId":"ESP32_RFID_Reader","uid":"A1:B2:C3:D4","cardType":"MIFARE 1K","data":"123456789012345"}`
 * - **Write Data:** Send a simple string (e.g., "123456789012345") directly.
 *
 * @section hardware_sec Hardware
 * - ESP32 Development Board
 * - MFRC522 RFID Module
 * - 2x Push Buttons (one for read, one for write)
 * - Passive Buzzer
 * - LED
 */


//==============================================================================
// INCLUDES
//==============================================================================
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include "BluetoothSerial.h"
#include "rfid_tasks.h" // Inclui o novo arquivo de cabeçalho

//==============================================================================
// PIN DEFINITIONS
//==============================================================================
#define SS_PIN 5      ///< ESP32 pin GPIO5 -> MFRC522 SDA/SS (Slave Select)
#define RST_PIN 4    ///< ESP32 pin GPIO19 -> MFRC522 RST (Reset)
#define BUZZER_PIN 22 ///< ESP32 pin GPIO22 -> Buzzer positive pin
#define READ_BUTTON_PIN 21  ///< ESP32 pin GPIO21 -> Push button for reading
#define LED_PIN 2     ///< ESP32 pin GPIO2 -> Status LED

//==============================================================================
// GLOBAL OBJECTS & CONSTANTS
//==============================================================================
MFRC522 mfrc522(SS_PIN, RST_PIN);          ///< Instance of the MFRC522 library.
BluetoothSerial BTSerial;                   ///< Instance of the BluetoothSerial library.
const char *DEVICE_ID = "PatroRFID-Reader"; ///< Unique identifier for this device.
volatile bool bluetoothConnected = false;   ///< Global flag to track Bluetooth connection status.
volatile bool writeMode = false;         ///< Global flag to enable write mode.
String dataToRecord = "";                 ///< String to hold data for writing to a tag.
MFRC522::MIFARE_Key key;                     ///< Key for authentication.

//==============================================================================
// FreeRTOS HANDLES
//==============================================================================
QueueHandle_t jsonDataQueue;        ///< Queue to pass JSON strings from rfidTask to bluetoothTask.
SemaphoreHandle_t buzzerSemaphore;  ///< Binary semaphore to trigger the buzzer task.
TaskHandle_t rfidTaskHandle;        ///< Handle for the RFID reader task.
TaskHandle_t rfidWriteTaskHandle;   ///< Handle for the RFID writer task.
TaskHandle_t bluetoothTaskHandle;   ///< Handle for the Bluetooth sender task.
TaskHandle_t buzzerTaskHandle;      ///< Handle for the buzzer control task.
TaskHandle_t ledTaskHandle;         ///< Handle for the status LED control task.

//==============================================================================
// FUNCTION PROTOTYPES
//==============================================================================
void bt_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

//==============================================================================
// SETUP FUNCTION
//==============================================================================
/**
 * @brief Initializes all hardware, peripherals, FreeRTOS objects, and tasks.
 * This function runs once on startup.
 */
void setup()
{
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

    if (jsonDataQueue == NULL || buzzerSemaphore == NULL)
    {
        Serial.println("Error creating RTOS primitives! Restarting...");
        ESP.restart();
    }

    // Initialize services
    BTSerial.register_callback(bt_callback);
    BTSerial.begin(DEVICE_ID);
    SPI.begin();
    mfrc522.PCD_Init();

    // Prepare default key for authentication
    for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF;
    }

    Serial.println("Peripherals initialized.");
    Serial.println("Hold the read button to scan tags. Use Bluetooth (*WriteMode / *StopWrite) for writing.");

    // Create and pin tasks to a specific core (Core 1)
    xTaskCreatePinnedToCore(rfidTask, "RFID_Task", 4096, NULL, 2, &rfidTaskHandle, 1);
    xTaskCreatePinnedToCore(rfidWriteTask, "RFID_Write_Task", 4096, NULL, 2, &rfidWriteTaskHandle, 1);
    xTaskCreatePinnedToCore(bluetoothTask, "Bluetooth_Task", 4096, NULL, 1, &bluetoothTaskHandle, 1);
    xTaskCreatePinnedToCore(buzzerTask, "Buzzer_Task", 1024, NULL, 1, &buzzerTaskHandle, 1);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 2048, NULL, 1, &ledTaskHandle, 1);

    Serial.println("FreeRTOS tasks created. System is running.");
}

//==============================================================================
// BLUETOOTH EVENT CALLBACK
//==============================================================================
/**
 * @brief Callback function for Bluetooth events.
 * @param event The type of event that occurred.
 * @param param Parameters associated with the event.
 * @note This function updates the global `bluetoothConnected` flag.
 */
void bt_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (event == ESP_SPP_SRV_OPEN_EVT)
    {
        Serial.println("Bluetooth Client Connected.");
        bluetoothConnected = true;
    }
    else if (event == ESP_SPP_CLOSE_EVT)
    {
        Serial.println("Bluetooth Client Disconnected.");
        bluetoothConnected = false;
    }
}

void loop()
{
    // This loop is intentionally left empty.
    // The FreeRTOS scheduler is managing all tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}