/**
 * @file config.h
 * @author Luis Felipe Patrocinio
 * @brief Centralized hardware pin definitions and application constants.
 * @date 2025-09-01
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h> // For 'byte' type

//==============================================================================
// PIN DEFINITIONS
//==============================================================================
#define SS_PIN 5           ///< ESP32 GPIO5 -> MFRC522 SDA/SS (Slave Select)
#define RST_PIN 4          ///< ESP32 GPIO4 -> MFRC522 RST (Reset)
#define BUZZER_PIN 22      ///< ESP32 GPIO22 -> Buzzer positive pin
#define READ_BUTTON_PIN 21 ///< ESP32 GPIO21 -> Push button for reading RFID
#define LED_PIN 2          ///< ESP32 GPIO2 -> Status LED

//==============================================================================
// RFID CONFIG
//==============================================================================
const byte DATA_BLOCK = 4;    ///< MIFARE block used for storing custom data.
const byte TRAILER_BLOCK = 7; ///< MIFARE sector trailer block for authentication.

extern volatile bool soundEnabled;

#endif // CONFIG_H