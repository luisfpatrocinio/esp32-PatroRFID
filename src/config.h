/**
 * @file config.h
 * @author Luis Felipe Patrocinio
 * @brief Centralized hardware pin definitions and application constants.
 * @date 2026-02-02
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

//==============================================================================
// PIN DEFINITIONS
//==============================================================================

// --- R200 (UART) ---
#define R200_RX_PIN 16 // RX do ESP32 (Liga ao TX do R200)
#define R200_TX_PIN 17 // TX do ESP32 (Liga ao RX do R200)

// --- Periféricos da Pistola ---
#define BUZZER_PIN 22
#define READ_BUTTON_PIN 21
#define LED_PIN 2

//==============================================================================
// R200 PROTOCOL CONSTANTS (Faltavam estas linhas!)
//==============================================================================
#define R200_BAUDRATE 115200
#define FRAME_HEAD 0xAA // Cabeçalho do pacote
#define FRAME_END 0xDD  // Rodapé do pacote

//==============================================================================
// GLOBAL FLAGS
//==============================================================================
extern volatile bool soundEnabled;

#endif // CONFIG_H