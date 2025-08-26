#ifndef RFID_TASKS_H
#define RFID_TASKS_H

#include <Arduino.h>
#include <MFRC522.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "config.h"

//==============================================================================
// TASK PROTOTYPES
//==============================================================================
/**
 * @brief Task responsible for reading RFID tags.
 */
void rfidTask(void *parameter);

/**
 * @brief Task responsible for writing data to RFID tags.
 */
void rfidWriteTask(void *parameter);

/**
 * @brief Task responsible for sending JSON data via BLE notify.
 */
void bluetoothTask(void *parameter);

/**
 * @brief Task responsible for controlling buzzer alerts.
 */
void buzzerTask(void *parameter);

/**
 * @brief Task responsible for controlling the status LED.
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
 * @brief Indicates if a BLE device is connected.
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

extern SemaphoreHandle_t writeDataMutex;

/**
 * @brief Semaphore to control buzzer access between tasks.
 */
extern SemaphoreHandle_t buzzerSemaphore;

#endif // RFID_TASKS_H
