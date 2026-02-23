/**
 * @file main.cpp
 * @brief Sistema R200 - Operação Contínua (Segurar botão) com Logs Detalhados
 */

#include <Arduino.h>
#include "config.h"
#include "R200.h"

// --- OBJETOS GLOBAIS ---
R200Driver rfid(Serial2);

// --- VARIÁVEIS DE CONTROLE ---
bool isWriteMode = false;
String dataToWrite = "PROD-001";
int productCounter = 1;
unsigned long lastContentChange = 0;

// --- CONTROLE DO BOTÃO ---
int clickCount = 0;
unsigned long lastClickTime = 0;
int lastButtonState = HIGH;
unsigned long pressTime = 0;
bool isLongPress = false; // Flag para identificar se está segurando o botão

// =============================================================================
// FUNÇÕES DE TRADUÇÃO E PADDING
// =============================================================================

// Converte texto para Hex e preenche espaço vazio (Evita "fantasmas" antigos)
static String textToHex(String text)
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

    // Garante 96 bits (24 chars hexadecimais) preenchendo com zeros
    while (hexString.length() < 24 || hexString.length() % 4 != 0)
    {
        hexString += "0";
    }
    return hexString;
}

// Converte Hex para Texto
static String hexToText(String hex)
{
    String text = "";
    for (unsigned int i = 0; i < hex.length(); i += 2)
    {
        String byteString = hex.substring(i, i + 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        if (byte >= 32 && byte <= 126)
        {
            text += byte;
        }
    }
    return text;
}

// --- PROTÓTIPOS ---
void performRead();
void performWrite();
void toggleMode();

void setup()
{
    Serial.begin(115200);
    while (!Serial)
        ;
    Serial.println("\n\n=== SISTEMA R200: TESTE CONTINUO ===");

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(READ_BUTTON_PIN, INPUT_PULLUP);

    Serial.println("-> Iniciando Modulo R200...");
    rfid.begin();
    delay(500);

    rfid.setRegionUS();
    delay(100);
    rfid.setTxPower(26);
    delay(100);

    Serial.println("-> Sistema Pronto!");
    Serial.println("MODE: [LEITURA]");
    Serial.println("-> INSTRUCOES: Segure o gatilho para acao continua. Clique 2x rapido para trocar modo.\n");
}

void loop()
{
    // 1. ATUALIZAÇÃO AUTOMÁTICA (A cada 5s)
    if (millis() - lastContentChange > 5000)
    {
        lastContentChange = millis();
        productCounter++;

        // Lista de nomes predefinidos
        const char *nomes[] = {"PATRO", "RYAN", "RUAN", "DANIEL", "JP", "FELIPE"};
        int quantidadeDeNomes = sizeof(nomes) / sizeof(nomes[0]);

        // Escolhe um nome aleatório (de 0 até quantidadeDeNomes - 1)
        int indexSorteado = random(0, quantidadeDeNomes);

        // Buffer aumentado para 16 para caber nomes maiores + contador (Ex: "FELIPE-999")
        char buffer[16];
        sprintf(buffer, "%s-%03d", nomes[indexSorteado], productCounter);
        dataToWrite = String(buffer);

        if (isWriteMode)
        {
            Serial.print("\n[AUTO] Proximo valor engatilhado para gravar: ");
            Serial.println(dataToWrite);
        }
    }

    // 2. LÓGICA DO BOTÃO (Single Click, Double Click e HOLD)
    int currentState = digitalRead(READ_BUTTON_PIN);

    // Borda de Descida (Apertou)
    if (currentState == LOW && lastButtonState == HIGH)
    {
        pressTime = millis();
        isLongPress = false;
    }

    // Enquanto estiver pressionado...
    if (currentState == LOW)
    {
        // Se segurou por mais de 300ms, inicia o MODO CONTÍNUO
        if (millis() - pressTime > 300)
        {
            isLongPress = true;

            if (isWriteMode)
            {
                performWrite();
                delay(300); // Pausa de 300ms para não travar o módulo com excesso de gravações
            }
            else
            {
                performRead();
                // O performRead já tem sua janela natural de tempo, não precisa de delay extra
            }
        }
    }

    // Borda de Subida (Soltou)
    if (currentState == HIGH && lastButtonState == LOW)
    {
        // Se foi um clique rápido (não foi hold longo), conta para o duplo clique
        if (millis() - pressTime > 50 && !isLongPress)
        {
            clickCount++;
            lastClickTime = millis();
        }

        if (isLongPress)
        {
            Serial.println("--- [ GATILHO SOLTO - FIM DA ACAO CONTINUA ] ---\n");
        }
    }
    lastButtonState = currentState;

    // Processamento do clique (Timeout de 400ms)
    if (clickCount > 0 && (millis() - lastClickTime > 400))
    {
        if (clickCount >= 2)
            toggleMode(); // Duplo clique
        else
        {
            // Clique Único (Executa a ação 1 vez)
            if (isWriteMode)
                performWrite();
            else
                performRead();
        }
        clickCount = 0;
    }
}

// =============================================================================
// FUNÇÕES DE AÇÃO
// =============================================================================

void toggleMode()
{
    isWriteMode = !isWriteMode;
    digitalWrite(BUZZER_PIN, HIGH);
    delay(50);
    digitalWrite(BUZZER_PIN, LOW);
    delay(50);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(50);
    digitalWrite(BUZZER_PIN, LOW);

    Serial.println("\n--------------------------------");
    Serial.print(">>> MODO ALTERADO PARA: ");
    Serial.println(isWriteMode ? "ESCRITA (GRAVAR)" : "LEITURA (SCANNER)");
    if (isWriteMode)
    {
        Serial.print("   Dado atual na memoria para gravar: ");
        Serial.println(dataToWrite);
    }
    Serial.println("--------------------------------\n");
}

void performRead()
{
    Serial.println("[SCAN] Iniciando varredura...");
    digitalWrite(LED_PIN, HIGH);

    while (Serial2.available())
        Serial2.read();
    rfid.singlePoll();

    unsigned long start = millis();
    bool tagFound = false;
    R200Tag tag;

    // Janela de escuta
    while (millis() - start < 150)
    {
        if (rfid.processIncomingData(tag))
        {
            tagFound = true;
            String textoTraduzido = hexToText(tag.epc);

            Serial.print("   >> TAG: ");
            Serial.print(tag.epc);

            if (textoTraduzido.length() > 0)
            {
                Serial.print(" -> TEXTO: [ ");
                Serial.print(textoTraduzido);
                Serial.print(" ]");
            }
            Serial.print(" (RSSI: ");
            Serial.print(tag.rssi);
            Serial.println(")");

            digitalWrite(BUZZER_PIN, HIGH);
            delay(20);
            digitalWrite(BUZZER_PIN, LOW);
        }
    }

    if (!tagFound)
        Serial.println("   ... Nenhuma tag detectada nesta janela.");
    digitalWrite(LED_PIN, LOW);
}

void performWrite()
{
    Serial.print("[GRAVACAO] Tentando gravar: ");
    Serial.println(dataToWrite);

    String hexToSend = textToHex(dataToWrite);
    digitalWrite(LED_PIN, HIGH);
    bool success = false;

    for (int i = 1; i <= 3; i++)
    {
        while (Serial2.available())
            Serial2.read();

        rfid.writeStatus = 0;
        rfid.writeEPC(hexToSend);

        unsigned long start = millis();
        while (millis() - start < 800)
        {
            R200Tag dummy;
            rfid.processIncomingData(dummy);
            if (rfid.writeStatus != 0)
                break;
        }

        if (rfid.writeStatus == 1)
        {
            success = true;
            break;
        }
        else
        {
            Serial.print("   ... Falha na tentativa ");
            Serial.print(i);
            Serial.println("/3");
        }
    }

    if (success)
    {
        Serial.println("   >>> SUCESSO! ETIQUETA GRAVADA! <<<");
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
    }
    else
    {
        Serial.println("   >>> ERRO: Falha ao gravar. Aproxime a tag.");
        for (int k = 0; k < 2; k++)
        {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(80);
            digitalWrite(BUZZER_PIN, LOW);
            delay(80);
        }
    }

    digitalWrite(LED_PIN, LOW);
}