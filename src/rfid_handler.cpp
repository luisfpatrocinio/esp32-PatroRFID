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
// RFID WRITER TASK (Adaptado para R200 com Feedback JSON)
//==============================================================================
void rfidWriteTask(void *parameter)
{
    // Trava para evitar disparos múltiplos enquanto segura o gatilho
    bool triggerLocked = false;

    for (;;)
    {
        // 1. Obter dados globais com segurança
        bool isWriteMode = false;
        String localDataToRecord = "";

        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            localDataToRecord = dataToRecord;
            xSemaphoreGive(writeDataMutex);
        }

        // 2. Lógica de Escrita
        // Só prossegue se estiver em modo escrita, tiver dados E o gatilho for apertado
        if (isWriteMode && localDataToRecord.length() > 0)
        {
            // Leitura do Gatilho (Segurança para UHF)
            if (digitalRead(READ_BUTTON_PIN) == LOW)
            {
                if (!triggerLocked) // Só executa uma vez por clique
                {
                    // --- PREPARAÇÃO ---
                    JsonDocument feedbackDoc;
                    feedbackDoc["type"] = "feedback";
                    Serial.println("Tentando escrever na tag (R200)...");

                    // Reseta o status antes de enviar
                    rfid.writeStatus = 0;

                    // --- ENVIO DO COMANDO ---
                    rfid.writeEPC(localDataToRecord);

                    // --- AGUARDAR RESPOSTA (Timeout 500ms) ---
                    // Como é assíncrono, ficamos ouvindo a serial até ter resposta
                    unsigned long startTime = millis();
                    bool responseReceived = false;

                    while (millis() - startTime < 500)
                    {
                        R200Tag dummy;                   // Não usada aqui, apenas para o parser funcionar
                        rfid.processIncomingData(dummy); // Isso vai atualizar rfid.writeStatus

                        if (rfid.writeStatus != 0) // Chegou resposta! (1 ou Erro)
                        {
                            responseReceived = true;
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(5));
                    }

                    // --- GERAÇÃO DO JSON (Baseado no resultado) ---
                    if (responseReceived && rfid.writeStatus == 1)
                    {
                        // SUCESSO
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["message"] = "Gravado com Sucesso: " + localDataToRecord;

                        xSemaphoreGive(buzzerSemaphore); // Beep de sucesso
                        Serial.println("Sucesso confirmado via Protocolo.");
                    }
                    else
                    {
                        // ERRO ou TIMEOUT
                        String msg = "Erro desconhecido";
                        if (!responseReceived)
                            msg = "Timeout (Sem tag?)";
                        else if (rfid.writeStatus == 0x16)
                            msg = "Erro: Acesso Negado/Senha";
                        else if (rfid.writeStatus == 0x10)
                            msg = "Erro: Falha na Tag";
                        else
                            msg = "Erro Code: 0x" + String(rfid.writeStatus, HEX);

                        feedbackDoc["content"]["status"] = "error";
                        feedbackDoc["content"]["message"] = msg;

                        Serial.println(msg);
                    }

                    // --- ENVIO PARA O APP ---
                    char feedbackJson[256];
                    serializeJson(feedbackDoc, feedbackJson);
                    xQueueSend(jsonDataQueue, &feedbackJson, (TickType_t)10);

                    triggerLocked = true; // Impede repetição até soltar o botão
                }
            }
            else
            {
                triggerLocked = false; // Gatilho solto, reseta trava
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}