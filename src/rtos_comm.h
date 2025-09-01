/**
 * @file rtos_comm.h
 * @author Luis Felipe Patrocinio
 * @brief Extern declarations for global variables and FreeRTOS primitives.
 * @date 2025-08-26
 *
 * @note This header acts as a central point of communication between different
 *       software modules, ensuring they share the same global state and RTOS handles.
 */

#ifndef RTOS_COMM_H
#define RTOS_COMM_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <MFRC522.h>
#include <BLECharacteristic.h>
#include "config.h"

//==============================================================================
// GLOBAL OBJECTS
//==============================================================================
extern MFRC522 mfrc522;                    ///< Global instance of the MFRC522 RFID reader.
extern MFRC522::MIFARE_Key key;            ///< Global MIFARE key for card authentication.
extern BLECharacteristic *pCharacteristic; ///< Global pointer to the BLE characteristic.

//==============================================================================
// GLOBAL STATE VARIABLES
//==============================================================================
extern volatile bool bluetoothConnected; ///< True if a BLE client is connected.
extern volatile bool writeMode;          ///< True if the device is in RFID write mode.
extern String dataToRecord;              ///< Data buffer for the RFID write operation.

//==============================================================================
// FREERTOS PRIMITIVES (HANDLES)
//==============================================================================
/// @brief Queue for passing JSON data from RFID tasks to the BLE task.
extern QueueHandle_t jsonDataQueue;

/// @brief Mutex to protect access to shared variables like `writeMode` and `dataToRecord`.
extern SemaphoreHandle_t writeDataMutex;

/// @brief Binary semaphore used to trigger the buzzer task.
extern SemaphoreHandle_t buzzerSemaphore;

#endif // RTOS_COMM_H