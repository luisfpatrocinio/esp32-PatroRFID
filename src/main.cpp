/**
 * @file main.cpp
 * @author Luis Felipe Patrocinio (https://www.github.com/luisfpatrocinio)
 * @brief Firmware for a handheld RFID reader using ESP32, MFRC522, and FreeRTOS.
 * @version 0.9
 * @date 2025-08-07
 *
 * @copyright Copyright (c) 2025
 *
 * @mainpage Handheld RFID Reader Project
 *
 * @section intro_sec Introduction
 * This project implements a fully functional handheld RFID reader. It reads RFID tags
 * only when a button is held down, provides audible feedback via a buzzer, visual
 * status via an LED, and sends the collected data over Bluetooth using a structured
 * JSON protocol.
 *
 * @section arch_sec Architecture
 * The firmware is built upon the FreeRTOS real-time operating system to handle
 * multiple tasks concurrently and in a non-blocking fashion.
 * - **rfidTask:** Manages reading the MFRC522 sensor when the button is pressed.
 * - **bluetoothTask:** Handles sending data from a queue over Bluetooth Serial.
 * - **ledTask:** Controls a status LED to indicate Bluetooth connection and activity.
 * - **buzzerTask:** Provides audible feedback upon successful tag reads.
 *
 * @section proto_sec Communication Protocol
 * Data is transmitted in JSON format for easy parsing by receiving applications.
 * Example: `{"deviceId":"ESP32_RFID_Reader","uid":"A1:B2:C3:D4","cardType":"MIFARE 1K"}`
 *
 * @section hardware_sec Hardware
 * - ESP32 Development Board
 * - MFRC522 RFID Module
 * - Push Button
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

//==============================================================================
// PIN DEFINITIONS
//==============================================================================
#define SS_PIN 5      ///< ESP32 pin GPIO5 -> MFRC522 SDA/SS (Slave Select)
#define RST_PIN 4     ///< ESP32 pin GPIO4 -> MFRC522 RST (Reset)
#define BUZZER_PIN 22 ///< ESP32 pin GPIO22 -> Buzzer positive pin
#define BUTTON_PIN 21 ///< ESP32 pin GPIO21 -> Push button for reading
#define LED_PIN 2     ///< ESP32 pin GPIO13 -> Status LED

//==============================================================================
// GLOBAL OBJECTS & CONSTANTS
//==============================================================================
MFRC522 mfrc522(SS_PIN, RST_PIN);           ///< Instance of the MFRC522 library.
BluetoothSerial BTSerial;                   ///< Instance of the BluetoothSerial library.
const char *DEVICE_ID = "PatroRFID-Reader"; ///< Unique identifier for this device.
volatile bool bluetoothConnected = false;   ///< Global flag to track Bluetooth connection status.

//==============================================================================
// FreeRTOS HANDLES
//==============================================================================
QueueHandle_t jsonDataQueue;       ///< Queue to pass JSON strings from rfidTask to bluetoothTask.
SemaphoreHandle_t buzzerSemaphore; ///< Binary semaphore to trigger the buzzer task.
TaskHandle_t rfidTaskHandle;       ///< Handle for the RFID reader task.
TaskHandle_t bluetoothTaskHandle;  ///< Handle for the Bluetooth sender task.
TaskHandle_t buzzerTaskHandle;     ///< Handle for the buzzer control task.
TaskHandle_t ledTaskHandle;        ///< Handle for the status LED control task.

//==============================================================================
// TASK & FUNCTION PROTOTYPES
//==============================================================================
void rfidTask(void *parameter);
void bluetoothTask(void *parameter);
void buzzerTask(void *parameter);
void ledTask(void *parameter);
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
    while (!Serial)
        ;
    Serial.println("System Initializing...");

    // Configure hardware pins
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

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

    Serial.println("Peripherals initialized.");
    Serial.println("Hold the button and approach an RFID card to read.");

    // Create and pin tasks to a specific core (Core 1)
    xTaskCreatePinnedToCore(rfidTask, "RFID_Task", 4096, NULL, 2, &rfidTaskHandle, 1);
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

//==============================================================================
// LED STATUS TASK
//==============================================================================
/**
 * @brief Manages the status LED in a non-blocking loop.
 * @param parameter Unused task parameter.
 * @note Blinks the LED when Bluetooth is disconnected. When connected, the LED
 * reflects the state of the push button.
 */
