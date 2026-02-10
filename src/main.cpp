/**
 * @file main.cpp
 * @brief Sistema de Teste R200 (Leitura + Escrita + Troca de Modo)
 * @note Correção: Funções auxiliares agora são 'static' para evitar conflito com rfid_handler.cpp
 */

#include <Arduino.h>
#include "config.h"
#include "R200.h"

// --- OBJETOS GLOBAIS ---
R200Driver rfid(Serial2);

// --- VARIÁVEIS DE CONTROLE ---
bool isWriteMode = false;        // false = Leitura, true = Escrita
String dataToWrite = "PROD-001"; // Conteúdo inicial
int productCounter = 1;
unsigned long lastContentChange = 0;

// --- CONTROLE DO BOTÃO ---
int clickCount = 0;
unsigned long lastClickTime = 0;
int lastButtonState = HIGH;
unsigned long pressTime = 0;

// =============================================================================
// FUNÇÕES DE TRADUÇÃO (ESTÁTICAS PARA EVITAR ERRO DE LINKAGEM)
// =============================================================================

// Converte "PROD" -> "50524F44"
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

    // Padding à direita
    while (hexString.length() % 4 != 0)
    {
        hexString += "0";
    }
    return hexString;
}

// Converte "50524F44" -> "PROD"
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
    Serial.println("\n\n=== SISTEMA R200: LEITURA & GRAVACAO (CORRIGIDO) ===");

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(READ_BUTTON_PIN, INPUT_PULLUP);

    Serial.println("-> Iniciando Modulo R200...");
    rfid.begin();
    delay(500);

    // Configurações
    rfid.setRegionUS();
    delay(100);
    rfid.setTxPower(26);
    delay(100);

    Serial.println("-> Sistema Pronto!");
    Serial.println("MODE: [LEITURA] (Padrao)");
}

void loop()
{
    // 1. ATUALIZAÇÃO AUTOMÁTICA
    if (millis() - lastContentChange > 5000)
    {
        lastContentChange = millis();
        productCounter++;
        char buffer[10];
        sprintf(buffer, "PROD-%03d", productCounter);
        dataToWrite = String(buffer);

        if (isWriteMode)
        {
            Serial.print("[AUTO] Proximo valor a gravar: ");
            Serial.println(dataToWrite);
        }
    }

    // 2. LÓGICA DO BOTÃO
    int currentState = digitalRead(READ_BUTTON_PIN);

    if (currentState == LOW && lastButtonState == HIGH)
        pressTime = millis();

    if (currentState == HIGH && lastButtonState == LOW)
    {
        if (millis() - pressTime > 50)
        {
            clickCount++;
            lastClickTime = millis();
        }
    }
    lastButtonState = currentState;

    if (clickCount > 0 && (millis() - lastClickTime > 400))
    {
        if (clickCount >= 2)
            toggleMode();
        else
        {
            if (isWriteMode)
                performWrite();
            else
                performRead();
        }
        clickCount = 0;
    }
}

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
        Serial.print("   Dado atual na memoria: ");
        Serial.println(dataToWrite);
    }
    Serial.println("--------------------------------\n");
}

void performRead()
{
    Serial.println("\n[SCANNER] Lendo etiquetas...");
    digitalWrite(LED_PIN, HIGH);

    while (Serial2.available())
        Serial2.read();
    rfid.singlePoll();

    unsigned long start = millis();
    bool tagFound = false;
    R200Tag tag;

    while (millis() - start < 200)
    {
        if (rfid.processIncomingData(tag))
        {
            tagFound = true;

            // Tradução Hex -> Texto
            String textoTraduzido = hexToText(tag.epc);

            Serial.print(">>> TAG: ");
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
        Serial.println("... Nenhuma tag encontrada.");
    digitalWrite(LED_PIN, LOW);
}

void performWrite()
{
    Serial.print("\n[GRAVADOR] Gravando Texto: ");
    Serial.println(dataToWrite);

    // Tradução Texto -> Hex
    String hexToSend = textToHex(dataToWrite);

    Serial.print("           (Convertido para Hex: ");
    Serial.print(hexToSend);
    Serial.println(")");

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
            Serial.print("Falha na tentativa ");
            Serial.print(i);
            Serial.println("...");
        }
    }

    if (success)
    {
        Serial.println(">>> SUCESSO! ETIQUETA GRAVADA! <<<");
        digitalWrite(BUZZER_PIN, HIGH);
        delay(300);
        digitalWrite(BUZZER_PIN, LOW);
    }
    else
    {
        Serial.println(">>> ERRO: Nao foi possivel gravar.");
        for (int k = 0; k < 3; k++)
        {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);
            delay(100);
        }
    }

    digitalWrite(LED_PIN, LOW);
}