#include "rfid_tasks.h"
#include <ArduinoJson.h>

//==============================================================================
// TASK IMPLEMENTATIONS
//==============================================================================

// RFID READER TASK
void rfidTask(void *parameter)
{
    for (;;)
    {
        // Only run if not in write mode
        if (digitalRead(READ_BUTTON_PIN) == LOW && !modoGravacao)
        {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                // --- Get UID ---
                String uidString = "";
                for (byte i = 0; i < mfrc522.uid.size; i++)
                {
                    uidString.concat(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
                    uidString.concat(String(mfrc522.uid.uidByte[i], HEX));
                    if (i < mfrc522.uid.size - 1)
                        uidString.concat(":");
                }
                uidString.toUpperCase();

                // --- Authenticate and Read Data from Block 4 ---
                String customData = "";
                byte trailerBlock = 7; // Trailer block for Sector 1
                MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));

                if (status == MFRC522::STATUS_OK) {
                    byte readBlock = 4; // Bloco onde os dados estão gravados
                    byte readBuffer[18]; // 16 bytes de dados + 2 de CRC
                    byte bufferSize = sizeof(readBuffer); 

                    status = mfrc522.MIFARE_Read(readBlock, readBuffer, &bufferSize);
                    if (status == MFRC522::STATUS_OK) {
                        // Certifique-se de que a string é terminada com nulo.
                        readBuffer[16] = '\0';
                        customData = String((char*)readBuffer);
                    } else {
                        customData = "Error reading data.";
                        Serial.print("❌ Read error: ");
                        Serial.println(mfrc522.GetStatusCodeName(status));
                    }
                } else {
                    customData = "Authentication failed.";
                    Serial.print("❌ Authentication failed for read: ");
                    Serial.println(mfrc522.GetStatusCodeName(status));
                }
                
                // --- Create JSON Object ---
                JsonDocument jsonDoc; 
                jsonDoc["uid"] = uidString;
                jsonDoc["OEM-Codigo"] = customData;

                // --- Serialize JSON to a string ---
                char jsonString[128]; 
                serializeJson(jsonDoc, jsonString);

                Serial.print("JSON Generated: ");
                Serial.println(jsonString);

                // --- Send JSON to queue and signal buzzer ---
                xQueueSend(jsonDataQueue, &jsonString, (TickType_t)10);
                xSemaphoreGive(buzzerSemaphore);

                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


// RFID WRITER TASK
void rfidWriteTask(void *parameter)
{
    for (;;)
    {
        // Activate write mode when the write button is pressed
        if (digitalRead(WRITE_BUTTON_PIN) == LOW && !modoGravacao)
        {
            modoGravacao = true;
            dadosParaGravar = "";
            Serial.println("🔴 Write mode activated. Send a number (up to 15 digits) via Bluetooth.");
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (modoGravacao && dadosParaGravar.length() > 0)
        {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                Serial.println("📡 Attempting to authenticate and write...");

                byte trailerBlock = 7; 
                MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));

                if (status != MFRC522::STATUS_OK) {
                    Serial.print("❌ Authentication failed! Key may not be default: ");
                    Serial.println(mfrc522.GetStatusCodeName(status));
                } else {
                    Serial.println("✅ Authentication successful!");
                    
                    byte buffer[16] = {0};
                    int len = dadosParaGravar.length();
                    if (len > 15) len = 15;
                    strncpy((char*)buffer, dadosParaGravar.c_str(), len);

                    byte bloco = 4;
                    status = mfrc522.MIFARE_Write(bloco, buffer, 16);
                    if (status == MFRC522::STATUS_OK)
                    {
                        Serial.println("✅ Write successful!");
                        xSemaphoreGive(buzzerSemaphore);
                    }
                    else
                    {
                        Serial.print("❌ Write error: ");
                        Serial.println(mfrc522.GetStatusCodeName(status));
                    }
                }
                
                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
                modoGravacao = false;
                dadosParaGravar = "";
            }
        }
        else if (digitalRead(WRITE_BUTTON_PIN) == HIGH && modoGravacao) {
             modoGravacao = false;
             dadosParaGravar = "";
             Serial.println("🔵 Write mode deactivated.");
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


// BLUETOOTH SENDER/RECEIVER TASK
void bluetoothTask(void *parameter)
{
    char receivedJson[256];
    String msg;
    for (;;)
    {
        if (BTSerial.available())
        {
            msg = BTSerial.readStringUntil('\n');
            msg.trim();

            if (modoGravacao)
            {
                dadosParaGravar = msg;
                Serial.print("📥 Data for writing received: ");
                Serial.println(dadosParaGravar);
            }
            else
            {
                msg.toCharArray(receivedJson, sizeof(receivedJson));
                xQueueSend(jsonDataQueue, &receivedJson, (TickType_t)10);
            }
        }

        if (xQueueReceive(jsonDataQueue, &receivedJson, 0) == pdPASS)
        {
            if (bluetoothConnected)
            {
                Serial.print("Sending via Bluetooth: ");
                Serial.println(receivedJson);
                BTSerial.println(receivedJson);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


// BUZZER TASK
void buzzerTask(void *parameter)
{
    for (;;)
    {
        if (xSemaphoreTake(buzzerSemaphore, portMAX_DELAY) == pdTRUE)
        {
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

// LED STATUS TASK
void ledTask(void *parameter)
{
    for (;;)
    {
        if (!bluetoothConnected)
        {
            digitalWrite(LED_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(250));
            digitalWrite(LED_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        else
        {
            digitalWrite(LED_PIN, (digitalRead(READ_BUTTON_PIN) == LOW || digitalRead(WRITE_BUTTON_PIN) == LOW) ? HIGH : LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}