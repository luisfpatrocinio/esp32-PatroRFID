#ifndef RFID_TASKS_H
#define RFID_TASKS_H

#include <Arduino.h>
#include <MFRC522.h>
#include "BluetoothSerial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

/**
 * @brief Pin definitions for RFID, Buzzer, Buttons, and LED.
 */
#define SS_PIN 5            ///< ESP32 GPIO5 -> MFRC522 SDA/SS (Slave Select)
#define RST_PIN 4          ///< ESP32 GPIO19 -> MFRC522 RST (Reset)
#define BUZZER_PIN 22       ///< ESP32 GPIO22 -> Buzzer positive pin
#define READ_BUTTON_PIN 21  ///< ESP32 GPIO21 -> Push button for reading RFID
#define LED_PIN 2           ///< ESP32 GPIO2 -> Status LED

//==============================================================================
// TASK PROTOTYPES - Include the new header file to access the information of the created modules.
//==============================================================================

/**
 * @brief Task responsible for reading RFID tags.
 * @param parameter Task parameters (not used in this implementation).
 */
void rfidTask(void *parameter);

/**
 * @brief Task responsible for writing data to RFID tags.
 * @param parameter Task parameters (not used in this implementation).
 */
void rfidWriteTask(void *parameter);

/**
 * @brief Task responsible for handling Bluetooth communication.
 * @param parameter Task parameters (not used in this implementation).
 */
void bluetoothTask(void *parameter);

/**
 * @brief Task responsible for controlling the buzzer alerts.
 * @param parameter Task parameters (not used in this implementation).
 */
void buzzerTask(void *parameter);

/**
 * @brief Task responsible for controlling the status LED.
 * @param parameter Task parameters (not used in this implementation).
 */
void ledTask(void *parameter);

//==============================================================================
// GLOBAL OBJECTS AND HANDLES
//==============================================================================

/** 
 * @brief RFID reader object.
 */
extern MFRC522 mfrc522;

/**
 * @brief Bluetooth Serial communication object.
 */
extern BluetoothSerial BTSerial;

/**
 * @brief RFID MIFARE key used for authentication.
 */
extern MFRC522::MIFARE_Key key;

//==============================================================================
// GLOBAL VARIABLES
//==============================================================================

/**
 * @brief Indicates if the system is in writing mode.
 */
extern volatile bool writeMode;

/**
 * @brief Indicates if a Bluetooth device is connected.
 */
extern volatile bool bluetoothConnected;

/**
 * @brief String containing the data to be written to an RFID tag.
 */
extern String dataToRecord;

//==============================================================================
// FREERTOS PRIMITIVES
//==============================================================================

/**
 * @brief Queue to store JSON data for communication between tasks.
 */
extern QueueHandle_t jsonDataQueue;

/**
 * @brief Semaphore to control buzzer access between tasks.
 */
extern SemaphoreHandle_t buzzerSemaphore;

#endif
