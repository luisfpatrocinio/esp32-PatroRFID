/**
 * @file ble_comm.h
 * @author Luis Felipe Patrocinio
 * @brief Prototypes for BLE communication setup and task.
 * @date 2025-08-26
 */

#ifndef BLE_COMM_H
#define BLE_COMM_H

#include <Arduino.h>

/**
 * @brief Initializes the BLE server, service, characteristic, and starts advertising.
 */
void setupBLE();

/**
 * @brief FreeRTOS task to send data from the queue over BLE notifications.
 * @param parameter Unused task parameter.
 */
void bluetoothTask(void *parameter);

#endif // BLE_COMM_H