/**
 * @file ui_handler.h
 * @author Luis Felipe Patrocinio
 * @brief Prototypes for user interface feedback tasks (LED and Buzzer).
 * @date 2025-08-26
 */

#ifndef UI_HANDLER_H
#define UI_HANDLER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief FreeRTOS task to provide audible feedback via the buzzer.
 * @param parameter Unused task parameter.
 */
void buzzerTask(void *parameter);

/**
 * @brief FreeRTOS task to provide visual feedback via the status LED.
 * @param parameter Unused task parameter.
 */
void ledTask(void *parameter);

#endif // UI_HANDLER_H