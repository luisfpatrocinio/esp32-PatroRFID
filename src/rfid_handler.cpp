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
        if (c < 16) hexString += "0";
        hexString += String(c, HEX);
    }
    hexString.toUpperCase();

    // Padding Anti-Fantasma: Garante 96 bits (24 chars) de limpeza
    while (hexString.length() < 24 || hexString.length() % 4 != 0)
    {
        hexString += "0";
    }
    return hexString;
}

String hexToText(String hex)
{
    String text = "";
    for (unsigned int i = 0; i < hex.length(); i += 2)
    {
        String byteString = hex.substring(i, i + 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        if (byte >= 32 && byte <= 126) text += byte;
    }
    return text;
}

//==============================================================================
// RFID READER TASK (Já é contínuo nativamente pelo FreeRTOS)
//==============================================================================
void rfidTask(void *parameter)
{
    String lastEPC = "";
    R200Tag readTag;

    for (;;)
    {
        bool isWriteMode = false;
        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE) {
            isWriteMode = writeMode;
            xSemaphoreGive(writeDataMutex);
        }

        if (isWriteMode) {
            vTaskDelay(pdMS_TO_TICKS(100)); 
            continue;                       
        }

        // LEITURA CONTÍNUA (Enquanto segurar o gatilho)
        if (digitalRead(READ_BUTTON_PIN) == LOW)
        {
            // Limpeza leve para sincronia
            if (Serial2.available()) {
                R200Tag dummy;
                rfid.processIncomingData(dummy);
            }

            rfid.singlePoll();
            unsigned long startTime = millis();

            while (millis() - startTime < 60)
            {
                if (rfid.processIncomingData(readTag))
                {
                    if (readTag.epc != lastEPC)
                    {
                        lastEPC = readTag.epc;
                        String decodedText = hexToText(readTag.epc);

                        JsonDocument jsonDoc;
                        jsonDoc["type"] = "readResult";
                        jsonDoc["content"]["status"] = "ok";
                        jsonDoc["content"]["uid"] = readTag.epc;
                        jsonDoc["content"]["text"] = decodedText;
                        jsonDoc["content"]["rssi"] = readTag.rssi;

                        if (decodedText.length() > 0) jsonDoc["content"]["data"] = decodedText;
                        else jsonDoc["content"]["data"] = readTag.epc;

                        char jsonString[256];
                        serializeJson(jsonDoc, jsonString);
                        xQueueSend(jsonDataQueue, &jsonString, (TickType_t)5);
                        xSemaphoreGive(buzzerSemaphore);

                        Serial.print(">>> LIDO: ");
                        Serial.println(decodedText.length() > 0 ? decodedText : readTag.epc);
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        else
        {
            if (lastEPC != "") lastEPC = "";
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

//==============================================================================
// RFID WRITER TASK (Atualizada para Gravação Contínua)
//==============================================================================
void rfidWriteTask(void *parameter)
{
    for (;;)
    {
        bool isWriteMode = false;
        String localDataToRecord = "";

        if (xSemaphoreTake(writeDataMutex, (TickType_t)10) == pdTRUE) {
            isWriteMode = writeMode;
            localDataToRecord = dataToRecord;
            xSemaphoreGive(writeDataMutex);
        }

        // GRAVAÇÃO CONTÍNUA (Sem a trava de clique único)
        if (isWriteMode && localDataToRecord.length() > 0)
        {
            if (digitalRead(READ_BUTTON_PIN) == LOW)
            {
                String epcToSend = localDataToRecord;
                bool looksLikeText = false;
                for (unsigned int i = 0; i < localDataToRecord.length(); i++) {
                    char c = localDataToRecord.charAt(i);
                    if (!isxdigit(c)) { looksLikeText = true; break; }
                }
                if (looksLikeText) epcToSend = textToHex(localDataToRecord);

                Serial.print("[App BLE] Gravando Continuamente: ");
                Serial.println(localDataToRecord);

                bool success = false;
                int attempts = 0;

                while (attempts < 5 && !success)
                {
                    attempts++;
                    while (Serial2.available()) Serial2.read(); // Flush

                    rfid.writeStatus = 0;
                    rfid.writeEPC(epcToSend);

                    unsigned long startTime = millis();
                    while (millis() - startTime < 800) {
                        R200Tag dummy;
                        rfid.processIncomingData(dummy);
                        if (rfid.writeStatus != 0) break; 
                        vTaskDelay(pdMS_TO_TICKS(5));
                    }

                    if (rfid.writeStatus == 1) success = true;
                    else vTaskDelay(pdMS_TO_TICKS(100));
                }

                JsonDocument responseDoc;

                if (success)
                {
                    responseDoc["type"] = "writeResult";
                    responseDoc["content"]["status"] = "ok";
                    responseDoc["content"]["uid"] = epcToSend;
                    responseDoc["content"]["data"] = localDataToRecord;
                    responseDoc["content"]["message"] = "Gravado com Sucesso!";

                    xSemaphoreGive(buzzerSemaphore); 
                    vTaskDelay(pdMS_TO_TICKS(100));
                    xSemaphoreGive(buzzerSemaphore); // Bip duplo para sucesso
                    
                    Serial.println("   >>> SUCESSO! <<<");
                    
                    // Pausa inteligente para o utilizador poder soltar o gatilho ou trocar de etiqueta 
                    // sem spammar o App de mensagens.
                    vTaskDelay(pdMS_TO_TICKS(800)); 
                }
                else
                {
                    responseDoc["type"] = "feedback";
                    responseDoc["content"]["status"] = "error";
                    responseDoc["content"]["message"] = "Falha apos 5 tentativas. Aproxime a tag.";
                    Serial.println("   >>> FALHA NA GRAVACAO.");
                    
                    // Pausa menor caso tenha falhado, permitindo continuar a tentar rapidamente
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