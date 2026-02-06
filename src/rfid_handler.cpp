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
#include <ctype.h> // For isxdigit

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

    for (;;)
    {
        // Check if the system is in write mode (protected by mutex)
        bool isWriteMode = false;
        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            xSemaphoreGive(writeDataMutex);
        }

        // If we are in write mode, this task MUST completely stop manipulating Serial2,
        // otherwise it will steal the response from the write task.
        if (isWriteMode)
        {
            vTaskDelay(pdMS_TO_TICKS(100)); // Dorme um pouco e tenta de novo
            continue;                       // Pula todo o resto do loop
        }

        // LOGIC: If button is pressed, send read command (Poll)
        if (digitalRead(READ_BUTTON_PIN) == LOW)
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

                        // Tenta decodificar o EPC para Texto Legível
                        String decodedText = hexToText(readTag.epc);

                        // Prepare JSON document
                        JsonDocument jsonDoc;
                        jsonDoc["type"] = "readResult";
                        jsonDoc["content"]["status"] = "ok";

                        // Envia os dois formatos: hex bruto (UID) e o decodificado (data)
                        jsonDoc["content"]["uid"] = readTag.epc;
                        jsonDoc["content"]["text"] = decodedText;
                        jsonDoc["content"]["rssi"] = readTag.rssi;

                        // Se o texto decodificado parecer válido, mostramos ele no campo data
                        if (decodedText.length() > 0)
                            jsonDoc["content"]["data"] = decodedText;
                        else
                            jsonDoc["content"]["data"] = readTag.epc;

                        // Serialize JSON to string
                        char jsonString[256];
                        serializeJson(jsonDoc, jsonString);

                        // Send JSON data to BLE queue
                        xQueueSend(jsonDataQueue, &jsonString, (TickType_t)5);

                        // Signal buzzer for feedback
                        xSemaphoreGive(buzzerSemaphore);

                        Serial.print("Lido: ");
                        Serial.println(decodedText.length() > 0 ? decodedText : readTag.epc);
                    }
                }
                // Small delay to avoid crashing the CPU and allow the UART buffer to fill up
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        else
        {
            if (lastEPC != "")
                lastEPC = "";

            // Limpeza ociosa (Só se NÃO estiver em modo escrita, garantido pelo if lá em cima)
            if (Serial2.available())
            {
                R200Tag dummy;
                rfid.processIncomingData(dummy);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

//==============================================================================
// RFID WRITER TASK (Adaptado para R200 com Feedback JSON)
//==============================================================================
void rfidWriteTask(void *parameter)
{
    bool triggerLocked = false;

    for (;;)
    {
        bool isWriteMode = false;
        String localDataToRecord = "";

        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            localDataToRecord = dataToRecord;
            xSemaphoreGive(writeDataMutex);
        }

        if (isWriteMode && localDataToRecord.length() > 0)
        {
            if (digitalRead(READ_BUTTON_PIN) == LOW)
            {
                if (!triggerLocked)
                {
                    // Prepara dados
                    String epcToSend = localDataToRecord;
                    bool looksLikeText = false;
                    for (unsigned int i = 0; i < localDataToRecord.length(); i++)
                    {
                        char c = localDataToRecord.charAt(i);
                        if (!isxdigit(c))
                        {
                            looksLikeText = true;
                            break;
                        }
                    }
                    if (looksLikeText)
                        epcToSend = textToHex(localDataToRecord);

                    Serial.print("Iniciando Ciclo de Gravacao: ");
                    Serial.println(localDataToRecord);

                    // --- SISTEMA DE RETRY (5 Tentativas) ---
                    bool success = false;
                    int attempts = 0;

                    while (attempts < 5 && !success)
                    {
                        attempts++;
                        Serial.print("Tentativa ");
                        Serial.print(attempts);
                        Serial.println("/5...");

                        // Limpa Buffer
                        while (Serial2.available())
                            Serial2.read();

                        // Envia
                        rfid.writeStatus = 0;
                        rfid.writeEPC(epcToSend);

                        // Espera (800ms)
                        unsigned long startTime = millis();
                        while (millis() - startTime < 800)
                        {
                            R200Tag dummy;
                            rfid.processIncomingData(dummy);
                            if (rfid.writeStatus != 0)
                                break; // Respondeu algo!
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }

                        // Verifica Sucesso
                        if (rfid.writeStatus == 1)
                        {
                            success = true;
                        }
                        else
                        {
                            // Se falhar, pequeno delay antes de tentar de novo
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                    }
                    // ---------------------------------------

                    JsonDocument responseDoc;

                    if (success)
                    {
                        responseDoc["type"] = "writeResult";
                        responseDoc["content"]["status"] = "ok";
                        responseDoc["content"]["uid"] = epcToSend;
                        responseDoc["content"]["data"] = localDataToRecord;
                        responseDoc["content"]["message"] = "Gravado com Sucesso!";

                        xSemaphoreGive(buzzerSemaphore); // Bip
                        // Beep duplo para celebrar
                        vTaskDelay(pdMS_TO_TICKS(100));
                        xSemaphoreGive(buzzerSemaphore);
                    }
                    else
                    {
                        responseDoc["type"] = "feedback";
                        responseDoc["content"]["status"] = "error";
                        String errMsg = "Falha apos 5 tentativas. Aproxime a tag.";
                        if (rfid.writeStatus == 0x10)
                            errMsg = "Erro: Tag nao encontrada (proxime mais)";
                        else if (rfid.writeStatus == 0x16)
                            errMsg = "Erro: Acesso Negado";

                        responseDoc["content"]["message"] = errMsg;
                        Serial.println(errMsg);
                    }

                    char responseJson[256];
                    serializeJson(responseDoc, responseJson);
                    xQueueSend(jsonDataQueue, &responseJson, (TickType_t)10);

                    triggerLocked = true;
                }
            }
            else
            {
                triggerLocked = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

//==============================================================================
// HELPER FUNCTIONS (Conversão Texto <-> Hex)
//==============================================================================

// Converte texto normal (Ex: "DISCOR") para Hex (Ex: "444953434F52")
String textToHex(String text)
{
    String hexString = "";
    for (unsigned int i = 0; i < text.length(); i++)
    {
        char c = text.charAt(i);
        if (c < 16)
            hexString += "0";
        hexString += String(c, HEX);
    }
    hexString.toUpperCase();

    // Padding: Garante que o tamanho final é múltiplo de 4 (Requisito Gen2)
    while (hexString.length() % 4 != 0)
    {
        hexString += "0";
    }
    return hexString;
}

// Converte Hex (Ex: "444953434F52") de volta para Texto (Ex: "DISCOR")
String hexToText(String hex)
{
    String text = "";
    for (unsigned int i = 0; i < hex.length(); i += 2)
    {
        // Pega pares de caracteres (bytes)
        String byteString = hex.substring(i, i + 2);
        // Converte base 16 para char
        char byte = (char)strtol(byteString.c_str(), NULL, 16);

        // Só adiciona se for caractere imprimível (evita lixo de memória e caracteres de controle)
        if (byte >= 32 && byte <= 126)
        {
            text += byte;
        }
    }
    return text;
}