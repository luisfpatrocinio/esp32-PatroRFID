#include "rfid_tasks.h"
#include <ArduinoJson.h>
#include <BLE2902.h>

extern BLECharacteristic *pCharacteristic;

//==============================================================================
// RFID READER TASK
//==============================================================================
/**
 * @brief Checks for button press and reads RFID tags.
 * @param parameter Unused task parameter.
 * @note Reads UID and block 4 data, serializes as JSON, sends to BLE queue and triggers buzzer.
 */
void rfidTask(void *parameter) {
    String lastUID = ""; // Guarda a última tag processada
    for (;;) {
        // Apenas entre no loop de leitura se o botão estiver pressionado e não estiver em modo de escrita
        if (digitalRead(READ_BUTTON_PIN) == LOW && !writeMode) {
            // Verifica a presença de uma nova tag.
            if (mfrc522.PICC_IsNewCardPresent()) { 
                // Se uma tag for encontrada, tenta ler o serial dela.
                if (mfrc522.PICC_ReadCardSerial()) {
                    // --- Get UID ---
                    String uidString = "";
                    for (byte i = 0; i < mfrc522.uid.size; i++) {
                        uidString.concat(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
                        uidString.concat(String(mfrc522.uid.uidByte[i], HEX));
                        if (i < mfrc522.uid.size - 1) uidString.concat(":");
                    }
                    uidString.toUpperCase();

                    // Só processa se for uma tag diferente da última lida
                    if (uidString != lastUID) {
                        lastUID = uidString; // Atualiza a última tag processada

                        // --- Authenticate and Read Data from Block 4 ---
                        String customData = "";
                        byte trailerBlock = 7;
                        MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
                            MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                            trailerBlock,
                            &key,
                            &(mfrc522.uid)
                        );

                        if (status == MFRC522::STATUS_OK) {
                            byte readBlock = 4;
                            byte readBuffer[18]; 
                            byte bufferSize = sizeof(readBuffer);

                            status = mfrc522.MIFARE_Read(readBlock, readBuffer, &bufferSize);
                            if (status == MFRC522::STATUS_OK) {
                                readBuffer[16] = '\0';
                                customData = String((char *)readBuffer);
                            } else {
                                customData = "Error reading data.";
                            }
                        } else {
                            customData = "Authentication failed.";
                        }

                        // --- Send JSON via queue ---
                        JsonDocument jsonDoc;
                        jsonDoc["uid"] = uidString;
                        jsonDoc["OEM-Codigo"] = customData;
                        char jsonString[128];
                        serializeJson(jsonDoc, jsonString);
                        xQueueSend(jsonDataQueue, &jsonString, (TickType_t)10);
                        xSemaphoreGive(buzzerSemaphore);
                    }
                    // A tag foi lida, então a impedimos de ser lida novamente até o botão ser solto.
                    mfrc522.PICC_HaltA();
                    mfrc522.PCD_StopCrypto1();
                }
            }
        } else {
            // Se o botão não estiver pressionado, reseta a última UID lida para permitir uma nova leitura
            // quando o botão for pressionado novamente.
            lastUID = "";
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


//==============================================================================
// RFID WRITER TASK
//==============================================================================
/**
 * @brief Manages RFID tag writing process.
 * @param parameter Unused task parameter.
 * @note Waits for BLE write data and writes to next tag presented.
 */
void rfidWriteTask(void *parameter) {
    for (;;) {
        if (writeMode && dataToRecord.length() > 0) {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

                Serial.println("📡 Attempting to authenticate and write...");

                byte trailerBlock = 7;
                MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
                    MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                    trailerBlock,
                    &key,
                    &(mfrc522.uid)
                );

                if (status != MFRC522::STATUS_OK) {
                    Serial.print("❌ Authentication failed! Key may not be default: ");
                    Serial.println(mfrc522.GetStatusCodeName(status));
                } else {
                    Serial.println("✅ Authentication successful!");

                    // Prepare buffer
                    byte buffer[16] = {0};
                    int len = dataToRecord.length();
                    if (len > 15) len = 15;
                    strncpy((char *)buffer, dataToRecord.c_str(), len);

                    byte block = 4;
                    status = mfrc522.MIFARE_Write(block, buffer, 16);
                    if (status == MFRC522::STATUS_OK) {
                        Serial.println("✅ Write successful!");
                        xSemaphoreGive(buzzerSemaphore);
                    } else {
                        Serial.print("❌ Write error: ");
                        Serial.println(mfrc522.GetStatusCodeName(status));
                    }
                }

                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
                dataToRecord = "";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

//==============================================================================
// BLUETOOTH SENDER TASK
//==============================================================================
/**
 * @brief Sends JSON strings via BLE notify.
 * @param parameter Unused task parameter.
 */
void bluetoothTask(void *parameter) {
    char receivedJson[256];
    for (;;) {
        if (xQueueReceive(jsonDataQueue, &receivedJson, 0) == pdPASS) {
            if (bluetoothConnected && pCharacteristic != nullptr) {
                Serial.print("📤 Sending via BLE: ");
                Serial.println(receivedJson);

                pCharacteristic->setValue((uint8_t *)receivedJson, strlen(receivedJson));
                pCharacteristic->notify();
            } else {
                Serial.println("⚠️ BLE device not connected. JSON discarded.");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

//==============================================================================
// BUZZER TASK
//==============================================================================
/**
 * @brief Triggers a short beep on the buzzer.
 * @param parameter Unused task parameter.
 */
void buzzerTask(void *parameter) {
    for (;;) {
        if (xSemaphoreTake(buzzerSemaphore, portMAX_DELAY) == pdTRUE) {
            digitalWrite(BUZZER_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(BUZZER_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
            digitalWrite(BUZZER_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(BUZZER_PIN, LOW);
        }
    }
}

//==============================================================================
// LED STATUS TASK
//==============================================================================
/**
 * @brief Manages the status LED in a non-blocking loop.
 * @param parameter Unused task parameter.
 * @note Blinks LED when BLE disconnected. Reflects button state when connected.
 */
void ledTask(void *parameter) {
    for (;;) {
        if (!bluetoothConnected) {
            digitalWrite(LED_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(250));
            digitalWrite(LED_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            digitalWrite(LED_PIN, (digitalRead(READ_BUTTON_PIN) == LOW) ? HIGH : LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
