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
#define RST_PIN 19    ///< ESP32 pin GPIO19 -> MFRC522 RST (Reset)
#define BUZZER_PIN 22 ///< ESP32 pin GPIO22 -> Buzzer positive pin
#define READ_BUTTON_PIN 21  ///< ESP32 pin GPIO21 -> Push button for reading
#define WRITE_BUTTON_PIN 4 ///< ESP32 pin GPIO4 -> Push button for writing
#define LED_PIN 2     ///< ESP32 pin GPIO2 -> Status LED

//==============================================================================
// GLOBAL OBJECTS & CONSTANTS
//==============================================================================
MFRC522 mfrc522(SS_PIN, RST_PIN);          
BluetoothSerial BTSerial;                   
const char *DEVICE_ID = "PatroRFID-Reader"; 
volatile bool bluetoothConnected = false;   
volatile bool modoGravacao = false;         
String dadosParaGravar = "";                 
MFRC522::MIFARE_Key key;                     

//==============================================================================
// FreeRTOS HANDLES
//==============================================================================
QueueHandle_t jsonDataQueue;        
SemaphoreHandle_t buzzerSemaphore;  
TaskHandle_t rfidTaskHandle;        
TaskHandle_t rfidWriteTaskHandle;   
TaskHandle_t bluetoothTaskHandle;   
TaskHandle_t buzzerTaskHandle;      
TaskHandle_t ledTaskHandle;         

//==============================================================================
// FUNCTION PROTOTYPES
//==============================================================================
void bt_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

//==============================================================================
// SETUP FUNCTION
//==============================================================================
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
    pinMode(WRITE_BUTTON_PIN, INPUT_PULLUP);

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
    Serial.println("Hold the read button to scan tags. Hold the write button to write to tags.");

    // Create and pin tasks to a specific core (Core 1)
    xTaskCreatePinnedToCore(rfidTask, "RFID_Task", 4096, NULL, 2, &rfidTaskHandle, 1);
    xTaskCreatePinnedToCore(rfidWriteTask, "RFID_Write_Task", 4096, NULL, 2, &rfidWriteTaskHandle, 1);
    xTaskCreatePinnedToCore(bluetoothTask, "Bluetooth_Task", 4096, NULL, 1, &bluetoothTaskHandle, 1);
    xTaskCreatePinnedToCore(buzzerTask, "Buzzer_Task", 1024, NULL, 1, &buzzerTaskHandle, 1);
    xTaskCreatePinnedToCore(ledTask, "LED_Task", 2048, NULL, 1, &ledTaskHandle, 1);

    Serial.println("FreeRTOS tasks created. System is running.");
}

void loop()
{
    // This loop is intentionally left empty.
    // The FreeRTOS scheduler is managing all tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}

//==============================================================================
// BLUETOOTH EVENT CALLBACK
//==============================================================================
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