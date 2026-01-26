/**
 * @file config.h
 * @author Luis Felipe Patrocinio
 * @brief Centralized hardware pin definitions and application constants.
 * @date 2026-01-26
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

//==============================================================================
// PIN DEFINITIONS
//==============================================================================
#define R200_RX_PIN 16     ///< RX do ESP32 liga ao TX do R200
#define R200_TX_PIN 17     ///< TX do ESP32 liga ao RX do R200
#define R200_BAUDRATE 115200
#define BUZZER_PIN 22      ///< ESP32 GPIO22 -> Buzzer positive pin
#define READ_BUTTON_PIN 21 ///< ESP32 GPIO21 -> Push button for reading RFID
#define LED_PIN 2          ///< ESP32 GPIO2 -> Status LED

extern volatile bool soundEnabled;

#endif // CONFIG_H