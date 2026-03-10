#include "config.h"
#include "rfid_handler.h"
#include "rtos_comm.h"
#include <ArduinoJson.h>
#include <ctype.h>

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================

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

    // Preenche com ZEROS (Limpeza) até atingir 24 caracteres (96 bits)
    while (hexString.length() < 24)
    {
        hexString += "0";
    }

    // Trava de segurança: garante que nunca excede o limite da memória EPC
    if (hexString.length() > 24)
        hexString = hexString.substring(0, 24);

    return hexString;
}

String hexToText(String hex)
{
    String text = "";
    for (unsigned int i = 0; i < hex.length(); i += 2)
    {
        String byteString = hex.substring(i, i + 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        if (byte >= 32 && byte <= 126)
            text += byte;
    }
    return text;
}

//==============================================================================
// RFID READER TASK
//==============================================================================
void rfidTask(void *parameter)
{
    String lastTID = "";
    R200Tag readTag;

    for (;;)
    {
        bool isWriteMode = false;
        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE)
        {
            isWriteMode = writeMode;
            xSemaphoreGive(writeDataMutex);
        }

        if (isWriteMode)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (digitalRead(READ_BUTTON_PIN) == LOW)
        {
            while (Serial2.available())
                Serial2.read();
            rfid.singlePoll();

            unsigned long startTime = millis();
            bool tagFound = false;

            while (millis() - startTime < 60)
            {
                if (rfid.processIncomingData(readTag))
                {
                    // Filtro de sanidade: descarta pacotes gigantescos (lixo)
                    if (readTag.epc.length() <= 32)
                    {
                        tagFound = true;
                        break; // Achei uma tag, interrompo para não poluir o buffer
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(2));
            }

            if (tagFound)
            {
                // Dá um instante para o R200 terminar de falar e limpa a linha
                vTaskDelay(pdMS_TO_TICKS(15));
                while (Serial2.available())
                    Serial2.read();

                // LÊ O IDENTIFICADOR ÚNICO DE FÁBRICA
                String hardwareTID = rfid.getTID();

                // Se o TID falhar, NUNCA usar o EPC como plano B. Apenas ignora e tenta de novo.
                if (hardwareTID != "" && hardwareTID != lastTID)
                {
                    lastTID = hardwareTID;
                    String decodedText = hexToText(readTag.epc);

                    JsonDocument jsonDoc;
                    jsonDoc["type"] = "readResult";
                    jsonDoc["content"]["status"] = "ok";

                    // A App recebe o UID imutável do chip
                    jsonDoc["content"]["uid"] = hardwareTID;
                    jsonDoc["content"]["rssi"] = readTag.rssi;
                    jsonDoc["content"]["data"] = (decodedText.length() > 0) ? decodedText : readTag.epc;

                    char jsonString[256];
                    serializeJson(jsonDoc, jsonString);
                    xQueueSend(jsonDataQueue, &jsonString, (TickType_t)5);
                    xSemaphoreGive(buzzerSemaphore);

                    Serial.print(">>> LIDO | TID (Físico): ");
                    Serial.print(hardwareTID);
                    Serial.print(" | DATA: ");
                    Serial.println(decodedText);
                }
            }
        }
        else
        {
            if (lastTID != "")
                lastTID = "";
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

//==============================================================================
// RFID WRITER TASK (Com Memória de Sessão e Varredura Rápida)
//==============================================================================

// Variáveis para a Memória de Sessão
#define MAX_SESSION_TAGS 50
String sessionWrittenTags[MAX_SESSION_TAGS];
int sessionWrittenCount = 0;

void rfidWriteTask(void *parameter)
{
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

        // Monitora o estado do botão
        int currentButtonState = digitalRead(READ_BUTTON_PIN);

        // Se o botão for solto, limpa a memória para a próxima sessão de gravação
        if (currentButtonState == HIGH)
        {
            sessionWrittenCount = 0;
        }

        if (isWriteMode)
        {
            if (currentButtonState == LOW)
            {
                // 1. DESCOBRIR A UID DA ETIQUETA EM CAMPO
                String targetTID = "";
                R200Tag tempTag;
                bool tagFound = false;

                while (Serial2.available())
                    Serial2.read();
                rfid.singlePoll();

                unsigned long pollStart = millis();
                while (millis() - pollStart < 80) // Acelerado para 80ms
                {
                    if (rfid.processIncomingData(tempTag))
                    {
                        if (tempTag.epc.length() <= 32)
                        {
                            tagFound = true;
                            break;
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(2));
                }

                if (tagFound)
                {
                    vTaskDelay(pdMS_TO_TICKS(15));
                    while (Serial2.available())
                        Serial2.read();

                    targetTID = rfid.getTID();
                }

                // Se não achou uma tag válida ou se a extração do TID físico falhou, recomeça
                if (targetTID == "")
                {
                    vTaskDelay(pdMS_TO_TICKS(30)); // Varrer mais rápido
                    continue;
                }

                // --- NOVO: FILTRO ANTI-REPETIÇÃO ---
                bool alreadyWritten = false;
                for (int i = 0; i < sessionWrittenCount; i++)
                {
                    if (sessionWrittenTags[i] == targetTID)
                    {
                        alreadyWritten = true;
                        break;
                    }
                }

                if (alreadyWritten)
                {
                    // Já gravamos nesta etiqueta! Pula imediatamente sem demorar.
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
                // -----------------------------------

                // 2. PREPARAR DADOS E GRAVAR
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
                else
                {
                    while (epcToSend.length() < 24)
                        epcToSend += "0";
                    if (epcToSend.length() > 24)
                        epcToSend = epcToSend.substring(0, 24);
                }

                Serial.print("[App BLE] Gravando: ");
                Serial.println(localDataToRecord);
                Serial.print("          Na Tag TID: ");
                Serial.println(targetTID);

                bool success = false;
                int attempts = 0;

                while (attempts < 5 && !success)
                {
                    attempts++;
                    while (Serial2.available())
                        Serial2.read();

                    rfid.writeStatus = 0;
                    rfid.writeEPC(epcToSend);

                    unsigned long startTime = millis();
                    while (millis() - startTime < 800)
                    {
                        R200Tag dummy;
                        rfid.processIncomingData(dummy);
                        if (rfid.writeStatus != 0)
                            break;
                        vTaskDelay(pdMS_TO_TICKS(5));
                    }

                    if (rfid.writeStatus == 1)
                        success = true;
                    else
                        vTaskDelay(pdMS_TO_TICKS(100));
                }

                // 3. ENVIO DE FEEDBACK AO APP
                JsonDocument responseDoc;

                if (success)
                {
                    // Registra que a gravação teve sucesso para ignorar na próxima volta!
                    if (sessionWrittenCount < MAX_SESSION_TAGS)
                    {
                        sessionWrittenTags[sessionWrittenCount++] = targetTID;
                    }

                    responseDoc["type"] = "writeResult";
                    responseDoc["content"]["status"] = "ok";
                    responseDoc["content"]["uid"] = targetTID;
                    responseDoc["content"]["data"] = localDataToRecord;
                    responseDoc["content"]["message"] = "Gravado com Sucesso!";

                    xSemaphoreGive(buzzerSemaphore);
                    vTaskDelay(pdMS_TO_TICKS(80));
                    xSemaphoreGive(buzzerSemaphore);

                    // DELAY DE 1 SEGUNDO REMOVIDO!
                    // Agora espera só uma fração de segundo e já vai para a próxima etiqueta
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                else
                {
                    responseDoc["type"] = "feedback";
                    responseDoc["content"]["status"] = "error";
                    responseDoc["content"]["message"] = "Falha ao gravar.";
                    vTaskDelay(pdMS_TO_TICKS(200));
                }

                char responseJson[256];
                serializeJson(responseDoc, responseJson);
                xQueueSend(jsonDataQueue, &responseJson, (TickType_t)10);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}