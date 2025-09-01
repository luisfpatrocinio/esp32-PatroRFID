/**
 * @file rfid_handler.h
 * @author Luis Felipe Patrocinio
 * @brief Prototypes for RFID handling tasks.
 * @date 2025-08-26
 */

#ifndef RFID_HANDLER_H
#define RFID_HANDLER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief FreeRTOS task to read RFID tags when the button is pressed.
 * @param parameter Unused task parameter.
 */
void rfidTask(void *parameter);

/**
 * @brief FreeRTOS task to write data to an RFID tag.
 * @param parameter Unused task parameter.
 */
void rfidWriteTask(void *parameter);

#endif // RFID_HANDLER_H