/**
 * @file rfid_handler.cpp
 * @author Luis Felipe Patrocinio
 * @brief Implementation of RFID reading and writing tasks.
 * @date 2025-09-01
 */

#include "rfid_handler.h"
#include "rtos_comm.h"
#include <ArduinoJson.h>

//==============================================================================
// RFID READER TASK
//==============================================================================
void rfidTask(void *parameter)
{
    String lastUID = "";
    for (;;)
    {
        // Check if the system is in write mode (protected by mutex)
        bool isWriteMode = false;
        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            xSemaphoreGive(writeDataMutex);
        }

        // Only proceed with reading if the button is pressed and not in write mode
        if (digitalRead(READ_BUTTON_PIN) == LOW && !isWriteMode)
        {
            // Check for new RFID tag and read its serial
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                // Build UID string from tag bytes
                String uidString = "";
                for (byte i = 0; i < mfrc522.uid.size; i++)
                {
                    uidString.concat(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
                    uidString.concat(String(mfrc522.uid.uidByte[i], HEX));
                    if (i < mfrc522.uid.size - 1)
                        uidString.concat(":");
                }
                uidString.toUpperCase();

                // Avoid duplicate reads by checking lastUID
                if (uidString != lastUID)
                {
                    lastUID = uidString;
                    String customData = "";
                    // Access the RFID tag's memory block before reading data
                    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, TRAILER_BLOCK, &key, &(mfrc522.uid));

                    if (status == MFRC522::STATUS_OK)
                    {
                        // Read custom data from the specified block
                        byte readBuffer[18];
                        byte bufferSize = sizeof(readBuffer);
                        status = mfrc522.MIFARE_Read(DATA_BLOCK, readBuffer, &bufferSize);
                        customData = (status == MFRC522::STATUS_OK) ? String((char *)readBuffer) : "Error reading data.";
                    }
                    else
                    {
                        // Could not access tag memory block, cannot read data
                        customData = "Tag access error.";
                    }
                    // Prepare JSON with UID and data to send via BLE
                    JsonDocument jsonDoc;
                    jsonDoc["type"] = "readResult";
                    jsonDoc["uid"] = uidString;
                    jsonDoc["data"] = customData;
                    char jsonString[128];
                    serializeJson(jsonDoc, jsonString);
                    // Send JSON to BLE queue for notification
                    xQueueSend(jsonDataQueue, &jsonString, (TickType_t)10);
                    // Signal buzzer task for feedback
                    xSemaphoreGive(buzzerSemaphore);
                }
                // Halt communication with card and stop encryption
                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
            }
        }
        else
        {
            // Reset lastUID if button is not pressed or in write mode
            lastUID = "";
        }
        // Short delay to avoid busy looping
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

//==============================================================================
// RFID WRITER TASK
//==============================================================================
void rfidWriteTask(void *parameter)
{
    for (;;)
    {
        // Check write mode and get data to record (protected by mutex)
        bool isWriteMode;
        String localDataToRecord;

        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            localDataToRecord = dataToRecord;
            xSemaphoreGive(writeDataMutex);
        }

        // Only proceed if in write mode and there is data to write
        if (isWriteMode && localDataToRecord.length() > 0)
        {
            // Wait for a new RFID tag to be presented
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                // Prepare feedback JSON for BLE notification
                JsonDocument feedbackDoc;
                feedbackDoc["type"] = "writeResult";
                Serial.println("Attempting to write data to RFID tag...");

                // Access the RFID tag's memory block before writing data
                MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, TRAILER_BLOCK, &key, &(mfrc522.uid));

                if (status != MFRC522::STATUS_OK)
                {
                    // Could not access tag memory block, cannot write data
                    feedbackDoc["content"]["status"] = "error";
                    feedbackDoc["content"]["message"] = "Tag access error.";
                    Serial.println("Tag access error, cannot write data.");
                }
                else
                {
                    // Prepare buffer and copy data to be written
                    byte buffer[16] = {0};
                    strncpy((char *)buffer, localDataToRecord.c_str(), 15);

                    Serial.println("Attempting to write data to RFID tag...");

                    // Write data to the specified block
                    status = mfrc522.MIFARE_Write(DATA_BLOCK, buffer, 16);
                    if (status == MFRC522::STATUS_OK)
                    {
                        // Write successful, signal buzzer for feedback
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["message"] = "Write successful";
                        xSemaphoreGive(buzzerSemaphore);
                        Serial.println("Data written successfully to RFID tag.");
                    }
                    else
                    {
                        // Write failed, report error
                        feedbackDoc["content"]["status"] = "error";
                        feedbackDoc["content"]["message"] = "Write Failed: " + String(mfrc522.GetStatusCodeName(status));
                        Serial.println("Data write failed: " + String(mfrc522.GetStatusCodeName(status)));
                    }
                }

                // Send feedback JSON to BLE queue
                char feedbackJson[128];
                serializeJson(feedbackDoc, feedbackJson);
                xQueueSend(jsonDataQueue, &feedbackJson, (TickType_t)10);

                // Halt communication with card and stop encryption
                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();

                // Clear dataToRecord after writing (protected by mutex)
                if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
                {
                    dataToRecord = "";
                    xSemaphoreGive(writeDataMutex);
                }
            }
        }
        // Short delay to avoid busy looping
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}