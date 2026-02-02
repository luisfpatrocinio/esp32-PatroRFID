//==============================================================================
// Project Header Includes
//==============================================================================
#include "config.h"
/**
 * @file rfid_handler.cpp
 * @author Luis Felipe Patrocinio
 * @brief Implementation of RFID reading and writing tasks.
 * @date 2026-01-26
 */

#include "rfid_handler.h"
#include "rtos_comm.h"
#include <ArduinoJson.h>

//==============================================================================
// Standard Library Includes
//==============================================================================

// (none)

//==============================================================================
// Arduino & ESP32 Includes
//==============================================================================
#include <ArduinoJson.h>

//==============================================================================
// Project Header Includes
//==============================================================================
#include "rfid_handler.h"
#include "rtos_comm.h"
#include "config.h"

//==============================================================================
// RFID READER TASK
//==============================================================================
void rfidTask(void *parameter)
{
    String lastEPC = "";
    R200Tag readTag;

    // Variable to control the time between beeps for the same tag (optional)
    unsigned long lastBeepTime = 0;

    for (;;)
    {
        // Check if the system is in write mode (protected by mutex)
        bool isWriteMode = false;
        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            xSemaphoreGive(writeDataMutex);
        }

        // LOGIC: If button is pressed, send read command (Poll)
        if (digitalRead(READ_BUTTON_PIN) == LOW && !isWriteMode)
        {
            // Send single read command
            rfid.singlePoll();

            // Time window to process incoming data (60ms timeout)
            unsigned long startTime = millis();

            while (millis() - startTime < 60)
            {
                // Process incoming data
                if (rfid.processIncomingData(readTag))
                {
                    // --- FILTERING AND SENDING DATA ---
                    // If it is a new tag OR if enough time has passed (e.g., reset of the same tag)
                    // In UHF mode, as we read many times per second, it is CRUCIAL to filter.
                    // Here we filter: Only notify if the EPC has changed from the last one read IMMEDIATELY before.

                    if (readTag.epc != lastEPC)
                    {
                        lastEPC = readTag.epc;
                        lastBeepTime = millis();

                        // Prepare JSON document
                        JsonDocument jsonDoc;
                        jsonDoc["type"] = "readResult";
                        jsonDoc["content"]["uid"] = readTag.epc;
                        jsonDoc["content"]["rssi"] = readTag.rssi;
                        jsonDoc["content"]["data"] = "EPC Gen2 Data"; // Placeholder

                        // Serialize JSON to string
                        char jsonString[256];
                        serializeJson(jsonDoc, jsonString);

                        // Send JSON data to BLE queue
                        xQueueSend(jsonDataQueue, &jsonString, (TickType_t)5);

                        // Signal buzzer for feedback
                        xSemaphoreGive(buzzerSemaphore);

                        Serial.print("Tag UHF Detectada: ");
                        Serial.println(readTag.epc);
                    }
                    else
                    {
                        // Efeito Ghost: If the same tag is read again after some time, we can still give feedback
                    }
                }
                // Small delay to avoid crashing the CPU and allow the UART buffer to fill up
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        else
        {
            // Reset lastEPC if button is not pressed or in write mode
            if (lastEPC != "")
            {
                lastEPC = "";
                Serial.println("--- Paused Reading (Trigger Released) ---");
            }

            if (Serial2.available())
            {
                // It is important to continue processing residual data that may arrive at the Serial port
                // to keep the buffer clean, even without actively "requesting" a read.
                R200Tag dummy;
                rfid.processIncomingData(dummy);
            }
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
        /*
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
                feedbackDoc["type"] = "feedback";
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
                        feedbackDoc["content"]["message"] = "Write successful: " + localDataToRecord;
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
            }
        }
        */
        // Short delay to avoid busy looping
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}