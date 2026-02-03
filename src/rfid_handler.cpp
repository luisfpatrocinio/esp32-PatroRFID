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

                        // Tenta decodificar o EPC para Texto Legível
                        String decodedText = hexToText(readTag.epc);

                        // Prepare JSON document
                        JsonDocument jsonDoc;
                        jsonDoc["type"] = "readResult";

                        // Envia os dois formatos: hex bruto (UID) e o decodificado (data)
                        jsonDoc["content"]["uid"] = readTag.epc;
                        jsonDoc["content"]["rssi"] = readTag.rssi;

                        // Se o texto decodificado parecer válido, mostramos ele no campo data
                        if (decodedText.length() > 0)
                        {
                            jsonDoc["content"]["data"] = decodedText;
                        }
                        else
                        {
                            jsonDoc["content"]["data"] = readTag.epc;
                        }

                        // Serialize JSON to string
                        char jsonString[256];
                        serializeJson(jsonDoc, jsonString);

                        // Send JSON data to BLE queue
                        xQueueSend(jsonDataQueue, &jsonString, (TickType_t)5);

                        // Signal buzzer for feedback
                        xSemaphoreGive(buzzerSemaphore);

                        Serial.print("Tag: ");
                        Serial.print(readTag.epc);
                        Serial.print(" -> Texto: ");
                        Serial.println(decodedText);
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
                    // --- CONVERSÃO AUTOMÁTICA ---
                    // Se o dado recebido não for Hex puro, converte para Hex
                    // Ex: Recebe "DISCOR" -> Converte para "444953434F52"
                    String epcToSend = localDataToRecord;

                    // Verificação simples: Se tiver letras > F, com certeza é texto
                    bool looksLikeText = false;
                    for (char c : localDataToRecord)
                    {
                        if (!isxdigit(c))
                        {
                            looksLikeText = true;
                            break;
                        }
                    }

                    // Se parece texto (ex: tem 'R', 'S', etc) OU se queremos forçar texto
                    // (Recomendado forçar texto se o seu sistema usa nomes como IDs)
                    if (looksLikeText)
                    {
                        epcToSend = textToHex(localDataToRecord);
                    }

                    Serial.print("Gravando Texto: ");
                    Serial.print(localDataToRecord);
                    Serial.print(" -> Hex: ");
                    Serial.println(epcToSend);

                    rfid.writeStatus = 0;

                    // Envia o HEX já tratado
                    rfid.writeEPC(epcToSend);

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
                    JsonDocument feedbackDoc;
                    feedbackDoc["type"] = "feedback";

                    if (responseReceived && rfid.writeStatus == 1)
                    {
                        // SUCESSO
                        feedbackDoc["content"]["status"] = "ok";
                        feedbackDoc["content"]["message"] = "Gravado: " + localDataToRecord;

                        xSemaphoreGive(buzzerSemaphore); // Beep de sucesso
                        Serial.println("Sucesso confirmado via Protocolo.");
                    }
                    else
                    {
                        // ERRO ou TIMEOUT
                        String msg = "Erro na gravacao.";
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