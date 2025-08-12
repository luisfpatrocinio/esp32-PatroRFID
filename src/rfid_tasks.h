#ifndef RFID_TASKS_H
#define RFID_TASKS_H

#include <Arduino.h>
#include <MFRC522.h>
#include "BluetoothSerial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define SS_PIN 5      ///< ESP32 pin GPIO5 -> MFRC522 SDA/SS (Slave Select)
#define RST_PIN 19    ///< ESP32 pin GPIO19 -> MFRC522 RST (Reset)
#define BUZZER_PIN 22 ///< ESP32 pin GPIO22 -> Buzzer positive pin
#define READ_BUTTON_PIN 21  ///< ESP32 pin GPIO21 -> Push button for reading
#define WRITE_BUTTON_PIN 4 ///< ESP32 pin GPIO4 -> Push button for writing
#define LED_PIN 2     ///< ESP32 pin GPIO2 -> Status LED


// Protótipos das tasks
void rfidTask(void *parameter);
void rfidWriteTask(void *parameter);
void bluetoothTask(void *parameter);
void buzzerTask(void *parameter);
void ledTask(void *parameter);

// Declarações de objetos e handles
extern MFRC522 mfrc522;
extern BluetoothSerial BTSerial;
extern MFRC522::MIFARE_Key key;

// Variáveis globais
extern volatile bool modoGravacao;
extern volatile bool bluetoothConnected;
extern String dadosParaGravar;

// Primitivas do FreeRTOS
extern QueueHandle_t jsonDataQueue;
extern SemaphoreHandle_t buzzerSemaphore;

#endif