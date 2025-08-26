// --- config.h ---
#include <cstddef>

#ifndef CONFIG_H
#define CONFIG_H

//==============================================================================
// PIN DEFINITIONS
//==============================================================================
#define SS_PIN 5           // MFRC522 SDA/SS
#define RST_PIN 4          // MFRC522 RST
#define BUZZER_PIN 22      // Buzzer
#define READ_BUTTON_PIN 21 // Button for reading
#define LED_PIN 2          // Status LED

//==============================================================================
// RFID CONFIG
//==============================================================================
const byte DATA_BLOCK = 4;
const byte TRAILER_BLOCK = 7;

extern volatile bool soundEnabled;

#endif // CONFIG_H