void ledTask(void *parameter)
{
    for (;;)
    {
        if (!bluetoothConnected)
        {
            // Blink when not connected
            digitalWrite(LED_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(250));
            digitalWrite(LED_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        else
        {
            // When connected, LED state follows the button
            digitalWrite(LED_PIN, (digitalRead(BUTTON_PIN) == LOW) ? HIGH : LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

//==============================================================================
// RFID READER TASK
//==============================================================================
/**
 * @brief Checks for button press and reads RFID tags.
 * @param parameter Unused task parameter.
 * @note If the button is held, this task polls the MFRC522. On a successful read,
 * it constructs a JSON object and places it in the `jsonDataQueue`.
 */
void rfidTask(void *parameter)
{
    for (;;)
    {
        if (digitalRead(BUTTON_PIN) == LOW)
        {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                // --- Build UID String ---
                String uidString = "";
                for (byte i = 0; i < mfrc522.uid.size; i++)
                {
                    uidString.concat(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
                    uidString.concat(String(mfrc522.uid.uidByte[i], HEX));
                    if (i < mfrc522.uid.size - 1)
                        uidString.concat(":");
                }
                uidString.toUpperCase();

                // --- Get Card Type ---
                MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
                String cardType = mfrc522.PICC_GetTypeName(piccType);

                // --- Create JSON Object ---
                StaticJsonDocument<200> jsonDoc;
                jsonDoc["deviceId"] = DEVICE_ID;
                jsonDoc["uid"] = uidString;
                jsonDoc["cardType"] = cardType;

                // --- Serialize JSON to a string ---
                char jsonString[256];
                serializeJson(jsonDoc, jsonString);

                Serial.print("JSON Generated: ");
                Serial.println(jsonString);

                // --- Send JSON to queue and signal buzzer ---
                xQueueSend(jsonDataQueue, &jsonString, (TickType_t)10);
                xSemaphoreGive(buzzerSemaphore);

                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

//==============================================================================
// BLUETOOTH SENDER TASK
//==============================================================================
/**
 * @brief Waits for a JSON string from the queue and sends it via Bluetooth.
 * @param parameter Unused task parameter.
 * @note This task remains blocked until data is available in `jsonDataQueue`.
 */
void bluetoothTask(void *parameter)
{
    char receivedJson[256];
    for (;;)
    {
        if (xQueueReceive(jsonDataQueue, &receivedJson, portMAX_DELAY) == pdPASS)
        {
            Serial.print("Sending via Bluetooth: ");
            Serial.println(receivedJson);
            BTSerial.println(receivedJson);
        }
    }
}

//==============================================================================
// BUZZER TASK
//==============================================================================
/**
 * @brief Waits for a signal and triggers a short beep on the buzzer.
 * @param parameter Unused task parameter.
 * @note This task remains blocked until `buzzerSemaphore` is given.
 */
void buzzerTask(void *parameter)
{
    for (;;)
    {
        if (xSemaphoreTake(buzzerSemaphore, portMAX_DELAY) == pdTRUE)
        {
            digitalWrite(BUZZER_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(BUZZER_PIN, LOW);
        }
    }
}

//==============================================================================
// MAIN LOOP
//==============================================================================
/**
 * @brief The standard Arduino loop function. It is unused in this FreeRTOS
 * application, as all functionality is handled by dedicated tasks.
 */
void loop()
{
    // This loop is intentionally left empty.
    // The FreeRTOS scheduler is managing all tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
