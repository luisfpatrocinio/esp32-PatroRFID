/**
 * @file rfid_handler.cpp
 * @author Luis Felipe Patrocinio
 * @brief Implementation of RFID reading and writing tasks.
 * @date 2025-08-26
 */

#include "rfid_handler.h"
#include "rtos_comm.h"
#include "config.h"
#include <ArduinoJson.h>

//==============================================================================
// RFID READER TASK
//==============================================================================
void rfidTask(void *parameter)
{
    String lastUID = "";
    for (;;)
    {
        bool isWriteMode = false;
        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            xSemaphoreGive(writeDataMutex);
        }

        if (digitalRead(READ_BUTTON_PIN) == LOW && !isWriteMode)
        {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                String uidString = "";
                for (byte i = 0; i < mfrc522.uid.size; i++)
                {
                    uidString.concat(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
                    uidString.concat(String(mfrc522.uid.uidByte[i], HEX));
                    if (i < mfrc522.uid.size - 1) uidString.concat(":");
                }
                uidString.toUpperCase();

                if (uidString != lastUID)
                {
                    lastUID = uidString;
                    String customData = "";
                    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, TRAILER_BLOCK, &key, &(mfrc522.uid));

                    if (status == MFRC522::STATUS_OK)
                    {
                        byte readBuffer[18];
                        byte bufferSize = sizeof(readBuffer);
                        status = mfrc522.MIFARE_Read(DATA_BLOCK, readBuffer, &bufferSize);
                        customData = (status == MFRC522::STATUS_OK) ? String((char *)readBuffer) : "Error reading data.";
                    }
                    else
                    {
                        customData = "Authentication failed.";
                    }
                    
                    JsonDocument jsonDoc;
                    jsonDoc["type"] = "readResult";
                    jsonDoc["uid"] = uidString;
                    jsonDoc["data"] = customData;
                    char jsonString[128];
                    serializeJson(jsonDoc, jsonString);
                    xQueueSend(jsonDataQueue, &jsonString, (TickType_t)10);
                    xSemaphoreGive(buzzerSemaphore);
                }
                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
            }
        }
        else
        {
            lastUID = "";
        }
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
        bool isWriteMode;
        String localDataToRecord;

        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            localDataToRecord = dataToRecord;
            xSemaphoreGive(writeDataMutex);
        }

        if (isWriteMode && localDataToRecord.length() > 0)
        {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
            {
                Serial.println("📡 Attempting to authenticate and write...");

                JsonDocument feedbackDoc;
                feedbackDoc["type"] = "writeResult";

                MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, TRAILER_BLOCK, &key, &(mfrc522.uid));

                if (status != MFRC522::STATUS_OK)
                {
                    Serial.print("❌ Auth failed: "); Serial.println(mfrc522.GetStatusCodeName(status));
                    feedbackDoc["content"]["status"] = "error";
                    feedbackDoc["content"]["message"] = "Authentication Failed";
                }
                else
                {
                    Serial.println("✅ Authentication successful!");
                    byte buffer[16] = {0};
                    strncpy((char *)buffer, localDataToRecord.c_str(), 15);

                    status = mfrc522.MIFARE_Write(DATA_BLOCK, buffer, 16);
                    if (status == MFRC522::STATUS_OK)
                    {
                        Serial.println("✅ Write successful!");
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["message"] = "Write successful";
                        xSemaphoreGive(buzzerSemaphore);
                    }
                    else
                    {
                        Serial.print("❌ Write error: "); Serial.println(mfrc522.GetStatusCodeName(status));
                        feedbackDoc["content"]["status"] = "error";
                        feedbackDoc["content"]["message"] = "Write Failed: " + String(mfrc522.GetStatusCodeName(status));
                    }
                }

                char feedbackJson[128];
                serializeJson(feedbackDoc, feedbackJson);
                xQueueSend(jsonDataQueue, &feedbackJson, (TickType_t)10);
                
                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();

                if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
                {
                    dataToRecord = "";
                    xSemaphoreGive(writeDataMutex);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}