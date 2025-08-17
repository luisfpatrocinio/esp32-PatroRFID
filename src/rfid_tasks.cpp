#include "rfid_tasks.h"
#include <ArduinoJson.h>

//==============================================================================
// RFID READER TASK
//==============================================================================
/**
 * @brief Checks for button press and reads RFID tags.
 * @param parameter Unused task parameter.
 * @note If the read button is held, this task polls the MFRC522. On a successful read,
 * it constructs a JSON object with UID, card type, and custom data from block 4.
 */
// RFID READER TASK
void rfidTask(void *parameter)
{
    for (;;)
    {
        // Only run if not in write mode
        if (digitalRead(READ_BUTTON_PIN) == LOW && !writeMode)
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

                if (status == MFRC522::STATUS_OK)
                {
                    byte readBlock = 4;  // Bloco onde os dados estão gravados
                    byte readBuffer[18]; // 16 bytes de dados + 2 de CRC
                    byte bufferSize = sizeof(readBuffer);

                    status = mfrc522.MIFARE_Read(readBlock, readBuffer, &bufferSize);
                    if (status == MFRC522::STATUS_OK)
                    {
                        // Certifique-se de que a string é terminada com nulo.
                        readBuffer[16] = '\0';
                        customData = String((char *)readBuffer);
                    }
                    else
                    {
                        customData = "Error reading data.";
                        Serial.print("❌ Read error: ");
                        Serial.println(mfrc522.GetStatusCodeName(status));
                    }
                }
                else
                {
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

//==============================================================================
// RFID WRITER TASK
//==============================================================================
/**
 * @brief Manages the RFID tag writing process.
 * @param parameter Unused task parameter.
 * @note This task waits for the write button to be held. When active, it waits for
 * a simple string from the `bluetoothTask` and then writes the data to the next
 * RFID tag that is presented. It first authenticates with the default key.
 */
void rfidWriteTask(void *parameter)
{
    for (;;)
    {
        if (writeMode && dataToRecord.length() > 0)
        {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                Serial.println("📡 Attempting to authenticate and write...");

                byte trailerBlock = 7;
                MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));

                if (status != MFRC522::STATUS_OK)
                {
                    Serial.print("❌ Authentication failed! Key may not be default: ");
                    Serial.println(mfrc522.GetStatusCodeName(status));
                }
                else
                {
                    Serial.println("✅ Authentication successful!");

                    byte buffer[16] = {0};
                    int len = dataToRecord.length();
                    if (len > 15)
                        len = 15;
                    strncpy((char *)buffer, dataToRecord.c_str(), len);

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
                dataToRecord = "";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

//==============================================================================
// BLUETOOTH SENDER/RECEIVER TASK
//==============================================================================
/**
 * @brief Waits for a JSON string from the queue or receives data from Bluetooth.
 * @param parameter Unused task parameter.
 * @note This task handles both sending read data and receiving write data.
 */
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

            if (msg.equals("*WriteMode"))
            {
                writeMode = true;
                dataToRecord = "";
                Serial.println("🔴 Write mode ACTIVATED via Bluetooth. Send a number (up to 15 digits).");
            }
            else if (msg.equals("*StopWrite"))
            {
                writeMode = false;
                dataToRecord = "";
                Serial.println("🔵 Write mode STOPPED via Bluetooth.");
            }

            else if (writeMode)
            {
                dataToRecord = msgn;
                Serial.print("📥 Data for writing received: ");
                Serial.println(dataToRecord);
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

//==============================================================================
// BUZZER TASK
//==============================================================================
/**
 * @brief Waits for a signal and triggers a short beep on the buzzer.
 * @param parameter Unused task parameter.
 * @note This task remains blocked until `buzzerSemaphore` is given.
 */
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

//==============================================================================
// LED STATUS TASK
//==============================================================================
/**
 * @brief Manages the status LED in a non-blocking loop.
 * @param parameter Unused task parameter.
 * @note Blinks the LED when Bluetooth is disconnected. When connected, the LED
 * reflects the state of the read or write buttons.
 */
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
            digitalWrite(LED_PIN, (digitalRead(READ_BUTTON_PIN) == LOW) ? HIGH : LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}